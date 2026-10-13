#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
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
};

class SdDiagnosticLogger {
 public:
  Status start() noexcept;
  bool enqueue(DiagnosticLogLevel level, const char* event, const char* details) noexcept;
  SdDiagnosticLoggerStats stats() const noexcept;
  bool isReady() const noexcept { return ready_; }

 private:
  static constexpr std::size_t kQueueCapacity = 32;
  static constexpr std::size_t kLineCapacity = 384;
  static constexpr std::uint32_t kTaskStackDepth = 2048;

  struct LogEntry {
    char line[kLineCapacity]{};
  };

  static void taskEntry(void* context) noexcept;
  void run() noexcept;

  diagnostics::RotatingTextLog log_{{"/sdcard/remus_pc.log",
                                     "/sdcard/remus_pc.log.1", 512U * 1024U,
                                     kLineCapacity - 1}};
  sdmmc_card_t* card_{nullptr};
  QueueHandle_t queue_{nullptr};
  StaticQueue_t queueBuffer_{};
  std::uint8_t queueStorage_[kQueueCapacity * sizeof(LogEntry)]{};
  StaticTask_t taskBuffer_{};
  StackType_t taskStack_[kTaskStackDepth]{};
  std::atomic<std::uint32_t> queued_{0};
  std::atomic<std::uint32_t> dropped_{0};
  std::atomic<std::uint32_t> writeFailures_{0};
  std::atomic<std::uint32_t> rotations_{0};
  bool ready_{false};
};

}  // namespace remus::computer
