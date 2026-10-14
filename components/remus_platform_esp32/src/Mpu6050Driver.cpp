#include "remus/platform/esp32/Mpu6050Driver.hpp"

#include <algorithm>

#include "esp_err.h"
#include "esp_rom_sys.h"
#include "remus/platform/esp32/Mpu6050Compatibility.hpp"

namespace remus::platform::esp32 {
namespace {
constexpr std::uint8_t kWhoAmI = 0x75;
constexpr std::uint8_t kSampleRateDivider = 0x19;
constexpr std::uint8_t kConfig = 0x1A;
constexpr std::uint8_t kGyroConfig = 0x1B;
constexpr std::uint8_t kAccelConfig = 0x1C;
constexpr std::uint8_t kFifoEnable = 0x23;
constexpr std::uint8_t kInterruptPinConfig = 0x37;
constexpr std::uint8_t kInterruptEnable = 0x38;
constexpr std::uint8_t kInterruptStatus = 0x3A;
constexpr std::uint8_t kAccelOutput = 0x3B;
constexpr std::uint8_t kUserControl = 0x6A;
constexpr std::uint8_t kPowerManagement = 0x6B;
constexpr std::uint8_t kFifoCount = 0x72;
constexpr std::uint8_t kFifoData = 0x74;
constexpr std::size_t kFifoSampleBytes = 12;
constexpr std::size_t kFifoCapacityBytes = 1024;
constexpr int kTransferTimeoutMs = 20;

std::int16_t signedBigEndian(const std::uint8_t* p) noexcept {
  return static_cast<std::int16_t>((static_cast<std::uint16_t>(p[0]) << 8) | p[1]);
}
}  // namespace

Mpu6050Driver::~Mpu6050Driver() {
  if (config_.dataReady != GPIO_NUM_NC) gpio_isr_handler_remove(config_.dataReady);
  if (device_ != nullptr) i2c_master_bus_rm_device(device_);
  if (bus_ != nullptr) i2c_del_master_bus(bus_);
}

Status Mpu6050Driver::writeRegister(std::uint8_t reg, std::uint8_t value) noexcept {
  const std::uint8_t bytes[]{reg, value};
  const auto error = i2c_master_transmit(device_, bytes, sizeof(bytes), kTransferTimeoutMs);
  if (error == ESP_OK) return Status::success();
  ++stats_.ioErrors;
  return Status::failure(Error::IoFailure, static_cast<std::uint32_t>(error));
}

Status Mpu6050Driver::readRegisters(std::uint8_t reg, std::uint8_t* data,
                                    std::size_t size) noexcept {
  if (device_ == nullptr || data == nullptr || size == 0)
    return Status::failure(Error::InvalidArgument);
  const auto error =
      i2c_master_transmit_receive(device_, &reg, 1, data, size, kTransferTimeoutMs);
  if (error == ESP_OK) return Status::success();
  ++stats_.ioErrors;
  return Status::failure(Error::IoFailure, static_cast<std::uint32_t>(error));
}

Status Mpu6050Driver::initialize(const core::ImuProfile& profile) noexcept {
  if (initialized_ || !core::validateImuProfile(profile).ok() ||
      config_.sda == GPIO_NUM_NC ||
      config_.scl == GPIO_NUM_NC) {
    return Status::failure(Error::InvalidArgument);
  }
  i2c_master_bus_config_t busConfig{};
  busConfig.i2c_port = I2C_NUM_0;
  busConfig.sda_io_num = config_.sda;
  busConfig.scl_io_num = config_.scl;
  busConfig.clk_source = I2C_CLK_SRC_DEFAULT;
  busConfig.glitch_ignore_cnt = 7;
  busConfig.flags.enable_internal_pullup = true;
  auto error = i2c_new_master_bus(&busConfig, &bus_);
  if (error != ESP_OK) return Status::failure(Error::IoFailure, error);

  // Match the proven remus-sensor bring-up: allow the powered module and bus
  // to settle, then probe both legal AD0 selections before configuring it.
  esp_rom_delay_us(30000);

  const std::uint8_t alternateAddress = config_.address == 0x68 ? 0x69 : 0x68;
  std::uint8_t selectedAddress = config_.address;
  error = i2c_master_probe(bus_, selectedAddress, kTransferTimeoutMs);
  if (error != ESP_OK) {
    selectedAddress = alternateAddress;
    error = i2c_master_probe(bus_, selectedAddress, kTransferTimeoutMs);
  }
  if (error != ESP_OK) return Status::failure(Error::NotReady, error);

  i2c_device_config_t deviceConfig{};
  deviceConfig.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  deviceConfig.device_address = selectedAddress;
  deviceConfig.scl_speed_hz = config_.busSpeedHz;
  error = i2c_master_bus_add_device(bus_, &deviceConfig, &device_);
  if (error != ESP_OK) return Status::failure(Error::IoFailure, error);

  Status status = writeRegister(kPowerManagement, 0x00);
  if (!status.ok()) return status;
  esp_rom_delay_us(10000);
  const auto divider = static_cast<std::uint8_t>(1000U / profile.sampleRateHz - 1U);
  if (!(status = writeRegister(kConfig, profile.dlpfCode)).ok() ||
      !(status = writeRegister(kSampleRateDivider, divider)).ok() ||
      !(status = writeRegister(kAccelConfig, core::accelRegisterBits(profile.accelRange))).ok() ||
      !(status = writeRegister(kGyroConfig, core::gyroRegisterBits(profile.gyroRange))).ok()) {
    return status;
  }

  std::uint8_t identity = 0;
  std::uint8_t power = 0;
  std::uint8_t dlpf = 0;
  std::uint8_t configuredDivider = 0;
  std::uint8_t accel = 0;
  std::uint8_t gyro = 0;
  if (!(status = readRegisters(kWhoAmI, &identity, 1)).ok() ||
      !(status = readRegisters(kPowerManagement, &power, 1)).ok() ||
      !(status = readRegisters(kConfig, &dlpf, 1)).ok() ||
      !(status = readRegisters(kSampleRateDivider, &configuredDivider, 1)).ok() ||
      !(status = readRegisters(kAccelConfig, &accel, 1)).ok() ||
      !(status = readRegisters(kGyroConfig, &gyro, 1)).ok()) {
    return status;
  }
  if (!hasMpu6050RegisterProfile(power, dlpf, configuredDivider, accel, gyro, divider, profile))
    return Status::failure(Error::NotReady, identity);
  detectedAddress_ = selectedAddress;
  detectedIdentity_ = identity;
  configuredDivider_ = divider;
  effectiveProfile_ = {profile,
                       identity,
                       selectedAddress,
                       accel,
                       gyro,
                       dlpf,
                       configuredDivider,
                       core::accelLsbPerG(profile.accelRange),
                       core::gyroLsbPerDpsX10(profile.gyroRange),
                       true};

  if (config_.dataReady != GPIO_NUM_NC) {
    gpio_config_t gpioConfig{};
    gpioConfig.pin_bit_mask = 1ULL << config_.dataReady;
    gpioConfig.mode = GPIO_MODE_INPUT;
    gpioConfig.pull_up_en = GPIO_PULLUP_DISABLE;
    gpioConfig.pull_down_en = GPIO_PULLDOWN_DISABLE;
    gpioConfig.intr_type = GPIO_INTR_POSEDGE;
    if ((error = gpio_config(&gpioConfig)) != ESP_OK)
      return Status::failure(Error::IoFailure, error);
    error = gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
    if (error != ESP_OK && error != ESP_ERR_INVALID_STATE)
      return Status::failure(Error::IoFailure, error);
    if ((error = gpio_isr_handler_add(config_.dataReady, dataReadyIsr, this)) != ESP_OK)
      return Status::failure(Error::IoFailure, error);
    if (!(status = writeRegister(kInterruptPinConfig, 0x10)).ok() ||
        !(status = writeRegister(kInterruptEnable, 0x11)).ok())
      return status;
  }
  initialized_ = true;
  return Status::success();
}

Status Mpu6050Driver::resetFifo() noexcept {
  Status status = writeRegister(kUserControl, 0x04);
  if (!status.ok()) return status;
  return writeRegister(kUserControl, fifoEnabled_ ? 0x40 : 0x00);
}

Status Mpu6050Driver::configureFifo(bool enabled) noexcept {
  if (!initialized_) return Status::failure(Error::NotReady);
  fifoEnabled_ = enabled;
  Status status = writeRegister(kFifoEnable, enabled ? 0x78 : 0x00);
  if (!status.ok()) return status;
  return resetFifo();
}

Status Mpu6050Driver::read(hal::ImuRaw& out) noexcept {
  if (!initialized_) return Status::failure(Error::NotReady);
  std::uint8_t bytes[14]{};
  const auto status = readRegisters(kAccelOutput, bytes, sizeof(bytes));
  if (!status.ok()) return status;
  out = {signedBigEndian(bytes),     signedBigEndian(bytes + 2),
         signedBigEndian(bytes + 4), signedBigEndian(bytes + 8),
         signedBigEndian(bytes + 10), signedBigEndian(bytes + 12)};
  ++stats_.reads;
  return Status::success();
}

hal::ImuRaw Mpu6050Driver::parseFifoSample(const std::uint8_t* bytes) noexcept {
  return {signedBigEndian(bytes),     signedBigEndian(bytes + 2),
          signedBigEndian(bytes + 4), signedBigEndian(bytes + 6),
          signedBigEndian(bytes + 8), signedBigEndian(bytes + 10)};
}

Status Mpu6050Driver::readFifo(hal::ImuRaw* destination, std::size_t maxSamples,
                               std::size_t& readSamples) noexcept {
  readSamples = 0;
  if (!initialized_ || !fifoEnabled_) return Status::failure(Error::NotReady);
  if (destination == nullptr || maxSamples == 0) return Status::failure(Error::InvalidArgument);
  std::uint8_t interruptStatus = 0;
  Status status = readRegisters(kInterruptStatus, &interruptStatus, 1);
  if (!status.ok()) return status;
  std::uint8_t countBytes[2]{};
  status = readRegisters(kFifoCount, countBytes, sizeof(countBytes));
  if (!status.ok()) return status;
  const std::size_t fifoBytes = (static_cast<std::size_t>(countBytes[0]) << 8) | countBytes[1];
  if ((interruptStatus & 0x10) != 0 || fifoBytes >= kFifoCapacityBytes) {
    ++stats_.fifoOverruns;
    resetFifo();
    return Status::failure(Error::BufferOverrun, static_cast<std::uint32_t>(fifoBytes));
  }
  const auto availableSamples = fifoBytes / kFifoSampleBytes;
  const auto requested = std::min(maxSamples, availableSamples);
  std::uint8_t bytes[kFifoSampleBytes]{};
  for (std::size_t index = 0; index < requested; ++index) {
    status = readRegisters(kFifoData, bytes, sizeof(bytes));
    if (!status.ok()) return status;
    destination[index] = parseFifoSample(bytes);
    ++readSamples;
  }
  stats_.fifoSamples += static_cast<std::uint32_t>(readSamples);
  return Status::success();
}

Status Mpu6050Driver::health() noexcept {
  if (!initialized_) return Status::failure(Error::NotReady);
  std::uint8_t power = 0;
  std::uint8_t dlpf = 0;
  std::uint8_t divider = 0;
  std::uint8_t accel = 0;
  std::uint8_t gyro = 0;
  Status status{};
  if (!(status = readRegisters(kPowerManagement, &power, 1)).ok() ||
      !(status = readRegisters(kConfig, &dlpf, 1)).ok() ||
      !(status = readRegisters(kSampleRateDivider, &divider, 1)).ok() ||
      !(status = readRegisters(kAccelConfig, &accel, 1)).ok() ||
      !(status = readRegisters(kGyroConfig, &gyro, 1)).ok())
    return status;
  return hasMpu6050RegisterProfile(power, dlpf, divider, accel, gyro, configuredDivider_,
                                   effectiveProfile_.requested)
             ? Status::success()
             : Status::failure(Error::IoFailure, detectedIdentity_);
}

void IRAM_ATTR Mpu6050Driver::dataReadyIsr(void* context) noexcept {
  auto* self = static_cast<Mpu6050Driver*>(context);
  self->dataReadyInterrupts_ = self->dataReadyInterrupts_ + 1U;
  BaseType_t shouldYield = pdFALSE;
  if (self->waitingTask_ != nullptr) vTaskNotifyGiveFromISR(self->waitingTask_, &shouldYield);
  if (shouldYield == pdTRUE) portYIELD_FROM_ISR();
}

bool Mpu6050Driver::waitForDataReady(TickType_t timeoutTicks) noexcept {
  if (config_.dataReady == GPIO_NUM_NC) return false;
  waitingTask_ = xTaskGetCurrentTaskHandle();
  const auto notified = ulTaskNotifyTake(pdTRUE, timeoutTicks) != 0;
  waitingTask_ = nullptr;
  return notified;
}

Mpu6050Stats Mpu6050Driver::stats() const noexcept {
  auto result = stats_;
  result.dataReadyInterrupts = dataReadyInterrupts_;
  return result;
}

}  // namespace remus::platform::esp32
