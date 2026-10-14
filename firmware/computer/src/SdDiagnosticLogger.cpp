#include "SdDiagnosticLogger.hpp"

#include <cerrno>
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

#if !defined(CONFIG_FATFS_LFN_HEAP) && !defined(CONFIG_FATFS_LFN_STACK)
#error "Remus Computer requires FatFs long filename support for RMS1 and log rotation"
#endif

namespace remus::computer {

namespace {

constexpr char kMountPoint[] = "/sdcard";
constexpr int kSdMisoGpio = 8;
constexpr int kSdMosiGpio = 10;
constexpr int kSdClockGpio = 20;
constexpr int kSdChipSelectGpio = 21;
constexpr int kSdFrequencyKhz = 10000;
constexpr std::int64_t kSyncIntervalUs = 1000000;
constexpr std::size_t kRmsBurstLimit = 24;

void updateHighWater(std::atomic<std::uint32_t>& highWater,
                     UBaseType_t currentDepth) noexcept {
  std::uint32_t observed = highWater.load(std::memory_order_relaxed);
  const auto depth = static_cast<std::uint32_t>(currentDepth);
  while (depth > observed &&
         !highWater.compare_exchange_weak(observed, depth, std::memory_order_relaxed)) {
  }
}

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
void put32(std::uint8_t* p, std::uint32_t value) noexcept {
  for (unsigned index = 0; index < 4; ++index)
    p[index] = static_cast<std::uint8_t>(value >> (index * 8));
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

  diagnosticQueue_ = xQueueCreateStatic(kDiagnosticQueueCapacity, sizeof(DiagnosticEntry),
                                        diagnosticQueueStorage_, &diagnosticQueueBuffer_);
  rmsQueue_ = xQueueCreateStatic(kRmsQueueCapacity, sizeof(RmsEntry), rmsQueueStorage_,
                                 &rmsQueueBuffer_);
  gpsQueue_ = xQueueCreateStatic(kGpsQueueCapacity, sizeof(GpsEntry), gpsQueueStorage_,
                                 &gpsQueueBuffer_);
  if (diagnosticQueue_ == nullptr || rmsQueue_ == nullptr || gpsQueue_ == nullptr) {
    log_.close();
    esp_vfs_fat_sdcard_unmount(kMountPoint, card_);
    spi_bus_free(spiHost);
    card_ = nullptr;
    return Status::failure(Error::StorageUnavailable);
  }

  filesystemMutex_ = xSemaphoreCreateMutexStatic(&filesystemMutexBuffer_);
  if (filesystemMutex_ == nullptr) {
    log_.close();
    esp_vfs_fat_sdcard_unmount(kMountPoint, card_);
    spi_bus_free(spiHost);
    card_ = nullptr;
    diagnosticQueue_ = nullptr;
    rmsQueue_ = nullptr;
    gpsQueue_ = nullptr;
    return Status::failure(Error::StorageUnavailable);
  }

  // ESP-IDF expresses task stack size in bytes and ESP32-C3 defines
  // StackType_t as uint8_t. FAT/stdio plus the bounded RMS1 encode buffers
  // exceed 2 KiB, so reserve the complete 8 KiB backing buffer.
  task_ = xTaskCreateStatic(taskEntry, "sd_writer", kTaskStackBytes, this, 7,
                            taskStack_, &taskBuffer_);
  if (task_ == nullptr) {
    log_.close();
    esp_vfs_fat_sdcard_unmount(kMountPoint, card_);
    spi_bus_free(spiHost);
    card_ = nullptr;
    diagnosticQueue_ = nullptr;
    rmsQueue_ = nullptr;
    gpsQueue_ = nullptr;
    filesystemMutex_ = nullptr;
    return Status::failure(Error::StorageUnavailable);
  }

  ready_ = true;
  return Status::success();
}

bool SdDiagnosticLogger::enqueue(DiagnosticLogLevel level, const char* event,
                                 const char* details) noexcept {
  if (!ready_ || diagnosticQueue_ == nullptr || event == nullptr || details == nullptr) {
    ++dropped_;
    return false;
  }

  DiagnosticEntry entry{};
  const int length = std::snprintf(
      entry.line, sizeof(entry.line), "mono_us=%lld level=%s event=%s %s",
      static_cast<long long>(esp_timer_get_time()), levelName(level), event, details);
  if (length < 0 || static_cast<std::size_t>(length) >= sizeof(entry.line) ||
      xQueueSend(diagnosticQueue_, &entry, 0) != pdTRUE) {
    ++dropped_;
    return false;
  }
  ++queued_;
  updateHighWater(diagnosticQueueHighWater_, uxQueueMessagesWaiting(diagnosticQueue_));
  if (task_ != nullptr) xTaskNotifyGive(task_);
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
  if (filesystemMutex_ == nullptr ||
      xSemaphoreTake(filesystemMutex_, portMAX_DELAY) != pdTRUE) {
    return Status::failure(Error::Busy);
  }
  rmsFile_ = std::fopen(path, "wb");
  if (rmsFile_ == nullptr) {
    const std::uint32_t detail = static_cast<std::uint32_t>(errno);
    xSemaphoreGive(filesystemMutex_);
    return Status::failure(Error::StorageUnavailable, detail);
  }
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
    const std::uint32_t detail = static_cast<std::uint32_t>(errno);
    std::fclose(rmsFile_);
    rmsFile_ = nullptr;
    xSemaphoreGive(filesystemMutex_);
    return Status::failure(Error::IoFailure, detail);
  }
  rmsBytes_.store(sizeof(encoded));
  rmsLastSyncUs_ = esp_timer_get_time();
  rmsHealthy_.store(true);
  xSemaphoreGive(filesystemMutex_);
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
  if (!rmsHealthy_.load() || rmsQueue_ == nullptr || originId == 0 || originBootId == 0 ||
      payload == nullptr || payloadSize == 0 || payloadSize > 176) {
    ++rmsDropped_;
    return false;
  }
  RmsEntry entry{};
  entry.kind = RmsEntryKind::ImuBatch;
  entry.imu.originId = originId;
  entry.imu.originBootId = originBootId;
  entry.imu.batchSeq = batchSeq;
  entry.imu.originMonoUs = originMonoUs;
  entry.imu.ingestPcMonoUs = ingestPcMonoUs;
  entry.imu.rssiDbm = rssiDbm;
  entry.imu.payloadSize = static_cast<std::uint8_t>(payloadSize);
  std::memcpy(entry.imu.payload, payload, payloadSize);
  if (xQueueSend(rmsQueue_, &entry, 0) != pdTRUE) {
    ++rmsDropped_;
    return false;
  }
  ++rmsQueued_;
  updateHighWater(rmsQueueHighWater_, uxQueueMessagesWaiting(rmsQueue_));
  if (task_ != nullptr) xTaskNotifyGive(task_);
  return true;
}

bool SdDiagnosticLogger::enqueueSourceManifest(std::uint64_t originId,
                                               std::uint32_t originBootId,
                                               const std::uint8_t* payload,
                                               std::size_t payloadSize) noexcept {
  if (!rmsHealthy_.load() || rmsQueue_ == nullptr || originId == 0 || originBootId == 0 ||
      payload == nullptr || payloadSize != 34) {
    ++rmsDropped_;
    return false;
  }
  RmsEntry entry{};
  entry.kind = RmsEntryKind::SourceManifest;
  entry.manifest.originId = originId;
  entry.manifest.originBootId = originBootId;
  std::memcpy(entry.manifest.payload, payload, payloadSize);
  if (xQueueSend(rmsQueue_, &entry, 0) != pdTRUE) {
    ++rmsDropped_;
    return false;
  }
  ++rmsQueued_;
  updateHighWater(rmsQueueHighWater_, uxQueueMessagesWaiting(rmsQueue_));
  if (task_ != nullptr) xTaskNotifyGive(task_);
  return true;
}

bool SdDiagnosticLogger::enqueueGap(std::uint64_t originId,
                                    std::uint32_t originBootId,
                                    std::uint32_t fromBatchSeq,
                                    std::uint32_t throughBatchSeq,
                                    std::uint64_t firstSampleIndex,
                                    std::uint16_t reason) noexcept {
  if (!rmsHealthy_.load() || rmsQueue_ == nullptr || originId == 0 || originBootId == 0 ||
      fromBatchSeq > throughBatchSeq) {
    ++rmsDropped_;
    return false;
  }
  RmsEntry entry{};
  entry.kind = RmsEntryKind::Gap;
  entry.gap = {originId, originBootId, fromBatchSeq, throughBatchSeq,
               firstSampleIndex, reason};
  if (xQueueSend(rmsQueue_, &entry, 0) != pdTRUE) {
    ++rmsDropped_;
    return false;
  }
  ++rmsQueued_;
  updateHighWater(rmsQueueHighWater_, uxQueueMessagesWaiting(rmsQueue_));
  if (task_ != nullptr) xTaskNotifyGive(task_);
  return true;
}

bool SdDiagnosticLogger::enqueueGpsFix(std::uint64_t originId,
                                       std::uint32_t originBootId,
                                       std::uint32_t sequence,
                                       const storage::GpsFixPayload& fix) noexcept {
  if (!rmsHealthy_.load() || gpsQueue_ == nullptr || originId == 0 || originBootId == 0) {
    ++gpsDropped_;
    return false;
  }
  const GpsEntry entry{originId, originBootId, sequence, fix};
  if (xQueueSend(gpsQueue_, &entry, 0) != pdTRUE) {
    ++gpsDropped_;
    return false;
  }
  ++gpsQueued_;
  updateHighWater(gpsQueueHighWater_, uxQueueMessagesWaiting(gpsQueue_));
  if (task_ != nullptr) xTaskNotifyGive(task_);
  return true;
}

SdDiagnosticLoggerStats SdDiagnosticLogger::stats() const noexcept {
  return {queued_.load(), dropped_.load(), writeFailures_.load(), rotations_.load(),
          rmsQueued_.load(), rmsWritten_.load(), rmsDropped_.load(),
          rmsWriteFailures_.load(), rmsCheckpoints_.load(), rmsBytes_.load(),
          diagnosticQueueHighWater_.load(), rmsQueueHighWater_.load(),
          gpsQueued_.load(), gpsWritten_.load(), gpsDropped_.load(),
          gpsQueueHighWater_.load()};
}

bool SdDiagnosticLogger::checkpointedThrough(std::uint64_t originId,
                                             std::uint32_t originBootId,
                                             std::uint32_t& batchSeq) const noexcept {
  for (const auto& progress : checkpointProgress_) {
    if (progress.originId.load(std::memory_order_acquire) == originId &&
        progress.originBootId.load(std::memory_order_relaxed) == originBootId &&
        progress.publishedValid.load(std::memory_order_acquire)) {
      batchSeq = progress.publishedThrough.load(std::memory_order_relaxed);
      return true;
    }
  }
  return false;
}

void SdDiagnosticLogger::markBatchWritten(std::uint64_t originId,
                                          std::uint32_t originBootId,
                                          std::uint32_t batchSeq) noexcept {
  CheckpointProgress* selected = nullptr;
  for (auto& progress : checkpointProgress_) {
    const auto existingId = progress.originId.load(std::memory_order_relaxed);
    if (existingId == originId &&
        progress.originBootId.load(std::memory_order_relaxed) == originBootId) {
      selected = &progress;
      break;
    }
    if (existingId == 0 && selected == nullptr) selected = &progress;
  }
  if (selected == nullptr) return;
  if (selected->originId.load(std::memory_order_relaxed) == 0) {
    selected->originBootId.store(originBootId, std::memory_order_relaxed);
    selected->originId.store(originId, std::memory_order_release);
  }
  if (selected->writtenValid && batchSeq <= selected->writtenThrough) return;
  const std::uint32_t base = selected->writtenValid ? selected->writtenThrough + 1U : 0U;
  const std::uint32_t offset = batchSeq - base;
  if (offset >= 32) return;
  selected->writtenBitmap |= std::uint32_t{1} << offset;
  while ((selected->writtenBitmap & 1U) != 0) {
    selected->writtenThrough = selected->writtenValid ? selected->writtenThrough + 1U : 0U;
    selected->writtenValid = true;
    selected->writtenBitmap >>= 1U;
  }
}

void SdDiagnosticLogger::publishCheckpoints() noexcept {
  for (auto& progress : checkpointProgress_) {
    if (!progress.writtenValid) continue;
    progress.publishedThrough.store(progress.writtenThrough, std::memory_order_relaxed);
    progress.publishedValid.store(true, std::memory_order_release);
  }
}

void SdDiagnosticLogger::taskEntry(void* context) noexcept {
  static_cast<SdDiagnosticLogger*>(context)->run();
}

void SdDiagnosticLogger::run() noexcept {
  DiagnosticEntry diagnostic{};
  RmsEntry entry{};
  GpsEntry gpsEntry{};
  bool diagnosticDirty = false;
  std::int64_t diagnosticLastSyncUs = esp_timer_get_time();
  for (;;) {
    bool processedRms = false;
    for (std::size_t burst = 0; burst < kRmsBurstLimit; ++burst) {
      if (xQueueReceive(rmsQueue_, &entry, 0) != pdTRUE) break;
      processedRms = true;
      std::uint8_t payload[storage::kRms1MaximumPayloadBytes]{};
      if (entry.kind == RmsEntryKind::SourceManifest) {
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
      if (entry.kind == RmsEntryKind::Gap) {
        // GAP_NOTICE v1 carries the first missing sample but not the final
        // sample index. Preserve that uncertainty explicitly: UINT64_MAX plus
        // flag bit 0 means lastSampleIndex is unavailable, never fabricated.
        put32(payload, entry.gap.fromBatchSeq);
        put32(payload + 4, entry.gap.throughBatchSeq);
        put64(payload + 8, entry.gap.firstSampleIndex);
        put64(payload + 16, UINT64_MAX);
        put16(payload + 24, entry.gap.reason);
        put16(payload + 26, 1);
        storage::Rms1Record record{storage::Rms1RecordType::Gap,
                                   ++rmsOrdinal_,
                                   static_cast<std::uint64_t>(esp_timer_get_time()),
                                   entry.gap.originId,
                                   entry.gap.originBootId,
                                   entry.gap.fromBatchSeq,
                                   payload,
                                   28};
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
        for (std::uint32_t sequence = entry.gap.fromBatchSeq;; ++sequence) {
          markBatchWritten(entry.gap.originId, entry.gap.originBootId, sequence);
          if (sequence == entry.gap.throughBatchSeq) break;
        }
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
      markBatchWritten(entry.imu.originId, entry.imu.originBootId, entry.imu.batchSeq);
    }

    // GPS has its own bounded queue and receives at most one write opportunity
    // after each IMU burst. It can be dropped under pressure, but can never
    // occupy the IMU/recovery queue or delay its admission path.
    if (xQueueReceive(gpsQueue_, &gpsEntry, 0) == pdTRUE) {
      std::uint8_t payload[storage::kGpsFixPayloadBytes]{};
      std::uint8_t encoded[storage::kRms1MaximumRecordBytes]{};
      std::size_t encodedSize = 0;
      if (!storage::encodeGpsFixPayload(gpsEntry.fix, payload, sizeof(payload)).ok()) {
        ++gpsDropped_;
      } else {
        const storage::Rms1Record record{
            storage::Rms1RecordType::GpsFix,
            ++rmsOrdinal_,
            gpsEntry.fix.pcMonotonicUs,
            gpsEntry.originId,
            gpsEntry.originBootId,
            gpsEntry.sequence,
            payload,
            static_cast<std::uint32_t>(sizeof(payload))};
        if (!rmsHealthy_.load() || rmsFile_ == nullptr ||
            !storage::encodeRecord(record, encoded, sizeof(encoded), encodedSize).ok() ||
            std::fwrite(encoded, 1, encodedSize, rmsFile_) != encodedSize) {
          ++rmsWriteFailures_;
          rmsHealthy_.store(false);
        } else {
          ++gpsWritten_;
          ++rmsWritten_;
          rmsBytes_.fetch_add(encodedSize);
          rmsBytesSinceSync_ += encodedSize;
        }
      }
    }

    if (xQueueReceive(diagnosticQueue_, &diagnostic, 0) == pdTRUE) {
        Status writeStatus = Status::failure(Error::Busy);
        if (filesystemMutex_ != nullptr &&
            xSemaphoreTake(filesystemMutex_, portMAX_DELAY) == pdTRUE) {
          writeStatus = log_.appendLine(diagnostic.line);
          xSemaphoreGive(filesystemMutex_);
        }
        if (!writeStatus.ok()) ++writeFailures_;
        else diagnosticDirty = true;
        rotations_.store(log_.stats().rotations);
    }

    const std::int64_t now = esp_timer_get_time();
    if (rmsHealthy_.load() && rmsFile_ != nullptr && rmsBytesSinceSync_ > 0 &&
        (rmsBytesSinceSync_ >= 32U * 1024U || now - rmsLastSyncUs_ >= kSyncIntervalUs)) {
        if (std::fflush(rmsFile_) != 0 || fsync(fileno(rmsFile_)) != 0) {
          ++rmsWriteFailures_;
          rmsHealthy_.store(false);
        } else {
          ++rmsCheckpoints_;
          publishCheckpoints();
        }
        rmsBytesSinceSync_ = 0;
        rmsLastSyncUs_ = now;
    }
    if (diagnosticDirty && now - diagnosticLastSyncUs >= kSyncIntervalUs) {
      Status syncStatus = Status::failure(Error::Busy);
      if (filesystemMutex_ != nullptr &&
          xSemaphoreTake(filesystemMutex_, portMAX_DELAY) == pdTRUE) {
        syncStatus = log_.sync();
        xSemaphoreGive(filesystemMutex_);
      }
      if (!syncStatus.ok()) ++writeFailures_;
      else diagnosticDirty = false;
      diagnosticLastSyncUs = now;
    }

    if (!processedRms && uxQueueMessagesWaiting(diagnosticQueue_) == 0 &&
        uxQueueMessagesWaiting(rmsQueue_) == 0 && uxQueueMessagesWaiting(gpsQueue_) == 0) {
      ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(100));
    }
  }
}

}  // namespace remus::computer
