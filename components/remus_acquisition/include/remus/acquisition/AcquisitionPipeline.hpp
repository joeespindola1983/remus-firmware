#pragma once

#include "remus/acquisition/BatchBuilder.hpp"
#include "remus/acquisition/Sampler.hpp"

namespace remus::acquisition {

class IAcquisitionSink {
 public:
  virtual ~IAcquisitionSink() = default;
  virtual Status onBatchReady(const SampleBatch& batch) noexcept = 0;
};

// Shared acquisition policy for every Remus IMU. Device-specific services own
// scheduling and choose a sink; this class owns sampling/index/batch semantics.
class AcquisitionPipeline {
 public:
  Status initialize(const SamplingConfig&, hal::IImu&, hal::IClock&,
                    IAcquisitionSink&) noexcept;
  Status startSession(RecordingId recordingId) noexcept;
  PollResult poll(NativeTimestampUs nowUs) noexcept;
  const SamplingMetrics& metrics() const noexcept { return sampler_.metrics(); }
  RecordingId recordingId() const noexcept { return recordingId_; }

 private:
  Status publishReady() noexcept;

  Sampler sampler_{};
  BatchBuilder batches_{};
  IAcquisitionSink* sink_{nullptr};
  RecordingId recordingId_{0};
  std::uint16_t nominalPeriodUs_{0};
};

}  // namespace remus::acquisition
