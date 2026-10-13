#pragma once

#include <cstddef>
#include <cstdint>

#include "remus/hal/ICrypto.hpp"

namespace remus::test {

// Deterministic test double only. It deliberately provides no cryptographic security.
class FakeCrypto final : public hal::ICrypto {
 public:
  Status hkdfSha256(const std::uint8_t* secret, std::size_t secretLength,
                    const std::uint8_t* salt, std::size_t saltLength,
                    const std::uint8_t* info, std::size_t infoLength, std::uint8_t* out,
                    std::size_t outLength) noexcept override {
    if (!valid(secret, secretLength) || !valid(salt, saltLength) || !valid(info, infoLength) ||
        !valid(out, outLength)) {
      return Status::failure(Error::InvalidArgument);
    }
    for (std::size_t i = 0; i < outLength; ++i) {
      out[i] = byteAt(secret, secretLength, i) ^ byteAt(salt, saltLength, i + 1U) ^
               byteAt(info, infoLength, i + 2U) ^ static_cast<std::uint8_t>(i);
    }
    return Status::success();
  }

  Status hmacSha256(const std::uint8_t* key, std::size_t keyLength, const std::uint8_t* data,
                    std::size_t dataLength, std::uint8_t out[32]) noexcept override {
    if (!valid(key, keyLength) || !valid(data, dataLength) || out == nullptr) {
      return Status::failure(Error::InvalidArgument);
    }
    for (std::size_t i = 0; i < 32; ++i) {
      out[i] = byteAt(key, keyLength, i) ^ byteAt(data, dataLength, i + 3U) ^
               static_cast<std::uint8_t>(0xA5U + i);
    }
    return Status::success();
  }

  bool constantTimeEqual(const std::uint8_t* left, const std::uint8_t* right,
                         std::size_t length) noexcept override {
    if (!valid(left, length) || !valid(right, length)) {
      return false;
    }
    std::uint8_t difference = 0;
    for (std::size_t i = 0; i < length; ++i) {
      difference |= left[i] ^ right[i];
    }
    return difference == 0;
  }

 private:
  static bool valid(const std::uint8_t* bytes, std::size_t length) noexcept {
    return length == 0 || bytes != nullptr;
  }

  static std::uint8_t byteAt(const std::uint8_t* bytes, std::size_t length,
                             std::size_t index) noexcept {
    return length == 0 ? 0 : bytes[index % length];
  }
};

}  // namespace remus::test
