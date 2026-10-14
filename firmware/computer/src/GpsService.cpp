#include "GpsService.hpp"

#include <algorithm>
#include <cstdio>
#include <limits>

#include "esp_log.h"
#include "esp_timer.h"

namespace remus::computer {
namespace {
constexpr char kTag[] = "gps";
constexpr std::uint32_t kBaudRate = 9600;
constexpr std::uint16_t kMeasurementRateMs = 1000;
constexpr std::uint64_t kNmeaFreshnessUs = 2500000;
constexpr std::uint64_t kStaleAfterUs = 3000000;
constexpr std::uint64_t kDiagnosticIntervalUs = 5000000;
constexpr TickType_t kPollInterval = pdMS_TO_TICKS(10);

std::uint32_t ageMilliseconds(std::uint64_t now, std::uint64_t received) noexcept {
  if (received == 0 || now < received) return UINT32_MAX;
  const std::uint64_t age = (now - received) / 1000U;
  return age > UINT32_MAX ? UINT32_MAX : static_cast<std::uint32_t>(age);
}
}  // namespace

Status GpsService::start(std::uint64_t computerId, std::uint32_t bootId) noexcept {
  if (task_ != nullptr) return Status::success();
  if (computerId == 0 || bootId == 0 || !storage_.isRecording())
    return Status::failure(Error::InvalidArgument);
  computerId_ = computerId;
  bootId_ = bootId;
  const Status uartStatus = uart_.start(kBaudRate);
  if (!uartStatus.ok()) return uartStatus;
  task_ = xTaskCreateStatic(taskEntry, "gps", kTaskStackBytes, this, 5,
                            taskStack_, &taskBuffer_);
  return task_ != nullptr ? Status::success() : Status::failure(Error::NotReady);
}

GpsServiceStats GpsService::stats() const noexcept {
  return {bytes_.load(), recordsQueued_.load(), recordsDropped_.load(),
          coherentEpochs_.load(), checksumFailures_.load(),
          fixTransitions_.load()};
}

void GpsService::taskEntry(void* context) noexcept {
  static_cast<GpsService*>(context)->run();
}

bool GpsService::sendUbx(std::uint8_t messageClass, std::uint8_t messageId,
                         const std::uint8_t* payload,
                         std::uint16_t payloadLength) noexcept {
  if (payloadLength > 16 || (payloadLength > 0 && payload == nullptr)) return false;
  std::uint8_t frame[24]{};
  frame[0] = 0xB5;
  frame[1] = 0x62;
  frame[2] = messageClass;
  frame[3] = messageId;
  frame[4] = static_cast<std::uint8_t>(payloadLength);
  frame[5] = static_cast<std::uint8_t>(payloadLength >> 8U);
  for (std::uint16_t index = 0; index < payloadLength; ++index) frame[6 + index] = payload[index];
  std::uint8_t checksumA = 0;
  std::uint8_t checksumB = 0;
  for (std::size_t index = 2; index < 6U + payloadLength; ++index) {
    checksumA = static_cast<std::uint8_t>(checksumA + frame[index]);
    checksumB = static_cast<std::uint8_t>(checksumB + checksumA);
  }
  frame[6 + payloadLength] = checksumA;
  frame[7 + payloadLength] = checksumB;
  std::size_t written = 0;
  return uart_.write(frame, 8U + payloadLength, written).ok() &&
         written == 8U + payloadLength;
}

void GpsService::configureReceiver() noexcept {
  struct MessageRate { std::uint8_t messageClass; std::uint8_t messageId; std::uint8_t rate; };
  constexpr MessageRate messages[] = {
      {0xF0, 0x00, 1},  // GGA: UTC-adjacent fix quality, satellites and HDOP.
      {0xF0, 0x02, 0},  // GSA off.
      {0xF0, 0x03, 5},  // GSV every five navigation epochs.
      {0xF0, 0x04, 1},  // RMC: UTC date and fallback navigation.
      {0xF0, 0x01, 0},  // GLL off.
      {0xF0, 0x05, 0},  // VTG off.
      {0x01, 0x02, 1},  // NAV-POSLLH.
      {0x01, 0x12, 1},  // NAV-VELNED.
      {0x01, 0x03, 1},  // NAV-STATUS.
  };
  for (const auto& message : messages) {
    const std::uint8_t payload[]{message.messageClass, message.messageId, message.rate};
    (void)sendUbx(0x06, 0x01, payload, sizeof(payload));
    vTaskDelay(pdMS_TO_TICKS(20));
  }
  const std::uint8_t ratePayload[]{
      static_cast<std::uint8_t>(kMeasurementRateMs),
      static_cast<std::uint8_t>(kMeasurementRateMs >> 8U),
      0x01, 0x00,  // one navigation solution per measurement
      0x01, 0x00,  // align to GPS time
  };
  (void)sendUbx(0x06, 0x08, ratePayload, sizeof(ratePayload));
  vTaskDelay(pdMS_TO_TICKS(20));
  (void)sendUbx(0x06, 0x08, nullptr, 0);  // Read back the effective rate.
}

void GpsService::reportFixState(bool valid, std::uint8_t fixQuality,
                                std::uint8_t satellites) noexcept {
  if (fixStateKnown_ && valid == fixValid_) return;
  fixStateKnown_ = true;
  fixValid_ = valid;
  ++fixTransitions_;
  if (valid)
    ESP_LOGI(kTag, "GPS: FIX quality=%u satellites=%u", static_cast<unsigned>(fixQuality),
             static_cast<unsigned>(satellites));
  else
    ESP_LOGW(kTag, "GPS: NO_FIX quality=%u satellites=%u", static_cast<unsigned>(fixQuality),
             static_cast<unsigned>(satellites));
}

void GpsService::publishUbx(const gps::NavigationEpoch& epoch,
                            const gps::NmeaSnapshot& nmea) noexcept {
  const std::uint64_t now = static_cast<std::uint64_t>(esp_timer_get_time());
  const bool nmeaFresh = nmea.receivedAtPcMonotonicUs != 0 &&
                         now >= nmea.receivedAtPcMonotonicUs &&
                         now - nmea.receivedAtPcMonotonicUs <= kNmeaFreshnessUs;
  storage::GpsFixPayload fix{};
  fix.pcMonotonicUs = epoch.receivedAtPcMonotonicUs;
  fix.ageMilliseconds = ageMilliseconds(now, epoch.receivedAtPcMonotonicUs);
  fix.flags = gps::kGpsFlagUbxCoherent;
  const bool positionValid = epoch.receiverFixValid &&
                             epoch.latitudeE7 != gps::kUnavailableCoordinateE7 &&
                             epoch.longitudeE7 != gps::kUnavailableCoordinateE7;
  if (epoch.receiverFixValid) fix.flags |= gps::kGpsFlagReceiverFixValid;
  if (positionValid) {
    fix.latitudeE7 = epoch.latitudeE7;
    fix.longitudeE7 = epoch.longitudeE7;
    fix.flags |= gps::kGpsFlagPositionValid;
  }
  if (epoch.receiverFixValid &&
      epoch.groundSpeedMillimetersPerSecond != gps::kUnavailableUnsigned32) {
    fix.speedMillimetersPerSecond = epoch.groundSpeedMillimetersPerSecond;
    fix.flags |= gps::kGpsFlagSpeedValid;
  }
  if (epoch.receiverFixValid && epoch.courseCentiDegrees != gps::kUnavailableUnsigned16 &&
      epoch.groundSpeedMillimetersPerSecond > 100) {
    fix.courseCentiDegrees = epoch.courseCentiDegrees;
    fix.flags |= gps::kGpsFlagCourseValid;
  }
  if (nmeaFresh) {
    fix.satellites = nmea.satellites;
    fix.fixQuality = nmea.fixQuality;
    if (nmea.hdopX100 != gps::kUnavailableUnsigned16) {
      fix.hdopX100 = nmea.hdopX100;
      fix.flags |= gps::kGpsFlagHdopValid;
    }
    if (nmea.utcValid) {
      fix.utcMilliseconds = nmea.utcMilliseconds;
      fix.flags |= gps::kGpsFlagUtcValid;
    }
  }
  if (storage_.enqueueGpsFix(computerId_, bootId_, sequence_++, fix)) ++recordsQueued_;
  else ++recordsDropped_;
  lastSolutionUs_ = now;
  lastUbxSolutionUs_ = now;
  staleReported_ = false;
  reportFixState(positionValid, fix.fixQuality, fix.satellites);
}

void GpsService::publishNmea(const gps::NmeaSnapshot& nmea) noexcept {
  const std::uint64_t now = static_cast<std::uint64_t>(esp_timer_get_time());
  storage::GpsFixPayload fix{};
  fix.pcMonotonicUs = nmea.receivedAtPcMonotonicUs;
  fix.ageMilliseconds = ageMilliseconds(now, nmea.receivedAtPcMonotonicUs);
  fix.fixQuality = nmea.fixQuality;
  fix.satellites = nmea.satellites;
  if (nmea.positionValid) {
    fix.latitudeE7 = nmea.latitudeE7;
    fix.longitudeE7 = nmea.longitudeE7;
    fix.flags |= gps::kGpsFlagReceiverFixValid | gps::kGpsFlagPositionValid;
  }
  if (nmea.speedValid) {
    fix.speedMillimetersPerSecond = nmea.speedMillimetersPerSecond;
    fix.flags |= gps::kGpsFlagSpeedValid;
  }
  if (nmea.courseValid) {
    fix.courseCentiDegrees = nmea.courseCentiDegrees;
    fix.flags |= gps::kGpsFlagCourseValid;
  }
  if (nmea.utcValid) {
    fix.utcMilliseconds = nmea.utcMilliseconds;
    fix.flags |= gps::kGpsFlagUtcValid;
  }
  if (nmea.hdopX100 != gps::kUnavailableUnsigned16) {
    fix.hdopX100 = nmea.hdopX100;
    fix.flags |= gps::kGpsFlagHdopValid;
  }
  if (storage_.enqueueGpsFix(computerId_, bootId_, sequence_++, fix)) ++recordsQueued_;
  else ++recordsDropped_;
  lastSolutionUs_ = now;
  staleReported_ = false;
  reportFixState(nmea.positionValid, fix.fixQuality, fix.satellites);
}

void GpsService::run() noexcept {
  configureReceiver();
  ESP_LOGI(kTag, "GPS: READY target_rate_hz=1 rx_gpio=0 tx_gpio=1 baud=9600");
  storage_.enqueue(DiagnosticLogLevel::Info, "gps_ready",
                   "target_rate_hz=1 rx_gpio=0 tx_gpio=1 baud=9600 rx_buffer_bytes=2048");
  const std::uint64_t startedAtUs = static_cast<std::uint64_t>(esp_timer_get_time());
  std::uint8_t bytes[256]{};
  for (;;) {
    const std::uint64_t receivedAt = static_cast<std::uint64_t>(esp_timer_get_time());
    const std::size_t count = uart_.readAvailable(bytes, sizeof(bytes));
    if (count > 0) {
      bytes_.fetch_add(count);
      for (std::size_t index = 0; index < count; ++index) {
        ubx_.feed(bytes[index], receivedAt);
        nmea_.feed(bytes[index], receivedAt);
      }
    }
    gps::NavigationEpoch epoch{};
    bool publishedUbx = false;
    while (ubx_.take(epoch)) {
      publishUbx(epoch, nmea_.snapshot());
      publishedUbx = true;
    }
    const gps::NmeaSnapshot snapshot = nmea_.snapshot();
    if (!publishedUbx && snapshot.revision != lastNmeaRevision_) {
      lastNmeaRevision_ = snapshot.revision;
      // NMEA keeps capture useful while a receiver ignores UBX configuration.
      const std::uint64_t now = static_cast<std::uint64_t>(esp_timer_get_time());
      if (lastUbxSolutionUs_ == 0 || now - lastUbxSolutionUs_ > kNmeaFreshnessUs)
        publishNmea(snapshot);
    }
    const std::uint64_t now = static_cast<std::uint64_t>(esp_timer_get_time());
    const bool stale = lastSolutionUs_ == 0 ? now - startedAtUs > kStaleAfterUs
                                           : now - lastSolutionUs_ > kStaleAfterUs;
    if (!staleReported_ && stale) {
      ESP_LOGW(kTag, "GPS: STALE");
      staleReported_ = true;
    }
    const gps::DecoderStats currentDecoder = ubx_.stats();
    coherentEpochs_.store(currentDecoder.coherentEpochs);
    checksumFailures_.store(currentDecoder.checksumFailures + nmea_.checksumFailures());
    if (now - lastDiagnosticUs_ >= kDiagnosticIntervalUs) {
      lastDiagnosticUs_ = now;
      const gps::DecoderStats decoder = currentDecoder;
      char details[320]{};
      std::snprintf(details, sizeof(details),
                    "bytes=%llu ubx_frames=%lu ubx_coherent_epochs=%lu "
                    "ubx_checksum_failures=%lu ubx_oversize=%lu nmea_sentences=%lu "
                    "nmea_checksum_failures=%lu cfg_ack_mask=0x%03x cfg_nak_mask=0x%03x "
                    "measurement_rate_ms=%u navigation_rate=%u queued=%lu dropped=%lu",
                    static_cast<unsigned long long>(bytes_.load()),
                    static_cast<unsigned long>(decoder.frames),
                    static_cast<unsigned long>(decoder.coherentEpochs),
                    static_cast<unsigned long>(decoder.checksumFailures),
                    static_cast<unsigned long>(decoder.oversizeFrames),
                    static_cast<unsigned long>(nmea_.validSentences()),
                    static_cast<unsigned long>(nmea_.checksumFailures()),
                    static_cast<unsigned>(ubx_.acknowledgedConfigurationMask()),
                    static_cast<unsigned>(ubx_.rejectedConfigurationMask()),
                    static_cast<unsigned>(ubx_.measurementRateMs()),
                    static_cast<unsigned>(ubx_.navigationRate()),
                    static_cast<unsigned long>(recordsQueued_.load()),
                    static_cast<unsigned long>(recordsDropped_.load()));
      storage_.enqueue(DiagnosticLogLevel::Info, "gps_health", details);
    }
    if (count == 0) vTaskDelay(kPollInterval);
  }
}

}  // namespace remus::computer
