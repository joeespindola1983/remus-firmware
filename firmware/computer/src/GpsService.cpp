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
  if (storage_.isRecording() && (computerId == 0 || bootId == 0))
    return Status::failure(Error::InvalidArgument);
  computerId_ = computerId;
  bootId_ = bootId;
  ESP_LOGI(kTag,
           "GPS: START uart=1 rx_gpio=0 tx_gpio=1 baud=9600 rx_buffer=2048 "
           "sd_ready=%s rms_recording=%s",
           storage_.isReady() ? "yes" : "no", storage_.isRecording() ? "yes" : "no");
  if (storage_.isReady())
    storage_.enqueue(DiagnosticLogLevel::Info, "gps_start",
                     "uart=1 rx_gpio=0 tx_gpio=1 baud=9600 rx_buffer_bytes=2048");
  const Status uartStatus = uart_.start(kBaudRate);
  if (!uartStatus.ok()) {
    ESP_LOGE(kTag, "GPS: UART_ERROR error=%u detail=%lu",
             static_cast<unsigned>(uartStatus.error),
             static_cast<unsigned long>(uartStatus.detail));
    return uartStatus;
  }
  task_ = xTaskCreateStatic(taskEntry, "gps", kTaskStackBytes, this, 5,
                            taskStack_, &taskBuffer_);
  return task_ != nullptr ? Status::success() : Status::failure(Error::NotReady);
}

