#pragma once

#include <cstdint>

namespace remus::platform::esp32 {

constexpr bool isCanonicalMpu6050Identity(std::uint8_t identity) noexcept {
  return (identity & 0x7E) == 0x68;
}

// Some fielded GY-521-compatible modules do not expose the canonical identity.
// They are accepted only after the complete operating register profile can be
// written and read back; identity alone never proves compatibility.
constexpr bool hasMpu6050RegisterProfile(std::uint8_t powerManagement,
                                         std::uint8_t dlpfConfig,
                                         std::uint8_t sampleRateDivider,
                                         std::uint8_t accelConfig,
                                         std::uint8_t gyroConfig,
                                         std::uint8_t expectedDivider) noexcept {
  return (powerManagement & 0x40) == 0 && (dlpfConfig & 0x07) == 0x03 &&
         sampleRateDivider == expectedDivider && (accelConfig & 0x18) == 0x10 &&
         (gyroConfig & 0x18) == 0x08;
}

}  // namespace remus::platform::esp32
