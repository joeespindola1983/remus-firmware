#include "remus/platform/esp32/MbedTlsCrypto.hpp"

#include "mbedtls/hkdf.h"
#include "mbedtls/md.h"

namespace remus::platform::esp32 {
namespace {
const mbedtls_md_info_t* sha256() noexcept { return mbedtls_md_info_from_type(MBEDTLS_MD_SHA256); }
bool valid(const std::uint8_t* value, std::size_t size) noexcept {
  return size == 0 || value != nullptr;
}
}  // namespace
Status MbedTlsCrypto::hkdfSha256(const std::uint8_t* secret, std::size_t secretSize,
                                 const std::uint8_t* salt, std::size_t saltSize,
                                 const std::uint8_t* info, std::size_t infoSize, std::uint8_t* out,
                                 std::size_t outSize) noexcept {
  if (!valid(secret, secretSize) || !valid(salt, saltSize) || !valid(info, infoSize) ||
      !valid(out, outSize) || sha256() == nullptr) {
    return Status::failure(Error::InvalidArgument);
  }
  const int result =
      mbedtls_hkdf(sha256(), salt, saltSize, secret, secretSize, info, infoSize, out, outSize);
  return result == 0 ? Status::success()
                     : Status::failure(Error::CryptoError, static_cast<std::uint32_t>(-result));
}
Status MbedTlsCrypto::hmacSha256(const std::uint8_t* key, std::size_t keySize,
                                 const std::uint8_t* data, std::size_t dataSize,
                                 std::uint8_t out[32]) noexcept {
  if (!valid(key, keySize) || !valid(data, dataSize) || out == nullptr || sha256() == nullptr) {
    return Status::failure(Error::InvalidArgument);
  }
  const int result = mbedtls_md_hmac(sha256(), key, keySize, data, dataSize, out);
  return result == 0 ? Status::success()
                     : Status::failure(Error::CryptoError, static_cast<std::uint32_t>(-result));
}
bool MbedTlsCrypto::constantTimeEqual(const std::uint8_t* left, const std::uint8_t* right,
                                      std::size_t size) noexcept {
  if (!valid(left, size) || !valid(right, size)) return false;
  std::uint8_t difference = 0;
  for (std::size_t index = 0; index < size; ++index) difference |= left[index] ^ right[index];
  return difference == 0;
}
}  // namespace remus::platform::esp32
