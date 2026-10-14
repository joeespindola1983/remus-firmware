#include "remus/acquisition/AcquisitionPipeline.hpp"

namespace remus::acquisition {

Status AcquisitionPipeline::initialize(const SamplingConfig& config, hal::IImu& imu,
                                       hal::IClock& clock, IAcquisitionSink& sink) noexcept {
  if (!core::validateImuProfile(config.imuProfile).ok() ||
      1000000U % config.imuProfile.sampleRateHz != 0)
    return Status::failure(Error::InvalidArgument);
  const auto status = sampler_.initialize(config, imu, clock);
  if (!status.ok()) return status;
  nominalPeriodUs_ = static_cast<std::uint16_t>(1000000U / config.imuProfile.sampleRateHz);
  sink_ = &sink;
  return Status::success();
}

Status AcquisitionPipeline::startSession(RecordingId recordingId) noexcept {
  if (sink_ == nullptr || nominalPeriodUs_ == 0) return Status::failure(Error::NotReady);
  if (recordingId == 0) return Status::failure(Error::InvalidArgument);
  if (recordingId_ != 0 && recordingId_ != recordingId) return Status::failure(Error::Busy);
  if (recordingId_ == recordingId) return Status::success();
  const auto scheduleStatus = sampler_.resetSchedule();
  if (!scheduleStatus.ok()) return scheduleStatus;
  batches_.resetForSession(recordingId, nominalPeriodUs_);
  recordingId_ = recordingId;
  return Status::success();
}

PollResult AcquisitionPipeline::poll(NativeTimestampUs nowUs) noexcept {
  if (recordingId_ == 0) return {false, Status::success()};
  ImuSample sample{};
  auto result = sampler_.pollDue(nowUs, sample);
  if (!result.sampleReady) return result;
  auto pushed = batches_.pushSample(sample);
  if (pushed == PushResult::GapClosedBatch) {
    const auto status = publishReady();
    if (!status.ok()) return {true, status};
    pushed = batches_.pushSample(sample);
  }
  if (pushed == PushResult::Busy) {
    const auto status = publishReady();
    if (!status.ok()) return {true, status};
    pushed = batches_.pushSample(sample);
  }
  if (pushed != PushResult::Accepted) return {true, Status::failure(Error::InvalidArgument)};
  if (batches_.isReady()) result.status = publishReady();
  return result;
}

Status AcquisitionPipeline::publishReady() noexcept {
  SampleBatch batch{};
  if (!batches_.take(batch)) return Status::failure(Error::NotReady);
  return sink_->onBatchReady(batch);
}

}  // namespace remus::acquisition
