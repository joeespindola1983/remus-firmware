#pragma once

#include <cstddef>
#include <cstdint>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "remus/hal/IImu.hpp"

namespace remus::platform::esp32 {

struct Mpu6050Config {
  gpio_num_t sda{GPIO_NUM_6};
  gpio_num_t scl{GPIO_NUM_5};
  gpio_num_t dataReady{GPIO_NUM_NC};
  std::uint8_t address{0x68};
  std::uint32_t busSpeedHz{400000};
};

struct Mpu6050Stats {
  std::uint32_t reads{0};
  std::uint32_t fifoSamples{0};
  std::uint32_t ioErrors{0};
  std::uint32_t fifoOverruns{0};
  std::uint32_t dataReadyInterrupts{0};
};

class Mpu6050Driver final : public hal::IImu {
 public:
  explicit Mpu6050Driver(const Mpu6050Config& config) noexcept : config_(config) {}
  ~Mpu6050Driver() override;

  Status initialize(const core::ImuProfile& profile) noexcept override;
  const core::EffectiveImuProfile& effectiveProfile() const noexcept override {
    return effectiveProfile_;
  }
  Status configureFifo(bool enabled) noexcept override;
  Status read(hal::ImuRaw& out) noexcept override;
  Status readFifo(hal::ImuRaw* destination, std::size_t maxSamples,
                  std::size_t& readSamples) noexcept override;
  Status health() noexcept override;
  bool waitForDataReady(TickType_t timeoutTicks) noexcept;
  Mpu6050Stats stats() const noexcept;
  std::uint8_t detectedAddress() const noexcept { return detectedAddress_; }
  std::uint8_t detectedIdentity() const noexcept { return detectedIdentity_; }

 private:
  static void IRAM_ATTR dataReadyIsr(void* context) noexcept;
  Status writeRegister(std::uint8_t reg, std::uint8_t value) noexcept;
  Status readRegisters(std::uint8_t reg, std::uint8_t* data, std::size_t size) noexcept;
  Status resetFifo() noexcept;
  static hal::ImuRaw parseFifoSample(const std::uint8_t* bytes) noexcept;

  Mpu6050Config config_{};
  i2c_master_bus_handle_t bus_{nullptr};
  i2c_master_dev_handle_t device_{nullptr};
  TaskHandle_t waitingTask_{nullptr};
  volatile std::uint32_t dataReadyInterrupts_{0};
  Mpu6050Stats stats_{};
  bool initialized_{false};
  bool fifoEnabled_{false};
  std::uint8_t detectedAddress_{0};
  std::uint8_t detectedIdentity_{0};
  std::uint8_t configuredDivider_{0};
  core::EffectiveImuProfile effectiveProfile_{};
};

}  // namespace remus::platform::esp32
