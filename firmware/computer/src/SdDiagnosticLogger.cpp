#include "SdDiagnosticLogger.hpp"

#include <cstdio>

#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

namespace remus::computer {

namespace {

constexpr char kMountPoint[] = "/sdcard";
constexpr int kSdMisoGpio = 8;
constexpr int kSdMosiGpio = 10;
constexpr int kSdClockGpio = 20;
constexpr int kSdChipSelectGpio = 21;
constexpr int kSdFrequencyKhz = 10000;

const char* levelName(DiagnosticLogLevel level) noexcept {
  switch (level) {
    case DiagnosticLogLevel::Info:
      return "INFO";
    case DiagnosticLogLevel::Warning:
      return "WARN";
    case DiagnosticLogLevel::Error:
      return "ERROR";
  }
  return "UNKNOWN";
}

}  // namespace

Status SdDiagnosticLogger::start() noexcept {
  if (ready_) {
    return Status::success();
  }

  sdmmc_host_t host = SDSPI_HOST_DEFAULT();
  host.max_freq_khz = kSdFrequencyKhz;
  const spi_host_device_t spiHost = static_cast<spi_host_device_t>(host.slot);

  spi_bus_config_t busConfig{};
  busConfig.mosi_io_num = kSdMosiGpio;
  busConfig.miso_io_num = kSdMisoGpio;
  busConfig.sclk_io_num = kSdClockGpio;
  busConfig.quadwp_io_num = -1;
  busConfig.quadhd_io_num = -1;
  busConfig.max_transfer_sz = 4096;

  esp_err_t result = spi_bus_initialize(spiHost, &busConfig, SDSPI_DEFAULT_DMA);
  if (result != ESP_OK) {
    return Status::failure(Error::StorageUnavailable, static_cast<std::uint32_t>(result));
  }

  sdspi_device_config_t slotConfig = SDSPI_DEVICE_CONFIG_DEFAULT();
  slotConfig.gpio_cs = static_cast<gpio_num_t>(kSdChipSelectGpio);
  slotConfig.host_id = spiHost;

  esp_vfs_fat_mount_config_t mountConfig{};
  mountConfig.format_if_mount_failed = false;
  mountConfig.max_files = 4;
  mountConfig.allocation_unit_size = 16U * 1024U;
  mountConfig.disk_status_check_enable = true;
  mountConfig.use_one_fat = false;

  result = esp_vfs_fat_sdspi_mount(kMountPoint, &host, &slotConfig, &mountConfig, &card_);
  if (result != ESP_OK) {
    spi_bus_free(spiHost);
    return Status::failure(Error::StorageUnavailable, static_cast<std::uint32_t>(result));
  }

  const Status openStatus = log_.open();
  if (!openStatus.ok()) {
    esp_vfs_fat_sdcard_unmount(kMountPoint, card_);
    spi_bus_free(spiHost);
    card_ = nullptr;
    return openStatus;
  }

  queue_ = xQueueCreateStatic(kQueueCapacity, sizeof(LogEntry), queueStorage_, &queueBuffer_);
  if (queue_ == nullptr) {
    log_.close();
    esp_vfs_fat_sdcard_unmount(kMountPoint, card_);
    spi_bus_free(spiHost);
    card_ = nullptr;
    return Status::failure(Error::StorageUnavailable);
  }

  if (xTaskCreateStatic(taskEntry, "sd_diag_log", kTaskStackDepth, this,
                        tskIDLE_PRIORITY + 1, taskStack_, &taskBuffer_) == nullptr) {
    log_.close();
    esp_vfs_fat_sdcard_unmount(kMountPoint, card_);
    spi_bus_free(spiHost);
    card_ = nullptr;
    queue_ = nullptr;
    return Status::failure(Error::StorageUnavailable);
  }

  ready_ = true;
  return Status::success();
}

bool SdDiagnosticLogger::enqueue(DiagnosticLogLevel level, const char* event,
                                 const char* details) noexcept {
  if (!ready_ || queue_ == nullptr || event == nullptr || details == nullptr) {
    ++dropped_;
    return false;
  }

  LogEntry entry{};
  const int length = std::snprintf(
      entry.line, sizeof(entry.line), "mono_us=%lld level=%s event=%s %s",
      static_cast<long long>(esp_timer_get_time()), levelName(level), event, details);
  if (length < 0 || static_cast<std::size_t>(length) >= sizeof(entry.line) ||
      xQueueSend(queue_, &entry, 0) != pdTRUE) {
    ++dropped_;
    return false;
  }
  ++queued_;
  return true;
}

SdDiagnosticLoggerStats SdDiagnosticLogger::stats() const noexcept {
  return {queued_.load(), dropped_.load(), writeFailures_.load(), rotations_.load()};
}

void SdDiagnosticLogger::taskEntry(void* context) noexcept {
  static_cast<SdDiagnosticLogger*>(context)->run();
}

void SdDiagnosticLogger::run() noexcept {
  LogEntry entry{};
  for (;;) {
    if (xQueueReceive(queue_, &entry, portMAX_DELAY) == pdTRUE) {
      if (!log_.appendLine(entry.line).ok()) {
        ++writeFailures_;
      }
      rotations_.store(log_.stats().rotations);
    }
  }
}

}  // namespace remus::computer
