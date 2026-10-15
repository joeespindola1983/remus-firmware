#pragma once

#include <cstdint>

#include "remus/gps/Types.hpp"

namespace remus::gps {

enum class GpsOperatingMode : std::uint8_t {
  NormalPowerSaving,
  Normal,
  DynamicArmed,
};

struct Neo6mProfile {
  std::uint16_t measurementRateMs;
  std::uint16_t navigationRate;
  std::uint8_t nmeaMessageRate;
  std::uint8_t ubxNavigationMessageRate;
  bool powerSave;
};

inline constexpr GpsOperatingMode kDefaultGpsOperatingMode =
    GpsOperatingMode::Normal;

constexpr Neo6mProfile profileFor(GpsOperatingMode mode) noexcept {
  switch (mode) {
    case GpsOperatingMode::DynamicArmed:
      // UBX navigation runs at 5 Hz; NMEA divisors keep GGA/RMC at 1 Hz.
      return {200, 1, 5, 1, false};
    case GpsOperatingMode::Normal:
      return {1000, 1, 1, 1, false};
    case GpsOperatingMode::NormalPowerSaving:
      return {1000, 1, 1, 1, true};
  }
  return {1000, 1, 1, 1, false};
}

constexpr const char* operatingModeName(GpsOperatingMode mode) noexcept {
  switch (mode) {
    case GpsOperatingMode::NormalPowerSaving: return "normalPowerSaving";
    case GpsOperatingMode::Normal: return "normal";
    case GpsOperatingMode::DynamicArmed: return "dynamicArmed";
  }
  return "normal";
}

class GpsModeController {
 public:
  static constexpr std::uint64_t kDynamicMaximumDurationUs = 180000000U;
  static constexpr std::uint32_t kStoragePressureDepth = 12;
  static constexpr std::uint8_t kMaximumConfigurationFailures = 3;

  GpsOperatingMode requestedMode() const noexcept { return requestedMode_; }
  GpsOperatingMode activeMode() const noexcept { return activeMode_; }
  bool configurationRequired() const noexcept { return requestedMode_ != activeMode_; }
  bool configurationFault() const noexcept { return configurationFault_; }
  bool storagePressureDetected() const noexcept { return storagePressureDetected_; }
  bool navigationFallbackDetected() const noexcept { return navigationFallbackDetected_; }

  void request(GpsOperatingMode mode, std::uint64_t nowUs) noexcept {
    requestedMode_ = mode;
    (void)nowUs;
    configurationFailures_ = 0;
    configurationFault_ = false;
  }

  bool configurationApplied(GpsOperatingMode mode, std::uint64_t nowUs) noexcept {
    if (mode != requestedMode_) return false;
    activeMode_ = mode;
    configuredAtUs_ = nowUs;
    configurationFailures_ = 0;
    return true;
  }

  void configurationFailed() noexcept {
    if (configurationFailures_ < UINT8_MAX) ++configurationFailures_;
    if (configurationFailures_ >= kMaximumConfigurationFailures) {
      // Continuous 1 Hz is the evidence-preserving fallback when receiver
      // power configuration cannot be confirmed.
      requestedMode_ = GpsOperatingMode::Normal;
      configurationFault_ = true;
    }
  }

  void tick(std::uint64_t nowUs) noexcept {
    if (activeMode_ == GpsOperatingMode::DynamicArmed && nowUs >= configuredAtUs_ &&
        nowUs - configuredAtUs_ >= kDynamicMaximumDurationUs) {
      requestedMode_ = GpsOperatingMode::Normal;
    }
  }

  void observeStorage(std::uint32_t dropped, std::uint32_t queueDepth,
                      std::uint32_t writeFailures, std::uint64_t nowUs) noexcept {
    const bool newDrop = storageObserved_ && dropped > lastStorageDropped_;
    const bool newWriteFailure =
        storageObserved_ && writeFailures > lastStorageWriteFailures_;
    storageObserved_ = true;
    lastStorageDropped_ = dropped;
    lastStorageWriteFailures_ = writeFailures;
    if (!newDrop && !newWriteFailure && queueDepth < kStoragePressureDepth) return;
    storagePressureDetected_ = true;
    if (requestedMode_ == GpsOperatingMode::DynamicArmed)
      request(GpsOperatingMode::Normal, nowUs);
  }

  void observeNavigationStale(std::uint64_t nowUs) noexcept {
    if (requestedMode_ != GpsOperatingMode::NormalPowerSaving) return;
    navigationFallbackDetected_ = true;
    request(GpsOperatingMode::Normal, nowUs);
  }

 private:
  GpsOperatingMode requestedMode_{kDefaultGpsOperatingMode};
  GpsOperatingMode activeMode_{kDefaultGpsOperatingMode};
  std::uint64_t configuredAtUs_{0};
  std::uint32_t lastStorageDropped_{0};
  std::uint32_t lastStorageWriteFailures_{0};
  std::uint8_t configurationFailures_{0};
  bool storageObserved_{false};
  bool configurationFault_{false};
  bool storagePressureDetected_{false};
  bool navigationFallbackDetected_{false};
};

constexpr bool hasQualifiedFixReport(bool receiverFixValid, bool positionValid,
                                     std::uint8_t fixQuality,
                                     std::uint8_t satellites) noexcept {
  return receiverFixValid && positionValid && fixQuality > 0 &&
         satellites != kUnavailableUnsigned8;
}

}  // namespace remus::gps
