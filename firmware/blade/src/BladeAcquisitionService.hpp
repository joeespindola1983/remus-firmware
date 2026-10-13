#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "remus/acquisition/AcquisitionPipeline.hpp"
#include "remus/core/Clock.hpp"
#include "remus/platform/esp32/Mpu6050Driver.hpp"

namespace remus::blade {
class BladeRadioService;

class BladeAcquisitionService final : public acquisition::IAcquisitionSink {
 public:
  BladeAcquisitionService(BladeRadioService& radio,
                          const platform::esp32::Mpu6050Config& imuConfig) noexcept
      : radio_(radio), imu_(imuConfig) {}
  Status start() noexcept;
  Status beginCapture(RecordingId recordingId) noexcept;
  acquisition::SamplingMetrics metrics() const noexcept { return pipeline_.metrics(); }
  Status onBatchReady(const acquisition::SampleBatch& source) noexcept override;

 private:
  class EspTimerClock final : public IClock {
   public:
    NativeTimestampUs monotonicUs() const noexcept override;
  };
  static void taskEntry(void*) noexcept;
  void run() noexcept;

  BladeRadioService& radio_;
  EspTimerClock clock_{};
  platform::esp32::Mpu6050Driver imu_;
  acquisition::AcquisitionPipeline pipeline_{};
  StaticTask_t taskBuffer_{};
  StackType_t taskStack_[4096]{};
  TaskHandle_t task_{nullptr};
};

}  // namespace remus::blade
