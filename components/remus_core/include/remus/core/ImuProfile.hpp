#pragma once

#include <cstdint>

#include "remus/core/Status.hpp"

namespace remus::core {

enum class AccelRangeG : std::uint8_t { G2 = 2, G4 = 4, G8 = 8, G16 = 16 };
enum class GyroRangeDps : std::uint16_t {
  Dps250 = 250,
  Dps500 = 500,
  Dps1000 = 1000,
  Dps2000 = 2000,
};

struct ImuProfile {
  AccelRangeG accelRange{AccelRangeG::G8};
  GyroRangeDps gyroRange{GyroRangeDps::Dps1000};
  std::uint8_t dlpfCode{3};
  std::uint16_t sampleRateHz{200};

  constexpr std::uint16_t gyroDlpfBandwidthHz() const noexcept {
    constexpr std::uint16_t bandwidths[]{256, 188, 98, 42, 20, 10, 5};
    return dlpfCode < 7 ? bandwidths[dlpfCode] : 0;
  }
};

struct EffectiveImuProfile {
  ImuProfile requested{};
  std::uint8_t whoAmI{0};
  std::uint8_t i2cAddress{0};
  std::uint8_t accelRegister{0};
  std::uint8_t gyroRegister{0};
  std::uint8_t filterRegister{0};
  std::uint8_t sampleDivider{0};
  std::uint16_t accelLsbPerG{0};
  std::uint16_t gyroLsbPerDpsX10{0};
  bool verified{false};
};

constexpr std::uint16_t accelLsbPerG(AccelRangeG range) noexcept {
  switch (range) {
    case AccelRangeG::G2:
      return 16384;
    case AccelRangeG::G4:
      return 8192;
    case AccelRangeG::G8:
      return 4096;
    case AccelRangeG::G16:
      return 2048;
  }
  return 0;
}

constexpr std::uint16_t gyroLsbPerDpsX10(GyroRangeDps range) noexcept {
  switch (range) {
    case GyroRangeDps::Dps250:
      return 1310;
    case GyroRangeDps::Dps500:
      return 655;
    case GyroRangeDps::Dps1000:
      return 328;
    case GyroRangeDps::Dps2000:
      return 164;
  }
  return 0;
}

constexpr std::uint8_t accelRegisterBits(AccelRangeG range) noexcept {
  switch (range) {
    case AccelRangeG::G2:
      return 0x00;
    case AccelRangeG::G4:
      return 0x08;
    case AccelRangeG::G8:
      return 0x10;
    case AccelRangeG::G16:
      return 0x18;
  }
  return 0xFF;
}

constexpr std::uint8_t gyroRegisterBits(GyroRangeDps range) noexcept {
  switch (range) {
    case GyroRangeDps::Dps250:
      return 0x00;
    case GyroRangeDps::Dps500:
      return 0x08;
    case GyroRangeDps::Dps1000:
      return 0x10;
    case GyroRangeDps::Dps2000:
      return 0x18;
  }
  return 0xFF;
}

constexpr Status validateImuProfile(const ImuProfile& profile) noexcept {
  return accelLsbPerG(profile.accelRange) != 0 && gyroLsbPerDpsX10(profile.gyroRange) != 0 &&
                 profile.dlpfCode > 0 && profile.dlpfCode < 7 && profile.sampleRateHz != 0 &&
                 profile.sampleRateHz <= 1000 && 1000U % profile.sampleRateHz == 0
             ? Status::success()
             : Status::failure(Error::InvalidArgument);
}

constexpr ImuProfile computerImuProfile() noexcept {
  return {AccelRangeG::G8, GyroRangeDps::Dps1000, 3, 200};
}

constexpr ImuProfile bladeImuProfile() noexcept {
  return {AccelRangeG::G16, GyroRangeDps::Dps2000, 3, 200};
}

}  // namespace remus::core
