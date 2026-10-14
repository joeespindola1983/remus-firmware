#pragma once

#include "remus/acquisition/Types.hpp"

namespace remus::acquisition {

enum class PushResult : std::uint8_t {
  Accepted = 0,
  GapClosedBatch,
  Busy,
  Invalid,
};

class BatchBuilder {
 public:
  void resetForSession(RecordingId recordingId, std::uint16_t nominalPeriodUs) noexcept;
  PushResult pushSample(const ImuSample& sample) noexcept;
  bool isReady() const noexcept { return ready_; }
  bool take(SampleBatch& out) noexcept;

 private:
  void closeCurrent() noexcept;

  RecordingId recordingId_{0};
  std::uint16_t nominalPeriodUs_{0};
  std::uint32_t nextBatchSeq_{0};
  SampleBatch current_{};
  SampleBatch readyBatch_{};
  bool ready_{false};
};

}  // namespace remus::acquisition
