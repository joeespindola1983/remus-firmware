#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "remus/core/Status.hpp"
#include "remus/core/Types.hpp"
#include "remus/hal/ICrypto.hpp"

namespace remus::link {
struct DerivedKeys {
  std::array<std::uint8_t, 32> pairAuthKey{};
  std::array<std::uint8_t, 16> pairLmk{};
};
// All derivations require an exact 32-byte provisioned secret. Output remains
// caller-owned; functions perform no logging or allocation.
Status deriveGroupAuthKey(hal::ICrypto&, const std::uint8_t*, std::size_t, NetworkId,
                          std::array<std::uint8_t, 32>&) noexcept;
Status derivePmk(hal::ICrypto&, const std::uint8_t*, std::size_t, NetworkId,
                 std::array<std::uint8_t, 16>&) noexcept;
Status derivePairKeys(hal::ICrypto&, const std::uint8_t*, std::size_t, DeviceSerialNumber,
                      DeviceSerialNumber, DerivedKeys&) noexcept;
}  // namespace remus::link
