#pragma once

#include <cstdint>

#include "remus/hal/IImu.hpp"

namespace remus::acquisition {

constexpr std::uint16_t kImuFlagAccelNearRail = 1U << 0U;
constexpr std::uint16_t kImuFlagGyroNearRail = 1U << 1U;
constexpr std::int32_t kImuNearRailThreshold = 32112;

constexpr bool isImuAxisNearRail(std::int16_t value) noexcept {
  return value >= kImuNearRailThreshold || value <= -kImuNearRailThreshold;
}

constexpr std::uint16_t imuQualityFlags(const hal::ImuRaw& raw) noexcept {
  std::uint16_t flags = 0;
  if (isImuAxisNearRail(raw.ax) || isImuAxisNearRail(raw.ay) || isImuAxisNearRail(raw.az))
    flags |= kImuFlagAccelNearRail;
  if (isImuAxisNearRail(raw.gx) || isImuAxisNearRail(raw.gy) || isImuAxisNearRail(raw.gz))
    flags |= kImuFlagGyroNearRail;
  return flags;
}

}  // namespace remus::acquisition
