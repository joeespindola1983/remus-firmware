#include "remus/acquisition/BatchBuilder.hpp"

namespace remus::acquisition {

void BatchBuilder::resetForSession(RecordingId recordingId,
                                   std::uint16_t nominalPeriodUs) noexcept {
  recordingId_ = recordingId;
  nominalPeriodUs_ = nominalPeriodUs;
  nextBatchSeq_ = 0;
  current_ = {};
  readyBatch_ = {};
  ready_ = false;
}

void BatchBuilder::closeCurrent() noexcept {
  readyBatch_ = current_;
  readyBatch_.batchSeq = nextBatchSeq_++;
  ready_ = true;
  current_ = {};
}

PushResult BatchBuilder::pushSample(const ImuSample& sample) noexcept {
  if (recordingId_ == 0 || nominalPeriodUs_ == 0) return PushResult::Invalid;
  if (ready_) return PushResult::Busy;
  if (current_.count != 0 &&
      sample.index != current_.firstSampleIndex + current_.count) {
    closeCurrent();
    return PushResult::GapClosedBatch;
  }
  if (current_.count == 0) {
    current_.recordingId = recordingId_;
    current_.firstSampleIndex = sample.index;
    current_.baseTimeUs = sample.sampledAtUs;
    current_.nominalPeriodUs = nominalPeriodUs_;
  }
  current_.samples[current_.count++] = sample;
  if (current_.count == kSamplesPerBatch) closeCurrent();
  return PushResult::Accepted;
}

bool BatchBuilder::take(SampleBatch& out) noexcept {
  if (!ready_) return false;
  out = readyBatch_;
  readyBatch_ = {};
  ready_ = false;
  return true;
}

}  // namespace remus::acquisition
