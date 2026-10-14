#include "remus/link/FrameCodec.hpp"

#include <cstring>

namespace remus::link {
namespace {
constexpr std::uint8_t kMagic[4] = {'R', 'M', 'F', '1'};
void put16(std::uint8_t* p, std::uint16_t v) {
  p[0] = v;
  p[1] = v >> 8;
}
void put32(std::uint8_t* p, std::uint32_t v) {
  for (int i = 0; i < 4; ++i) p[i] = v >> (8 * i);
}
void put64(std::uint8_t* p, std::uint64_t v) {
  for (int i = 0; i < 8; ++i) p[i] = v >> (8 * i);
}
std::uint16_t get16(const std::uint8_t* p) {
  return static_cast<std::uint16_t>(p[0]) | static_cast<std::uint16_t>(p[1]) << 8;
}
std::uint32_t get32(const std::uint8_t* p) {
  std::uint32_t v = 0;
  for (int i = 0; i < 4; ++i) v |= static_cast<std::uint32_t>(p[i]) << (8 * i);
  return v;
}
std::uint64_t get64(const std::uint8_t* p) {
  std::uint64_t v = 0;
  for (int i = 0; i < 8; ++i) v |= static_cast<std::uint64_t>(p[i]) << (8 * i);
  return v;
}
std::uint32_t crc32c(const std::uint8_t* data, std::size_t size) {
  std::uint32_t crc = 0xFFFFFFFFU;
  for (std::size_t i = 0; i < size; ++i) {
    crc ^= data[i];
    for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0x82F63B78U & (0U - (crc & 1U)));
  }
  return ~crc;
}
bool validType(std::uint8_t type) { return type >= 1U && type <= 0x11U; }
}  // namespace

Status FrameCodec::encode(const FrameHeader& h, const std::uint8_t* payload,
                          std::size_t payloadSize, const std::uint8_t* key, std::size_t keySize,
                          EncodedFrame& out) noexcept {
  if ((payloadSize != 0 && payload == nullptr) || key == nullptr || keySize == 0 ||
      payloadSize > kMaximumPayloadBytes || h.flags != 0 ||
      !validType(static_cast<std::uint8_t>(h.type)))
    return Status::failure(Error::InvalidArgument);
  const std::size_t total = kMinimumFrameBytes + payloadSize;
  auto* b = out.bytes.data();
  std::memcpy(b, kMagic, 4);
  b[4] = kWireVersion;
  b[5] = static_cast<std::uint8_t>(h.type);
  b[6] = h.flags;
  b[7] = kHeaderBytes;
  put16(b + 8, static_cast<std::uint16_t>(total));
  put32(b + 10, h.networkId);
  put64(b + 14, h.senderId);
  put32(b + 22, h.senderBootId);
  put64(b + 26, h.recordingId);
  put32(b + 34, h.messageSeq);
  put64(b + 38, h.originMonoUs);
  put16(b + 46, static_cast<std::uint16_t>(payloadSize));
  if (payloadSize) std::memcpy(b + kHeaderBytes, payload, payloadSize);
  put32(b + kHeaderBytes + payloadSize, crc32c(b, kHeaderBytes + payloadSize));
  std::uint8_t tag[32]{};
  const Status auth =
      crypto_.hmacSha256(key, keySize, b, kHeaderBytes + payloadSize + kCrcBytes, tag);
  if (!auth.ok()) return auth;
  std::memcpy(b + kHeaderBytes + payloadSize + kCrcBytes, tag, kAuthTagBytes);
  out.size = total;
  return Status::success();
}

Status FrameCodec::decode(const std::uint8_t* b, std::size_t size, const std::uint8_t* key,
                          std::size_t keySize, DecodedFrame& out) noexcept {
  if (b == nullptr || key == nullptr || keySize == 0 || size < kMinimumFrameBytes ||
      size > kMaximumFrameBytes)
    return Status::failure(Error::InvalidArgument);
  if (std::memcmp(b, kMagic, 4) != 0 || b[7] != kHeaderBytes || b[6] != 0 || !validType(b[5]))
    return Status::failure(Error::InvalidArgument);
  if (b[4] != kWireVersion) return Status::failure(Error::VersionMismatch, b[4]);
  const std::size_t payloadSize = get16(b + 46);
  if (get16(b + 8) != size || payloadSize != size - kMinimumFrameBytes ||
      payloadSize > kMaximumPayloadBytes)
    return Status::failure(Error::InvalidArgument);
  const std::size_t crcOffset = kHeaderBytes + payloadSize;
  if (get32(b + crcOffset) != crc32c(b, crcOffset)) return Status::failure(Error::BadCrc);
  std::uint8_t tag[32]{};
  const Status auth = crypto_.hmacSha256(key, keySize, b, crcOffset + kCrcBytes, tag);
  if (!auth.ok()) return auth;
  if (!crypto_.constantTimeEqual(tag, b + crcOffset + kCrcBytes, kAuthTagBytes))
    return Status::failure(Error::AuthFailed);
  out.header.type = static_cast<MessageType>(b[5]);
  out.header.flags = b[6];
  out.header.networkId = get32(b + 10);
  out.header.senderId = get64(b + 14);
  out.header.senderBootId = get32(b + 22);
  out.header.recordingId = get64(b + 26);
  out.header.messageSeq = get32(b + 34);
  out.header.originMonoUs = get64(b + 38);
  if (payloadSize) std::memcpy(out.payload.data(), b + kHeaderBytes, payloadSize);
  out.payloadSize = payloadSize;
  return Status::success();
}
}  // namespace remus::link