GpsServiceStats GpsService::stats() const noexcept {
  return {bytes_.load(), recordsQueued_.load(), recordsDropped_.load(),
          recordsSkippedNoStorage_.load(), coherentEpochs_.load(), checksumFailures_.load(),
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
  struct MessageRate {
    const char* name;
    std::uint8_t messageClass;
    std::uint8_t messageId;
    std::uint8_t rate;
  };
  constexpr MessageRate messages[] = {
      {"GGA", 0xF0, 0x00, 1}, {"GSA", 0xF0, 0x02, 0},
      {"GSV", 0xF0, 0x03, 5}, {"RMC", 0xF0, 0x04, 1},
      {"GLL", 0xF0, 0x01, 0}, {"VTG", 0xF0, 0x05, 0},
      {"NAV_POSLLH", 0x01, 0x02, 1}, {"NAV_VELNED", 0x01, 0x12, 1},
      {"NAV_STATUS", 0x01, 0x03, 1},
  };
  for (const auto& message : messages) {
    const std::uint8_t payload[]{message.messageClass, message.messageId, message.rate};
    const bool sent = sendUbx(0x06, 0x01, payload, sizeof(payload));
    ESP_LOGI(kTag, "GPS: CFG name=%s class=0x%02x id=0x%02x rate=%u tx=%s",
             message.name, static_cast<unsigned>(message.messageClass),
             static_cast<unsigned>(message.messageId), static_cast<unsigned>(message.rate),
             sent ? "OK" : "FAILED");
    if (storage_.isReady()) {
      char details[128]{};
      std::snprintf(details, sizeof(details),
                    "name=%s class=0x%02x id=0x%02x rate=%u tx=%s", message.name,
                    static_cast<unsigned>(message.messageClass),
                    static_cast<unsigned>(message.messageId),
                    static_cast<unsigned>(message.rate), sent ? "OK" : "FAILED");
      storage_.enqueue(sent ? DiagnosticLogLevel::Info : DiagnosticLogLevel::Warning,
                       "gps_config_message", details);
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
  const std::uint8_t ratePayload[]{
      static_cast<std::uint8_t>(kMeasurementRateMs),
      static_cast<std::uint8_t>(kMeasurementRateMs >> 8U),
      0x01, 0x00,  // one navigation solution per measurement
      0x01, 0x00,  // align to GPS time
  };
  const bool rateSent = sendUbx(0x06, 0x08, ratePayload, sizeof(ratePayload));
  ESP_LOGI(kTag, "GPS: CFG name=RATE measurement_ms=%u navigation_rate=1 tx=%s",
           static_cast<unsigned>(kMeasurementRateMs), rateSent ? "OK" : "FAILED");
  if (storage_.isReady()) {
    char details[128]{};
    std::snprintf(details, sizeof(details),
                  "name=RATE measurement_ms=%u navigation_rate=1 time_reference=GPS tx=%s",
                  static_cast<unsigned>(kMeasurementRateMs), rateSent ? "OK" : "FAILED");
    storage_.enqueue(rateSent ? DiagnosticLogLevel::Info : DiagnosticLogLevel::Warning,
                     "gps_config_rate", details);
  }
  vTaskDelay(pdMS_TO_TICKS(20));
  const bool pollSent = sendUbx(0x06, 0x08, nullptr, 0);
  ESP_LOGI(kTag, "GPS: CFG name=RATE_POLL tx=%s", pollSent ? "OK" : "FAILED");
}

void GpsService::reportFixState(bool valid, std::uint8_t fixQuality,
                                std::uint8_t satellites) noexcept {
  if (fixStateKnown_ && valid == fixValid_) return;
  fixStateKnown_ = true;
  fixValid_ = valid;
  ++fixTransitions_;
  if (storage_.isReady()) {
    char details[96]{};
    std::snprintf(details, sizeof(details), "valid=%s quality=%u satellites=%u",
                  valid ? "yes" : "no", static_cast<unsigned>(fixQuality),
                  static_cast<unsigned>(satellites));
    storage_.enqueue(valid ? DiagnosticLogLevel::Info : DiagnosticLogLevel::Warning,
                     valid ? "gps_fix_acquired" : "gps_fix_lost", details);
  }
  if (valid)
    ESP_LOGI(kTag, "GPS: FIX quality=%u satellites=%u", static_cast<unsigned>(fixQuality),
             static_cast<unsigned>(satellites));
  else
    ESP_LOGW(kTag, "GPS: NO_FIX quality=%u satellites=%u", static_cast<unsigned>(fixQuality),
             static_cast<unsigned>(satellites));
}

void GpsService::persistFix(const storage::GpsFixPayload& fix) noexcept {
  if (!storage_.isRecording()) {
    ++recordsSkippedNoStorage_;
    return;
  }
  if (storage_.enqueueGpsFix(computerId_, bootId_, sequence_++, fix)) ++recordsQueued_;
  else ++recordsDropped_;
}

void GpsService::logSolution(const char* source, const storage::GpsFixPayload& fix,
                             std::uint32_t gpsTimeOfWeekMs) noexcept {
  if (!storage_.isReady()) return;
  char details[384]{};
  std::snprintf(details, sizeof(details),
                "source=%s itow_ms=%lu pc_mono_us=%llu position_valid=%s "
                "latitude_e7=%ld longitude_e7=%ld speed_mm_s=%lu course_cdeg=%u "
                "satellites=%u fix_quality=%u hdop_x100=%u flags=0x%04x "
                "utc_ms=%lld age_ms=%lu rms_recording=%s",
                source, static_cast<unsigned long>(gpsTimeOfWeekMs),
                static_cast<unsigned long long>(fix.pcMonotonicUs),
                (fix.flags & gps::kGpsFlagPositionValid) != 0 ? "yes" : "no",
                static_cast<long>(fix.latitudeE7), static_cast<long>(fix.longitudeE7),
                static_cast<unsigned long>(fix.speedMillimetersPerSecond),
                static_cast<unsigned>(fix.courseCentiDegrees),
                static_cast<unsigned>(fix.satellites), static_cast<unsigned>(fix.fixQuality),
                static_cast<unsigned>(fix.hdopX100), static_cast<unsigned>(fix.flags),
                static_cast<long long>(fix.utcMilliseconds),
                static_cast<unsigned long>(fix.ageMilliseconds),
                storage_.isRecording() ? "yes" : "no");
  storage_.enqueue(DiagnosticLogLevel::Info, "gps_solution", details);
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
  persistFix(fix);
  logSolution("UBX", fix, epoch.gpsTimeOfWeekMs);
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
  persistFix(fix);
  logSolution("NMEA", fix, UINT32_MAX);
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
      if (storage_.isReady())
        storage_.enqueue(DiagnosticLogLevel::Warning, "gps_stale",
                         "no_navigation_solution_for_ms=3000");
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
                    "measurement_rate_ms=%u navigation_rate=%u queued=%lu dropped=%lu "
                    "skipped_no_storage=%lu fix_state=%s satellites=%u fix_quality=%u "
                    "hdop_x100=%u",
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
                    static_cast<unsigned long>(recordsDropped_.load()),
                    static_cast<unsigned long>(recordsSkippedNoStorage_.load()),
                    fixStateKnown_ ? (fixValid_ ? "FIX" : "NO_FIX") : "UNKNOWN",
                    static_cast<unsigned>(snapshot.satellites),
                    static_cast<unsigned>(snapshot.fixQuality),
                    static_cast<unsigned>(snapshot.hdopX100));
      ESP_LOGI(kTag, "GPS: HEALTH %s", details);
      if (storage_.isReady())
        storage_.enqueue(DiagnosticLogLevel::Info, "gps_health", details);
    }
    if (count == 0) vTaskDelay(kPollInterval);
  }
}

}  // namespace remus::computer
