#include "remus/link/KeyDerivation.hpp"

#include <algorithm>

namespace remus::link {
namespace {
constexpr std::uint8_t kDiscovery[] = "remus/rf-v1/discovery";
constexpr std::uint8_t kPairAuth[] = "remus/rf-v1/pair-auth";
constexpr std::uint8_t kPairLmk[] = "remus/rf-v1/espnow-lmk";
constexpr std::uint8_t kPmk[] = "remus/rf-v1/espnow-pmk";
void put32(std::uint8_t* p, std::uint32_t v) {
  for (int i = 0; i < 4; ++i) p[i] = v >> (8 * i);
}
void put64(std::uint8_t* p, std::uint64_t v) {
  for (int i = 0; i < 8; ++i) p[i] = v >> (8 * i);
}
Status derive(hal::ICrypto& c, const std::uint8_t* secret, std::size_t n, const std::uint8_t* salt,
              std::size_t sn, const std::uint8_t* info, std::size_t in, std::uint8_t* out,
              std::size_t on) {
  if (secret == nullptr || n != 32) return Status::failure(Error::InvalidArgument);
  return c.hkdfSha256(secret, n, salt, sn, info, in, out, on);
}
}  // namespace
Status deriveGroupAuthKey(hal::ICrypto& c, const std::uint8_t* s, std::size_t n, NetworkId id,
                          std::array<std::uint8_t, 32>& out) noexcept {
  std::uint8_t salt[4];
  put32(salt, id);
  return derive(c, s, n, salt, sizeof(salt), kDiscovery, sizeof(kDiscovery) - 1, out.data(),
                out.size());
}
Status derivePmk(hal::ICrypto& c, const std::uint8_t* s, std::size_t n, NetworkId id,
                 std::array<std::uint8_t, 16>& out) noexcept {
  std::uint8_t salt[4];
  put32(salt, id);
  return derive(c, s, n, salt, sizeof(salt), kPmk, sizeof(kPmk) - 1, out.data(), out.size());
}
Status derivePairKeys(hal::ICrypto& c, const std::uint8_t* s, std::size_t n, DeviceSerialNumber a,
                      DeviceSerialNumber b, DerivedKeys& out) noexcept {
  if (a == 0 || b == 0 || a == b) return Status::failure(Error::InvalidArgument);
  std::uint8_t salt[16];
  put64(salt, std::min(a, b));
  put64(salt + 8, std::max(a, b));
  Status status = derive(c, s, n, salt, sizeof(salt), kPairAuth, sizeof(kPairAuth) - 1,
                         out.pairAuthKey.data(), out.pairAuthKey.size());
  if (!status.ok()) return status;
  return derive(c, s, n, salt, sizeof(salt), kPairLmk, sizeof(kPairLmk) - 1, out.pairLmk.data(),
                out.pairLmk.size());
}
}  // namespace remus::link
