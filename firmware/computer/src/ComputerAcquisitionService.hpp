#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "remus/acquisition/AcquisitionPipeline.hpp"
#include "remus/core/Clock.hpp"
#include "remus/platform/esp32/Mpu6050Driver.hpp"

namespace remus::computer {

class ComputerAcquisitionService final : public acquisition::IAcquisitionSink {
 public:
  explicit ComputerAcquisitionService(
      const platform::esp32::Mpu6050Config& imuConfig) noexcept
      : imu_(imuConfig) {}
  Status start(RecordingId recordingId) noexcept;
  Status onBatchReady(const acquisition::SampleBatch& batch) noexcept override;
  std::uint64_t batchCount() const noexcept { return batchCount_; }
  std::uint64_t sampleCount() const noexcept { return sampleCount_; }
  acquisition::SamplingMetrics metrics() const noexcept { return pipeline_.metrics(); }

 private:
  class EspTimerClock final : public IClock {
   public:
    NativeTimestampUs monotonicUs() const noexcept override;
  };
  static void taskEntry(void*) noexcept;
  void run() noexcept;

  EspTimerClock clock_{};
  platform::esp32::Mpu6050Driver imu_;
  acquisition::AcquisitionPipeline pipeline_{};
  std::uint64_t batchCount_{0};
  std::uint64_t sampleCount_{0};
  StaticTask_t taskBuffer_{};
  StackType_t taskStack_[4096]{};
  TaskHandle_t task_{nullptr};
};

}  // namespace remus::computer
