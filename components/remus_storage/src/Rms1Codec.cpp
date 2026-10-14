#include "remus/storage/Rms1Codec.hpp"

#include <cstring>

namespace remus::storage {
namespace {
void put16(std::uint8_t* p, std::uint16_t value) noexcept {
  p[0] = static_cast<std::uint8_t>(value);
  p[1] = static_cast<std::uint8_t>(value >> 8);
}
void put32(std::uint8_t* p, std::uint32_t value) noexcept {
  for (unsigned index = 0; index < 4; ++index)
    p[index] = static_cast<std::uint8_t>(value >> (index * 8));
}
void put64(std::uint8_t* p, std::uint64_t value) noexcept {
  for (unsigned index = 0; index < 8; ++index)
    p[index] = static_cast<std::uint8_t>(value >> (index * 8));
}
std::uint32_t get32(const std::uint8_t* p) noexcept {
  return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
         (static_cast<std::uint32_t>(p[2]) << 16) |
         (static_cast<std::uint32_t>(p[3]) << 24);
}
}  // namespace

std::uint32_t crc32c(const std::uint8_t* data, std::size_t size) noexcept {
  std::uint32_t crc = 0xFFFFFFFFU;
  for (std::size_t index = 0; index < size; ++index) {
    crc ^= data[index];
    for (unsigned bit = 0; bit < 8; ++bit)
      crc = (crc >> 1) ^ (0x82F63B78U & (0U - (crc & 1U)));
  }
  return ~crc;
}

Status encodeSessionHeader(const Rms1SessionHeader& value, std::uint8_t* out,
                           std::size_t capacity) noexcept {
  if (out == nullptr || capacity < kRms1SessionHeaderBytes || value.recordingId == 0 ||
      value.computerId == 0 || value.pcBootId == 0)
    return Status::failure(Error::InvalidArgument);
  std::memset(out, 0, kRms1SessionHeaderBytes);
  std::memcpy(out, "RMS1", 4);
  put16(out + 4, 1);
  put16(out + 6, kRms1SessionHeaderBytes);
  put64(out + 8, value.recordingId);
  put64(out + 16, value.computerId);
  put32(out + 24, value.pcBootId);
  put64(out + 28, value.startPcMonotonicUs);
  put32(out + 36, value.firmwareBuildHash);
  put32(out + 40, value.hardwareProfileHash);
  put64(out + 44, static_cast<std::uint64_t>(value.startUtcMilliseconds));
  put32(out + 52, value.networkId);
  put32(out + 56, value.flags);
  put32(out + 60, crc32c(out, 60));
  return Status::success();
}

Status encodeRecord(const Rms1Record& value, std::uint8_t* out, std::size_t capacity,
                    std::size_t& encodedBytes) noexcept {
  encodedBytes = 0;
  if (out == nullptr || value.globalOrdinal == 0 || value.payloadLength > kRms1MaximumPayloadBytes ||
      (value.payloadLength != 0 && value.payload == nullptr))
    return Status::failure(Error::InvalidArgument);
  const std::size_t total = kRms1RecordHeaderBytes + value.payloadLength + kRms1RecordCrcBytes;
  if (capacity < total) return Status::failure(Error::InvalidArgument);
  std::memset(out, 0, kRms1RecordHeaderBytes);
  std::memcpy(out, "RMR1", 4);
  out[4] = 1;
  out[5] = static_cast<std::uint8_t>(value.type);
  put16(out + 6, kRms1RecordHeaderBytes);
  put32(out + 8, static_cast<std::uint32_t>(total));
  put64(out + 12, value.globalOrdinal);
  put64(out + 20, value.appendPcMonotonicUs);
  put64(out + 28, value.originId);
  put32(out + 36, value.originBootId);
  put32(out + 40, value.batchSeqOrEventSeq);
  put32(out + 44, value.payloadLength);
  if (value.payloadLength != 0)
    std::memcpy(out + kRms1RecordHeaderBytes, value.payload, value.payloadLength);
  put32(out + total - kRms1RecordCrcBytes, crc32c(out, total - kRms1RecordCrcBytes));
  encodedBytes = total;
  return Status::success();
}

Status recoverRms1Prefix(const std::uint8_t* data, std::size_t size,
                         Rms1RecoverySummary& summary) noexcept {
  summary = {};
  if (data == nullptr || size < kRms1SessionHeaderBytes)
    return Status::failure(Error::NotReady);
  if (std::memcmp(data, "RMS1", 4) != 0 || data[4] != 1 || data[5] != 0 ||
      data[6] != kRms1SessionHeaderBytes || data[7] != 0)
    return Status::failure(Error::VersionMismatch);
  if (get32(data + 60) != crc32c(data, 60)) return Status::failure(Error::BadCrc);
  summary.validBytes = kRms1SessionHeaderBytes;
  std::size_t offset = kRms1SessionHeaderBytes;
  while (offset < size) {
    if (size - offset < kRms1RecordHeaderBytes + kRms1RecordCrcBytes) {
      summary.truncatedTail = true;
      break;
    }
    const auto* record = data + offset;
    if (std::memcmp(record, "RMR1", 4) != 0 || record[4] != 1 || record[6] != 48 ||
        record[7] != 0)
      return Status::failure(Error::VersionMismatch, static_cast<std::uint32_t>(offset));
    const std::uint32_t total = get32(record + 8);
    const std::uint32_t payload = get32(record + 44);
    if (total != kRms1RecordHeaderBytes + payload + kRms1RecordCrcBytes ||
        payload > kRms1MaximumPayloadBytes)
      return Status::failure(Error::InvalidArgument, static_cast<std::uint32_t>(offset));
    if (size - offset < total) {
      summary.truncatedTail = true;
      break;
    }
    if (get32(record + total - 4) != crc32c(record, total - 4))
      return Status::failure(Error::BadCrc, static_cast<std::uint32_t>(offset));
    offset += total;
    summary.validBytes = offset;
    ++summary.recordCount;
  }
  return Status::success();
}

}  // namespace remus::storage
