#include "ComputerAcquisitionService.hpp"

#include "esp_log.h"
#include "esp_timer.h"

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
  task_ = xTaskCreateStatic(taskEntry, "computer_imu", 4096, this, 12, taskStack_, &taskBuffer_);
  if (task_ == nullptr) return Status::failure(Error::NotReady);
  ESP_LOGW(kTag,
           "ready source=computer_local_imu rate_hz=200 mode=software_timer "
           "drdy_gpio=unassigned fifo_supported=true");
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
