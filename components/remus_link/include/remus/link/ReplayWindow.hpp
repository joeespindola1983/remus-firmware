#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "remus/core/Status.hpp"
#include "remus/core/Types.hpp"
#include "remus/link/Protocol.hpp"

namespace remus::link {

constexpr std::size_t kReplayWindowMaximumBatches = 256;

struct ReplayBatch {
  RecordingId recordingId{0};
  std::uint32_t batchSeq{0};
  SampleIndex firstSampleIndex{0};
  NativeTimestampUs originMonoUs{0};
  std::uint16_t payloadSize{0};
  std::array<std::uint8_t, kMaximumPayloadBytes> payload{};
};

struct ReplayGap {
  bool valid{false};
  std::uint32_t fromSeq{0};
  std::uint32_t throughSeq{0};
  SampleIndex firstSampleIndex{0};
};

enum class ReplayAppendDisposition : std::uint8_t {
  Stored,
  StoredWithGap,
  Duplicate,
  Rejected,
};

struct ReplayAppendResult {
  ReplayAppendDisposition disposition{ReplayAppendDisposition::Rejected};
  ReplayGap gap{};
  Status status{Status::failure(Error::InvalidArgument)};
};

// Fixed-memory replay ring. One owner performs all operations; callers move
// batches into/out of the ring through bounded copies and never retain pointers.
class ReplayWindow {
 public:
  explicit ReplayWindow(std::size_t capacity = kReplayWindowMaximumBatches) noexcept;
  ReplayAppendResult append(const ReplayBatch&) noexcept;
  Status prepareReplay(RecordingId, std::uint32_t batchSeq, ReplayBatch&) const noexcept;
  bool knownLostGap(RecordingId, std::uint32_t batchSeq, ReplayGap&) const noexcept;
  Status recordLostGap(RecordingId, const ReplayGap&) noexcept;
  Status markAcceptedThrough(RecordingId, std::uint32_t batchSeq) noexcept;
  Status markCheckpointedThrough(RecordingId, std::uint32_t batchSeq) noexcept;
  std::size_t size() const noexcept { return size_; }
  std::size_t capacity() const noexcept { return capacity_; }
  std::uint32_t oldestSeq() const noexcept;
  std::uint32_t newestSeq() const noexcept;
  std::uint32_t overflowCount() const noexcept { return overflowCount_; }

 private:
  const ReplayBatch* find(RecordingId, std::uint32_t) const noexcept;

  std::array<ReplayBatch, kReplayWindowMaximumBatches> entries_{};
  RecordingId activeRecordingId_{0};
  std::size_t capacity_{kReplayWindowMaximumBatches};
  std::size_t head_{0};
  std::size_t size_{0};
  std::uint32_t overflowCount_{0};
  bool acceptedValid_{false};
  std::uint32_t acceptedThrough_{0};
  bool checkpointedValid_{false};
  std::uint32_t checkpointedThrough_{0};
  RecordingId lostGapRecordingId_{0};
  ReplayGap lostGap_{};
};

}  // namespace remus::link
