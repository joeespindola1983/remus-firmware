#pragma once

#include <atomic>
#include <cstdint>

#include "SdDiagnosticLogger.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "remus/gps/NmeaDecoder.hpp"
#include "remus/gps/UbxNavigationDecoder.hpp"
#include "remus/platform/esp32/Esp32UartGps.hpp"

namespace remus::computer {

struct GpsServiceStats {
  std::uint64_t bytes{0};
  std::uint32_t recordsQueued{0};
  std::uint32_t recordsDropped{0};
  std::uint32_t recordsSkippedNoStorage{0};
  std::uint32_t coherentEpochs{0};
  std::uint32_t checksumFailures{0};
  std::uint32_t fixTransitions{0};
};

class GpsService {
 public:
  explicit GpsService(SdDiagnosticLogger& storage) noexcept : storage_(storage) {}
  Status start(std::uint64_t computerId, std::uint32_t bootId) noexcept;
  GpsServiceStats stats() const noexcept;

 private:
  static void taskEntry(void* context) noexcept;
  void run() noexcept;
  void configureReceiver() noexcept;
  bool sendUbx(std::uint8_t messageClass, std::uint8_t messageId,
               const std::uint8_t* payload, std::uint16_t payloadLength) noexcept;
  void publishUbx(const gps::NavigationEpoch& epoch,
                  const gps::NmeaSnapshot& nmea) noexcept;
  void publishNmea(const gps::NmeaSnapshot& nmea) noexcept;
  void persistFix(const storage::GpsFixPayload& fix) noexcept;
  void logSolution(const char* source, const storage::GpsFixPayload& fix,
                   std::uint32_t gpsTimeOfWeekMs) noexcept;
  void reportFixState(bool valid, std::uint8_t fixQuality,
                      std::uint8_t satellites) noexcept;

  static constexpr std::uint32_t kTaskStackBytes = 4096;
  SdDiagnosticLogger& storage_;
  platform::esp32::Esp32UartGps uart_{{UART_NUM_1, GPIO_NUM_0, GPIO_NUM_1, 2048}};
  gps::UbxNavigationDecoder ubx_{};
  gps::NmeaDecoder nmea_{};
  std::uint64_t computerId_{0};
  std::uint32_t bootId_{0};
  std::uint32_t sequence_{0};
  std::uint32_t lastNmeaRevision_{0};
  std::uint64_t lastSolutionUs_{0};
  std::uint64_t lastUbxSolutionUs_{0};
  std::uint64_t lastDiagnosticUs_{0};
  bool fixStateKnown_{false};
  bool fixValid_{false};
  bool staleReported_{false};
  StaticTask_t taskBuffer_{};
  StackType_t taskStack_[kTaskStackBytes]{};
  TaskHandle_t task_{nullptr};
  std::atomic<std::uint64_t> bytes_{0};
  std::atomic<std::uint32_t> recordsQueued_{0};
  std::atomic<std::uint32_t> recordsDropped_{0};
  std::atomic<std::uint32_t> recordsSkippedNoStorage_{0};
  std::atomic<std::uint32_t> coherentEpochs_{0};
  std::atomic<std::uint32_t> checksumFailures_{0};
  std::atomic<std::uint32_t> fixTransitions_{0};
};

}  // namespace remus::computer
