#pragma once

#include <cstdint>

#include "remus/acquisition/SampleClock.hpp"
#include "remus/acquisition/Types.hpp"
#include "remus/hal/IClock.hpp"
#include "remus/hal/IImu.hpp"

namespace remus::acquisition {

struct SamplingConfig {
  core::ImuProfile imuProfile{core::computerImuProfile()};
};

struct SamplingMetrics {
  std::uint64_t attempts{0};
  std::uint64_t validSamples{0};
  std::uint64_t readErrors{0};
  std::uint64_t missedPeriods{0};
  std::uint64_t maxAbsJitterUs{0};
};

struct PollResult {
  bool sampleReady{false};
  Status status{};
};

class Sampler {
 public:
  Status initialize(const SamplingConfig& config, hal::IImu& imu, hal::IClock& clock) noexcept;
  Status resetSchedule() noexcept;
  PollResult pollDue(NativeTimestampUs nowUs, ImuSample& out) noexcept;
  const SamplingMetrics& metrics() const noexcept { return metrics_; }

 private:
  hal::IImu* imu_{nullptr};
  hal::IClock* clock_{nullptr};
  SampleClock sampleClock_{};
  SamplingMetrics metrics_{};
  std::uint16_t rateHz_{0};
};

}  // namespace remus::acquisition
