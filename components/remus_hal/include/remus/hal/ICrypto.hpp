#pragma once

#include <cstddef>
#include <cstdint>

#include "remus/core/Status.hpp"

namespace remus::hal {

class ICrypto {
 public:
  virtual ~ICrypto() = default;
  virtual Status hkdfSha256(const std::uint8_t* secret, std::size_t secretLength,
                            const std::uint8_t* salt, std::size_t saltLength,
                            const std::uint8_t* info, std::size_t infoLength,
                            std::uint8_t* out, std::size_t outLength) noexcept = 0;
  virtual Status hmacSha256(const std::uint8_t* key, std::size_t keyLength,
                            const std::uint8_t* data, std::size_t dataLength,
                            std::uint8_t out[32]) noexcept = 0;
  virtual bool constantTimeEqual(const std::uint8_t* left, const std::uint8_t* right,
                                 std::size_t length) noexcept = 0;
};

}  // namespace remus::hal
