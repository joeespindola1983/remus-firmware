#include "remus/acquisition/Sampler.hpp"

#include "remus/acquisition/ImuFlags.hpp"

namespace remus::acquisition {

Status Sampler::initialize(const SamplingConfig& config, hal::IImu& imu,
                           hal::IClock& clock) noexcept {
  const auto profileStatus = core::validateImuProfile(config.imuProfile);
  if (!profileStatus.ok()) return profileStatus;
  const auto clockStatus = sampleClock_.reset(config.imuProfile.sampleRateHz, clock.monotonicUs());
  if (!clockStatus.ok()) return clockStatus;
  const auto imuStatus = imu.initialize(config.imuProfile);
  if (!imuStatus.ok()) return imuStatus;
  imu_ = &imu;
  clock_ = &clock;
  rateHz_ = config.imuProfile.sampleRateHz;
  metrics_ = {};
  return Status::success();
}

Status Sampler::resetSchedule() noexcept {
  if (clock_ == nullptr || rateHz_ == 0) return Status::failure(Error::NotReady);
  metrics_ = {};
  return sampleClock_.reset(rateHz_, clock_->monotonicUs());
}

PollResult Sampler::pollDue(NativeTimestampUs nowUs, ImuSample& out) noexcept {
  if (imu_ == nullptr || clock_ == nullptr) {
    return {false, Status::failure(Error::NotReady)};
  }
  const auto due = sampleClock_.consumeDue(nowUs);
  if (!due.due) return {false, Status::success()};

  ++metrics_.attempts;
  metrics_.missedPeriods += due.missedPeriods;
  hal::ImuRaw raw{};
  const auto status = imu_->read(raw);
  if (!status.ok()) {
    ++metrics_.readErrors;
    return {false, status};
  }

  const auto sampledAtUs = clock_->monotonicUs();
  const auto jitter = sampledAtUs >= due.scheduledAtUs ? sampledAtUs - due.scheduledAtUs
                                                       : due.scheduledAtUs - sampledAtUs;
  if (jitter > metrics_.maxAbsJitterUs) metrics_.maxAbsJitterUs = jitter;
  out = {due.index, sampledAtUs, raw, imuQualityFlags(raw)};
  ++metrics_.validSamples;
  return {true, Status::success()};
}

}  // namespace remus::acquisition
