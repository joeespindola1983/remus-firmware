#pragma once

#include <cstddef>
#include <cstdint>

#include "remus/core/Status.hpp"

namespace remus::storage {

constexpr std::size_t kRms1SessionHeaderBytes = 64;
constexpr std::size_t kRms1RecordHeaderBytes = 48;
constexpr std::size_t kRms1RecordCrcBytes = 4;
constexpr std::size_t kRms1MaximumPayloadBytes = 196;
constexpr std::size_t kRms1MaximumRecordBytes =
    kRms1RecordHeaderBytes + kRms1MaximumPayloadBytes + kRms1RecordCrcBytes;

enum class Rms1RecordType : std::uint8_t {
  SessionEvent = 0x01,
  Participant = 0x02,
  ImuBatch = 0x03,
  Gap = 0x05,
  Health = 0x07,
  FinalSummary = 0x09,
  SourceManifest = 0x0A,
};

struct Rms1SessionHeader {
  std::uint64_t recordingId{0};
  std::uint64_t computerId{0};
  std::uint32_t pcBootId{0};
  std::uint64_t startPcMonotonicUs{0};
  std::uint32_t firmwareBuildHash{0};
  std::uint32_t hardwareProfileHash{0};
  std::int64_t startUtcMilliseconds{0};
  std::uint32_t networkId{0};
  std::uint32_t flags{0};
};

struct Rms1Record {
  Rms1RecordType type{Rms1RecordType::SessionEvent};
  std::uint64_t globalOrdinal{0};
  std::uint64_t appendPcMonotonicUs{0};
  std::uint64_t originId{0};
  std::uint32_t originBootId{0};
  std::uint32_t batchSeqOrEventSeq{0};
  const std::uint8_t* payload{nullptr};
  std::uint32_t payloadLength{0};
};

struct Rms1RecoverySummary {
  std::size_t validBytes{0};
  std::uint64_t recordCount{0};
  bool truncatedTail{false};
};

std::uint32_t crc32c(const std::uint8_t* data, std::size_t size) noexcept;
Status encodeSessionHeader(const Rms1SessionHeader&, std::uint8_t* destination,
                           std::size_t capacity) noexcept;
Status encodeRecord(const Rms1Record&, std::uint8_t* destination, std::size_t capacity,
                    std::size_t& encodedBytes) noexcept;
Status recoverRms1Prefix(const std::uint8_t* data, std::size_t size,
                         Rms1RecoverySummary&) noexcept;

}  // namespace remus::storage
