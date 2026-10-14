#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "remus/core/Status.hpp"
#include "remus/diagnostics/RotatingTextLog.hpp"
#include "sdmmc_cmd.h"

namespace remus::computer {

enum class DiagnosticLogLevel : std::uint8_t { Info, Warning, Error };

struct SdDiagnosticLoggerStats {
  std::uint32_t queued{0};
  std::uint32_t dropped{0};
  std::uint32_t writeFailures{0};
  std::uint32_t rotations{0};
  std::uint32_t rmsQueued{0};
  std::uint32_t rmsWritten{0};
  std::uint32_t rmsDropped{0};
  std::uint32_t rmsWriteFailures{0};
  std::uint32_t rmsCheckpoints{0};
  std::uint64_t rmsBytes{0};
  std::uint32_t diagnosticQueueHighWater{0};
  std::uint32_t rmsQueueHighWater{0};
};

class SdDiagnosticLogger {
 public:
  Status start() noexcept;
  Status startRecording(std::uint64_t recordingId, std::uint64_t computerId,
                        std::uint32_t pcBootId, std::uint32_t networkId,
                        std::uint32_t firmwareHash, std::uint32_t profileHash) noexcept;
  bool enqueue(DiagnosticLogLevel level, const char* event, const char* details) noexcept;
  bool enqueueImuBatch(std::uint64_t originId, std::uint32_t originBootId,
                       std::uint32_t batchSeq, std::uint64_t originMonoUs,
                       std::uint64_t ingestPcMonoUs, std::int8_t rssiDbm,
                       const std::uint8_t* dataBatchPayload,
                       std::size_t dataBatchPayloadSize) noexcept;
  bool enqueueSourceManifest(std::uint64_t originId, std::uint32_t originBootId,
                             const std::uint8_t* manifestPayload,
                             std::size_t manifestPayloadSize) noexcept;
  SdDiagnosticLoggerStats stats() const noexcept;
  bool isReady() const noexcept { return ready_; }
  bool isRecording() const noexcept { return rmsHealthy_.load(); }

 private:
  static constexpr std::size_t kDiagnosticQueueCapacity = 16;
  static constexpr std::size_t kRmsQueueCapacity = 96;
  static constexpr std::size_t kLineCapacity = 640;
  static constexpr std::uint32_t kTaskStackBytes = 8192;

  enum class RmsEntryKind : std::uint8_t { ImuBatch, SourceManifest };
  struct ImuBatchEntry {
    std::uint64_t originId;
    std::uint32_t originBootId;
    std::uint32_t batchSeq;
    std::uint64_t originMonoUs;
    std::uint64_t ingestPcMonoUs;
    std::int8_t rssiDbm;
    std::uint8_t payloadSize;
    std::uint8_t payload[176];
  };
  struct DiagnosticEntry {
    char line[kLineCapacity]{};
  };
  struct RmsEntry {
    RmsEntryKind kind{RmsEntryKind::ImuBatch};
    union {
      ImuBatchEntry imu;
      struct {
        std::uint64_t originId;
        std::uint32_t originBootId;
        std::uint8_t payload[34];
      } manifest;
    };
    RmsEntry() noexcept : imu{} {}
  };

  static void taskEntry(void* context) noexcept;
  void run() noexcept;

  diagnostics::RotatingTextLog log_{{"/sdcard/remus_pc.log",
                                     "/sdcard/remus_pc.log.1", 512U * 1024U,
                                     kLineCapacity - 1, false}};
  sdmmc_card_t* card_{nullptr};
  FILE* rmsFile_{nullptr};
  std::uint64_t rmsOrdinal_{0};
  std::uint64_t rmsBytesSinceSync_{0};
  std::int64_t rmsLastSyncUs_{0};
  QueueHandle_t diagnosticQueue_{nullptr};
  StaticQueue_t diagnosticQueueBuffer_{};
  std::uint8_t diagnosticQueueStorage_[kDiagnosticQueueCapacity * sizeof(DiagnosticEntry)]{};
  QueueHandle_t rmsQueue_{nullptr};
  StaticQueue_t rmsQueueBuffer_{};
  std::uint8_t rmsQueueStorage_[kRmsQueueCapacity * sizeof(RmsEntry)]{};
  SemaphoreHandle_t filesystemMutex_{nullptr};
  StaticSemaphore_t filesystemMutexBuffer_{};
  StaticTask_t taskBuffer_{};
  StackType_t taskStack_[kTaskStackBytes]{};
  TaskHandle_t task_{nullptr};
  std::atomic<std::uint32_t> queued_{0};
  std::atomic<std::uint32_t> dropped_{0};
  std::atomic<std::uint32_t> writeFailures_{0};
  std::atomic<std::uint32_t> rotations_{0};
  std::atomic<std::uint32_t> rmsQueued_{0};
  std::atomic<std::uint32_t> rmsWritten_{0};
  std::atomic<std::uint32_t> rmsDropped_{0};
  std::atomic<std::uint32_t> rmsWriteFailures_{0};
  std::atomic<std::uint32_t> rmsCheckpoints_{0};
  std::atomic<std::uint64_t> rmsBytes_{0};
  std::atomic<std::uint32_t> diagnosticQueueHighWater_{0};
  std::atomic<std::uint32_t> rmsQueueHighWater_{0};
  std::atomic<bool> rmsHealthy_{false};
  bool ready_{false};
};

}  // namespace remus::computer
