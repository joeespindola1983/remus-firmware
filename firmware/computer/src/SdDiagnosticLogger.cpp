#include "SdDiagnosticLogger.hpp"

#include <cstdio>
#include <cstring>
#include <unistd.h>

#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "remus/storage/Rms1Codec.hpp"

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

namespace {
void put16(std::uint8_t* p, std::uint16_t value) noexcept {
  p[0] = static_cast<std::uint8_t>(value);
  p[1] = static_cast<std::uint8_t>(value >> 8);
}
void put64(std::uint8_t* p, std::uint64_t value) noexcept {
  for (unsigned index = 0; index < 8; ++index)
    p[index] = static_cast<std::uint8_t>(value >> (index * 8));
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

  // ESP-IDF expresses task stack size in bytes and ESP32-C3 defines
  // StackType_t as uint8_t. FAT/stdio plus the bounded RMS1 encode buffers
  // exceed 2 KiB, so reserve the complete 8 KiB backing buffer.
  if (xTaskCreateStatic(taskEntry, "sd_diag_log", kTaskStackBytes, this,
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
  entry.kind = EntryKind::Diagnostic;
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

Status SdDiagnosticLogger::startRecording(std::uint64_t recordingId,
                                          std::uint64_t computerId,
                                          std::uint32_t pcBootId,
                                          std::uint32_t networkId,
                                          std::uint32_t firmwareHash,
                                          std::uint32_t profileHash) noexcept {
  if (!ready_ || rmsFile_ != nullptr || recordingId == 0 || computerId == 0 || pcBootId == 0)
    return Status::failure(Error::InvalidArgument);
  char path[64]{};
  std::snprintf(path, sizeof(path), "/sdcard/RM_%016llX.rms",
                static_cast<unsigned long long>(recordingId));
  rmsFile_ = std::fopen(path, "wb");
  if (rmsFile_ == nullptr) return Status::failure(Error::StorageUnavailable);
  storage::Rms1SessionHeader header{recordingId,
                                    computerId,
                                    pcBootId,
                                    static_cast<std::uint64_t>(esp_timer_get_time()),
                                    firmwareHash,
                                    profileHash,
                                    0,
                                    networkId,
                                    0};
  std::uint8_t encoded[storage::kRms1SessionHeaderBytes]{};
  Status status = storage::encodeSessionHeader(header, encoded, sizeof(encoded));
  if (!status.ok() || std::fwrite(encoded, 1, sizeof(encoded), rmsFile_) != sizeof(encoded) ||
      std::fflush(rmsFile_) != 0 || fsync(fileno(rmsFile_)) != 0) {
    std::fclose(rmsFile_);
    rmsFile_ = nullptr;
    return Status::failure(Error::IoFailure);
  }
  rmsBytes_.store(sizeof(encoded));
  rmsLastSyncUs_ = esp_timer_get_time();
  rmsHealthy_.store(true);
  return Status::success();
}

bool SdDiagnosticLogger::enqueueImuBatch(std::uint64_t originId,
                                         std::uint32_t originBootId,
                                         std::uint32_t batchSeq,
                                         std::uint64_t originMonoUs,
                                         std::uint64_t ingestPcMonoUs,
                                         std::int8_t rssiDbm,
                                         const std::uint8_t* payload,
                                         std::size_t payloadSize) noexcept {
  if (!rmsHealthy_.load() || queue_ == nullptr || originId == 0 || originBootId == 0 ||
      payload == nullptr || payloadSize == 0 || payloadSize > 176) {
    ++rmsDropped_;
    return false;
  }
  LogEntry entry{};
  entry.kind = EntryKind::ImuBatch;
  entry.imu.originId = originId;
  entry.imu.originBootId = originBootId;
  entry.imu.batchSeq = batchSeq;
  entry.imu.originMonoUs = originMonoUs;
  entry.imu.ingestPcMonoUs = ingestPcMonoUs;
  entry.imu.rssiDbm = rssiDbm;
  entry.imu.payloadSize = static_cast<std::uint8_t>(payloadSize);
  std::memcpy(entry.imu.payload, payload, payloadSize);
  if (xQueueSend(queue_, &entry, 0) != pdTRUE) {
    ++rmsDropped_;
    return false;
  }
  ++rmsQueued_;
  return true;
}

bool SdDiagnosticLogger::enqueueSourceManifest(std::uint64_t originId,
                                               std::uint32_t originBootId,
                                               const std::uint8_t* payload,
                                               std::size_t payloadSize) noexcept {
  if (!rmsHealthy_.load() || queue_ == nullptr || originId == 0 || originBootId == 0 ||
      payload == nullptr || payloadSize != 34) {
    ++rmsDropped_;
    return false;
  }
  LogEntry entry{};
  entry.kind = EntryKind::SourceManifest;
  entry.manifest.originId = originId;
  entry.manifest.originBootId = originBootId;
  std::memcpy(entry.manifest.payload, payload, payloadSize);
  if (xQueueSend(queue_, &entry, 0) != pdTRUE) {
    ++rmsDropped_;
    return false;
  }
  ++rmsQueued_;
  return true;
}

SdDiagnosticLoggerStats SdDiagnosticLogger::stats() const noexcept {
  return {queued_.load(), dropped_.load(), writeFailures_.load(), rotations_.load(),
          rmsQueued_.load(), rmsWritten_.load(), rmsDropped_.load(),
          rmsWriteFailures_.load(), rmsCheckpoints_.load(), rmsBytes_.load()};
}

void SdDiagnosticLogger::taskEntry(void* context) noexcept {
  static_cast<SdDiagnosticLogger*>(context)->run();
}

void SdDiagnosticLogger::run() noexcept {
  LogEntry entry{};
  for (;;) {
    if (xQueueReceive(queue_, &entry, portMAX_DELAY) == pdTRUE) {
      if (entry.kind == EntryKind::Diagnostic) {
        if (!log_.appendLine(entry.line).ok()) ++writeFailures_;
        rotations_.store(log_.stats().rotations);
        continue;
      }
      std::uint8_t payload[storage::kRms1MaximumPayloadBytes]{};
      if (entry.kind == EntryKind::SourceManifest) {
        std::memcpy(payload, entry.manifest.payload, 34);
        storage::Rms1Record record{storage::Rms1RecordType::SourceManifest,
                                   ++rmsOrdinal_,
                                   static_cast<std::uint64_t>(esp_timer_get_time()),
                                   entry.manifest.originId,
                                   entry.manifest.originBootId,
                                   0,
                                   payload,
                                   38};
        std::uint8_t encoded[storage::kRms1MaximumRecordBytes]{};
        std::size_t encodedSize = 0;
        if (!rmsHealthy_.load() || rmsFile_ == nullptr ||
            !storage::encodeRecord(record, encoded, sizeof(encoded), encodedSize).ok() ||
            std::fwrite(encoded, 1, encodedSize, rmsFile_) != encodedSize) {
          ++rmsWriteFailures_;
          rmsHealthy_.store(false);
          continue;
        }
        ++rmsWritten_;
        rmsBytes_.fetch_add(encodedSize);
        rmsBytesSinceSync_ += encodedSize;
        continue;
      }
      put64(payload, entry.imu.originMonoUs);
      put64(payload + 8, entry.imu.ingestPcMonoUs);
      payload[16] = static_cast<std::uint8_t>(entry.imu.rssiDbm);
      payload[17] = 0;
      put16(payload + 18, 0);
      std::memcpy(payload + 20, entry.imu.payload, entry.imu.payloadSize);
      storage::Rms1Record record{storage::Rms1RecordType::ImuBatch,
                                 ++rmsOrdinal_,
                                 static_cast<std::uint64_t>(esp_timer_get_time()),
                                 entry.imu.originId,
                                 entry.imu.originBootId,
                                 entry.imu.batchSeq,
                                 payload,
                                 static_cast<std::uint32_t>(20 + entry.imu.payloadSize)};
      std::uint8_t encoded[storage::kRms1MaximumRecordBytes]{};
      std::size_t encodedSize = 0;
      if (!rmsHealthy_.load() || rmsFile_ == nullptr ||
          !storage::encodeRecord(record, encoded, sizeof(encoded), encodedSize).ok() ||
          std::fwrite(encoded, 1, encodedSize, rmsFile_) != encodedSize) {
        ++rmsWriteFailures_;
        rmsHealthy_.store(false);
        continue;
      }
      ++rmsWritten_;
      rmsBytes_.fetch_add(encodedSize);
      rmsBytesSinceSync_ += encodedSize;
      const std::int64_t now = esp_timer_get_time();
      if (rmsBytesSinceSync_ >= 32U * 1024U || now - rmsLastSyncUs_ >= 1000000) {
        if (std::fflush(rmsFile_) != 0 || fsync(fileno(rmsFile_)) != 0) {
          ++rmsWriteFailures_;
          rmsHealthy_.store(false);
        } else {
          ++rmsCheckpoints_;
        }
        rmsBytesSinceSync_ = 0;
        rmsLastSyncUs_ = now;
      }
    }
  }
}

}  // namespace remus::computer
