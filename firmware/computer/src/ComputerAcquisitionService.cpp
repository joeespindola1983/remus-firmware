#include "ComputerAcquisitionService.hpp"

#include "SdDiagnosticLogger.hpp"

#include "esp_log.h"
#include "esp_timer.h"
#include "remus/link/MessageCodec.hpp"

namespace remus::computer {
namespace {
constexpr char kTag[] = "computer_acquisition";
constexpr TickType_t kPollInterval = pdMS_TO_TICKS(1);
}  // namespace

NativeTimestampUs ComputerAcquisitionService::EspTimerClock::monotonicUs() const noexcept {
  return static_cast<NativeTimestampUs>(esp_timer_get_time());
}

Status ComputerAcquisitionService::start(RecordingId recordingId) noexcept {
  auto status = pipeline_.initialize({200}, imu_, clock_, *this);
  if (!status.ok()) return status;
  status = pipeline_.startSession(recordingId);
  if (!status.ok()) return status;
  link::SourceManifest manifest{computerId_, bootId_, imu_.detectedIdentity(), 8, 500, 42, 200,
                                0, 0, 0, 0, 0, 4096, 655};
  std::uint8_t manifestPayload[link::kSourceManifestPayloadBytes]{};
  std::size_t manifestSize = 0;
  status = link::MessageCodec::encode(manifest, manifestPayload, sizeof(manifestPayload),
                                      manifestSize);
  if (!status.ok() || storage_ == nullptr ||
      !storage_->enqueueSourceManifest(computerId_, bootId_, manifestPayload, manifestSize))
    return Status::failure(Error::QueueFull);
  task_ = xTaskCreateStatic(taskEntry, "computer_imu", 4096, this, 12, taskStack_, &taskBuffer_);
  if (task_ == nullptr) return Status::failure(Error::NotReady);
  ESP_LOGW(kTag,
           "ready source=computer_local_imu rate_hz=200 address=0x%02x identity=0x%02x "
           "mode=software_timer drdy_gpio=unassigned fifo_supported=true",
           static_cast<unsigned>(imu_.detectedAddress()),
           static_cast<unsigned>(imu_.detectedIdentity()));
  return Status::success();
}

void ComputerAcquisitionService::taskEntry(void* context) noexcept {
  static_cast<ComputerAcquisitionService*>(context)->run();
}

void ComputerAcquisitionService::run() noexcept {
  for (;;) {
    const auto result = pipeline_.poll(clock_.monotonicUs());
    if (!result.status.ok() && result.status.error != Error::IoFailure)
      ESP_LOGW(kTag, "pipeline error=%u detail=%lu", static_cast<unsigned>(result.status.error),
               static_cast<unsigned long>(result.status.detail));
    vTaskDelay(kPollInterval);
  }
}

Status ComputerAcquisitionService::onBatchReady(
    const acquisition::SampleBatch& batch) noexcept {
  link::DataBatch wire{};
  wire.firstSampleIndex = batch.firstSampleIndex;
  wire.batchSeq = batch.batchSeq;
  wire.sampleCount = batch.count;
  wire.nominalPeriodUs = batch.nominalPeriodUs;
  for (std::size_t index = 0; index < batch.count; ++index) {
    const auto& sample = batch.samples[index];
    const auto deltaUs = sample.sampledAtUs - batch.baseTimeUs;
    if (deltaUs > 655350) return Status::failure(Error::InvalidArgument);
    wire.samples[index] = {static_cast<std::uint16_t>((deltaUs + 5) / 10),
                           sample.raw.ax, sample.raw.ay, sample.raw.az,
                           sample.raw.gx, sample.raw.gy, sample.raw.gz, sample.flags};
  }
  std::uint8_t payload[link::kMaximumDataBatchPayloadBytes]{};
  std::size_t payloadSize = 0;
  Status status = link::MessageCodec::encode(wire, payload, sizeof(payload), payloadSize);
  if (!status.ok()) return status;
  if (storage_ == nullptr || computerId_ == 0 || bootId_ == 0 ||
      !storage_->enqueueImuBatch(computerId_, bootId_, batch.batchSeq, batch.baseTimeUs,
                                 static_cast<std::uint64_t>(esp_timer_get_time()), -127,
                                 payload, payloadSize))
    return Status::failure(Error::QueueFull);
  ++batchCount_;
  sampleCount_ += batch.count;
  if (batchCount_ % 20 == 0) {
    ESP_LOGI(kTag, "sink batches=%llu samples=%llu last_batch_seq=%lu",
             static_cast<unsigned long long>(batchCount_),
             static_cast<unsigned long long>(sampleCount_),
             static_cast<unsigned long>(batch.batchSeq));
  }
  return Status::success();
}

}  // namespace remus::computer
