#include "GpsService.hpp"

#include <algorithm>
#include <cstdio>
#include <limits>

#include "esp_log.h"
#include "esp_timer.h"
#include "remus/gps/Neo6mProfile.hpp"

namespace remus::computer {
namespace {
constexpr char kTag[] = "gps";
constexpr std::uint32_t kBaudRate = 9600;
constexpr std::uint64_t kNmeaFreshnessUs = 2500000;
constexpr std::uint64_t kStaleAfterUs = 3000000;
constexpr std::uint64_t kConfigurationTimeoutUs = 2000000;
constexpr std::uint64_t kConfigurationRetryUs = 2000000;
constexpr std::uint16_t kRequiredBaseConfigurationMask = 0x0007;
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
          decoderOverflows_.load(), fixTransitions_.load(),
          static_cast<gps::GpsOperatingMode>(activeMode_.load()),
          configurationFault_.load()};
}

void GpsService::requestOperatingMode(gps::GpsOperatingMode mode) noexcept {
  requestedMode_.store(static_cast<std::uint8_t>(mode), std::memory_order_relaxed);
  modeRequestRevision_.fetch_add(1, std::memory_order_release);
}

void GpsService::taskEntry(void* context) noexcept {
  static_cast<GpsService*>(context)->run();
}

bool GpsService::sendUbx(std::uint8_t messageClass, std::uint8_t messageId,
                         const std::uint8_t* payload,
                         std::uint16_t payloadLength) noexcept {
  if (payloadLength > 44 || (payloadLength > 0 && payload == nullptr)) return false;
  std::uint8_t frame[52]{};
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

bool GpsService::configureReceiver(gps::GpsOperatingMode mode) noexcept {
  const gps::Neo6mProfile profile = gps::profileFor(mode);
  ubx_.resetConfigurationEvidence();
  bool allSent = true;

  // PSM must be disabled before changing rate or power parameters.
  const std::uint8_t continuousPayload[]{0x08, 0x00};
  allSent = sendUbx(0x06, 0x11, continuousPayload, sizeof(continuousPayload)) && allSent;
  vTaskDelay(pdMS_TO_TICKS(20));
  struct MessageRate {
    const char* name;
    std::uint8_t messageClass;
    std::uint8_t messageId;
    std::uint8_t rate;
  };
  const MessageRate messages[] = {
      {"GGA", 0xF0, 0x00, profile.nmeaMessageRate},
      {"GSA", 0xF0, 0x02, 0},
      {"GSV", 0xF0, 0x03, profile.nmeaMessageRate},
      {"RMC", 0xF0, 0x04, profile.nmeaMessageRate},
      {"GLL", 0xF0, 0x01, 0}, {"VTG", 0xF0, 0x05, 0},
      {"NAV_POSLLH", 0x01, 0x02, profile.ubxNavigationMessageRate},
      {"NAV_VELNED", 0x01, 0x12, profile.ubxNavigationMessageRate},
      {"NAV_STATUS", 0x01, 0x03, profile.ubxNavigationMessageRate},
  };
  for (const auto& message : messages) {
    const std::uint8_t payload[]{message.messageClass, message.messageId, message.rate};
    const bool sent = sendUbx(0x06, 0x01, payload, sizeof(payload));
    allSent = sent && allSent;
    if (!sent) {
      ESP_LOGW(kTag, "GPS: CFG_FAILED name=%s class=0x%02x id=0x%02x rate=%u",
               message.name, static_cast<unsigned>(message.messageClass),
               static_cast<unsigned>(message.messageId), static_cast<unsigned>(message.rate));
    }
    if (!sent && storage_.isReady()) {
      char details[128]{};
      std::snprintf(details, sizeof(details),
                    "name=%s class=0x%02x id=0x%02x rate=%u", message.name,
                    static_cast<unsigned>(message.messageClass),
                    static_cast<unsigned>(message.messageId),
                    static_cast<unsigned>(message.rate));
      storage_.enqueue(DiagnosticLogLevel::Warning, "gps_config_message_failed", details);
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
  const std::uint8_t ratePayload[]{
      static_cast<std::uint8_t>(profile.measurementRateMs),
      static_cast<std::uint8_t>(profile.measurementRateMs >> 8U),
      static_cast<std::uint8_t>(profile.navigationRate), 0x00,
      0x01, 0x00,  // align to GPS time
  };
  const bool rateSent = sendUbx(0x06, 0x08, ratePayload, sizeof(ratePayload));
  allSent = rateSent && allSent;
  if (!rateSent)
    ESP_LOGW(kTag, "GPS: CFG_FAILED name=RATE measurement_ms=%u navigation_rate=1",
             static_cast<unsigned>(profile.measurementRateMs));
  if (!rateSent && storage_.isReady()) {
    char details[128]{};
    std::snprintf(details, sizeof(details),
                  "name=RATE measurement_ms=%u navigation_rate=1 time_reference=GPS",
                  static_cast<unsigned>(profile.measurementRateMs));
    storage_.enqueue(DiagnosticLogLevel::Warning, "gps_config_rate_failed", details);
  }
  vTaskDelay(pdMS_TO_TICKS(20));

  if (profile.powerSave) {
    // Some NEO-6M-compatible firmware rejects CFG-PM2 while accepting the
    // documented CFG-RXM power-save selector. Preserve its internal PM2
    // defaults instead of retrying an optional advanced profile indefinitely.
    const std::uint8_t powerSavePayload[]{0x08, 0x01};
    allSent = sendUbx(0x06, 0x11, powerSavePayload, sizeof(powerSavePayload)) && allSent;
    vTaskDelay(pdMS_TO_TICKS(20));
  }
  const bool pollSent = sendUbx(0x06, 0x08, nullptr, 0);
  allSent = pollSent && allSent;
  if (!pollSent) ESP_LOGW(kTag, "GPS: CFG_FAILED name=RATE_POLL");
  return allSent;
}

void GpsService::reportModeConfiguration(bool applied, const char* reason) noexcept {
  const char* mode = gps::operatingModeName(configuringMode_);
  ESP_LOG_LEVEL(applied ? ESP_LOG_INFO : ESP_LOG_WARN, kTag,
                "GPS: MODE mode=%s status=%s reason=%s", mode,
                applied ? "APPLIED" : "FAILED", reason);
  if (!storage_.isReady()) return;
  char details[160]{};
  std::snprintf(details, sizeof(details), "mode=%s status=%s reason=%s", mode,
                applied ? "applied" : "failed", reason);
  storage_.enqueue(applied ? DiagnosticLogLevel::Info : DiagnosticLogLevel::Warning,
                   "gps_mode_configuration", details);
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
  lastSolutionUs_ = now;
  lastUbxSolutionUs_ = now;
  staleReported_ = false;
  reportFixState(gps::hasQualifiedFixReport(epoch.receiverFixValid, positionValid,
                                            fix.fixQuality, fix.satellites),
                 fix.fixQuality, fix.satellites);
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
  lastSolutionUs_ = now;
  staleReported_ = false;
  reportFixState(nmea.positionValid, fix.fixQuality, fix.satellites);
}

void GpsService::run() noexcept {
  configuringMode_ = gps::kDefaultGpsOperatingMode;
  const std::uint64_t initialConfigurationUs =
      static_cast<std::uint64_t>(esp_timer_get_time());
  if (configureReceiver(configuringMode_)) {
    configurationPending_ = true;
    configurationDeadlineUs_ = initialConfigurationUs + kConfigurationTimeoutUs;
  } else {
    modeController_.configurationFailed();
    nextConfigurationAttemptUs_ = initialConfigurationUs + kConfigurationRetryUs;
    reportModeConfiguration(false, "uart_tx");
  }
  ESP_LOGI(kTag,
           "GPS: READY requested_mode=normal target_rate_hz=1 "
           "rx_gpio=0 tx_gpio=1 baud=9600");
  storage_.enqueue(DiagnosticLogLevel::Info, "gps_ready",
                   "requested_mode=normal target_rate_hz=1 rx_gpio=0 "
                   "tx_gpio=1 baud=9600 rx_buffer_bytes=2048");
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
    const std::uint32_t requestRevision =
        modeRequestRevision_.load(std::memory_order_acquire);
    if (requestRevision != handledModeRequestRevision_) {
      handledModeRequestRevision_ = requestRevision;
      const std::uint8_t requested = requestedMode_.load(std::memory_order_relaxed);
      if (requested <= static_cast<std::uint8_t>(gps::GpsOperatingMode::DynamicArmed)) {
        modeController_.request(static_cast<gps::GpsOperatingMode>(requested), now);
        configurationPending_ = false;
        nextConfigurationAttemptUs_ = now;
      }
    }

    modeController_.tick(now);
    const SdDiagnosticLoggerStats storageStats = storage_.stats();
    modeController_.observeStorage(storageStats.gpsDropped, storageStats.gpsQueueDepth,
                                   storageStats.rmsWriteFailures, now);

    if (configurationPending_ && configuringMode_ != modeController_.requestedMode()) {
      configurationPending_ = false;
      nextConfigurationAttemptUs_ = now;
      reportModeConfiguration(false, "superseded");
    }

    if (configurationPending_) {
      const gps::Neo6mProfile profile = gps::profileFor(configuringMode_);
      const std::uint16_t requiredMask = kRequiredBaseConfigurationMask;
      const std::uint16_t acknowledged = ubx_.acknowledgedConfigurationMask();
      const std::uint16_t rejected = ubx_.rejectedConfigurationMask();
      const bool rateConfirmed = ubx_.measurementRateMs() == profile.measurementRateMs &&
                                 ubx_.navigationRate() == profile.navigationRate;
      if ((rejected & requiredMask) != 0) {
        configurationPending_ = false;
        modeConfigured_ = false;
        modeController_.configurationFailed();
        configurationFault_.store(modeController_.configurationFault());
        nextConfigurationAttemptUs_ = now + kConfigurationRetryUs;
        reportModeConfiguration(false, "receiver_nak");
      } else if ((acknowledged & requiredMask) == requiredMask && rateConfirmed) {
        configurationPending_ = false;
        modeConfigured_ = true;
        if (modeController_.configurationApplied(configuringMode_, now)) {
          activeMode_.store(static_cast<std::uint8_t>(configuringMode_));
          configurationFault_.store(modeController_.configurationFault());
          reportModeConfiguration(true, "ack_and_rate_readback");
        } else {
          modeConfigured_ = false;
          nextConfigurationAttemptUs_ = now;
          reportModeConfiguration(false, "superseded");
        }
      } else if (now >= configurationDeadlineUs_) {
        configurationPending_ = false;
        modeConfigured_ = false;
        modeController_.configurationFailed();
        configurationFault_.store(modeController_.configurationFault());
        nextConfigurationAttemptUs_ = now + kConfigurationRetryUs;
        reportModeConfiguration(false, "timeout");
      }
    }

    if (!configurationPending_ &&
        (!modeConfigured_ || modeController_.configurationRequired()) &&
        now >= nextConfigurationAttemptUs_) {
      configuringMode_ = modeController_.requestedMode();
      if (configureReceiver(configuringMode_)) {
        configurationPending_ = true;
        configurationDeadlineUs_ =
            static_cast<std::uint64_t>(esp_timer_get_time()) + kConfigurationTimeoutUs;
      } else {
        modeController_.configurationFailed();
        configurationFault_.store(modeController_.configurationFault());
        nextConfigurationAttemptUs_ = now + kConfigurationRetryUs;
        reportModeConfiguration(false, "uart_tx");
      }
    }

    const bool stale = lastSolutionUs_ == 0 ? now - startedAtUs > kStaleAfterUs
                                           : now - lastSolutionUs_ > kStaleAfterUs;
    if (stale && lastSolutionUs_ != 0) modeController_.observeNavigationStale(now);
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
    decoderOverflows_.store(currentDecoder.overwrittenEpochs);
    if (count == 0) vTaskDelay(kPollInterval);
  }
}

}  // namespace remus::computer
