#include "BladeAcquisitionService.hpp"

#include "BladeRadioService.hpp"
#include "esp_log.h"
#include "esp_timer.h"
#include "remus/link/MessageCodec.hpp"

namespace remus::blade {
namespace {
constexpr char kTag[] = "blade_acquisition";
constexpr std::uint16_t kRateHz = 200;
constexpr TickType_t kPollInterval = pdMS_TO_TICKS(1);
}  // namespace

NativeTimestampUs BladeAcquisitionService::EspTimerClock::monotonicUs() const noexcept {
  return static_cast<NativeTimestampUs>(esp_timer_get_time());
}

Status BladeAcquisitionService::start() noexcept {
  Status status = pipeline_.initialize({kRateHz}, imu_, clock_, *this);
  if (!status.ok()) return status;
  task_ = xTaskCreateStatic(taskEntry, "blade_imu", 4096, this, 12, taskStack_, &taskBuffer_);
  if (task_ == nullptr) return Status::failure(Error::NotReady);
  ESP_LOGW(kTag,
           "ready rate_hz=200 sda=6 scl=5 address=0x%02x identity=0x%02x "
           "mode=software_timer drdy_gpio=unassigned fifo_supported=true",
           static_cast<unsigned>(imu_.detectedAddress()),
           static_cast<unsigned>(imu_.detectedIdentity()));
  return Status::success();
}

Status BladeAcquisitionService::beginCapture(RecordingId recordingId) noexcept {
  if (recordingId == 0) return Status::failure(Error::InvalidArgument);
  const auto status = pipeline_.startSession(recordingId);
  if (!status.ok()) return status;
  ESP_LOGI(kTag, "capture_started recording_id=%llu", static_cast<unsigned long long>(recordingId));
  return Status::success();
}

void BladeAcquisitionService::taskEntry(void* context) noexcept {
  static_cast<BladeAcquisitionService*>(context)->run();
}

void BladeAcquisitionService::run() noexcept {
  for (;;) {
    if (pipeline_.recordingId() == 0) {
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }
    const auto now = clock_.monotonicUs();
    const auto result = pipeline_.poll(now);
    if (!result.status.ok() && result.status.error != Error::IoFailure)
      ESP_LOGW(kTag, "pipeline error=%u detail=%lu", static_cast<unsigned>(result.status.error),
               static_cast<unsigned long>(result.status.detail));
    vTaskDelay(kPollInterval);
  }
}

Status BladeAcquisitionService::onBatchReady(const acquisition::SampleBatch& source) noexcept {
  link::DataBatch wire{};
  wire.firstSampleIndex = source.firstSampleIndex;
  wire.batchSeq = source.batchSeq;
  wire.sampleCount = source.count;
  wire.nominalPeriodUs = source.nominalPeriodUs;
  for (std::size_t index = 0; index < source.count; ++index) {
    const auto& sample = source.samples[index];
    const auto deltaUs = sample.sampledAtUs - source.baseTimeUs;
    if (deltaUs > 655350) {
      ESP_LOGE(kTag, "batch_drop reason=timestamp_delta_overflow batch_seq=%lu",
               static_cast<unsigned long>(source.batchSeq));
      return Status::failure(Error::InvalidArgument);
    }
    wire.samples[index] = {static_cast<std::uint16_t>((deltaUs + 5) / 10),
                           sample.raw.ax,
                           sample.raw.ay,
                           sample.raw.az,
                           sample.raw.gx,
                           sample.raw.gy,
                           sample.raw.gz,
                           sample.flags};
  }
  const auto status = radio_.queueDataBatch(source.recordingId, source.baseTimeUs, wire);
  if (!status.ok()) {
    ESP_LOGW(kTag, "batch_drop reason=radio_queue error=%u batch_seq=%lu",
             static_cast<unsigned>(status.error), static_cast<unsigned long>(source.batchSeq));
  }
  return status;
}

}  // namespace remus::blade
