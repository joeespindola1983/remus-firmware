#pragma once

#include "remus/hal/ICrypto.hpp"

namespace remus::platform::esp32 {
class MbedTlsCrypto final : public hal::ICrypto {
 public:
  Status hkdfSha256(const std::uint8_t*, std::size_t, const std::uint8_t*, std::size_t,
                    const std::uint8_t*, std::size_t, std::uint8_t*, std::size_t) noexcept override;
  Status hmacSha256(const std::uint8_t*, std::size_t, const std::uint8_t*, std::size_t,
                    std::uint8_t out[32]) noexcept override;
  bool constantTimeEqual(const std::uint8_t*, const std::uint8_t*, std::size_t) noexcept override;
};
}  // namespace remus::platform::esp32
