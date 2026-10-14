#pragma once

#include <array>
#include <atomic>

#include "SdDiagnosticLogger.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "remus/hal/IRadio.hpp"
#include "remus/link/FrameCodec.hpp"
#include "remus/link/KeyDerivation.hpp"
#include "remus/link/RadioSupervisor.hpp"
#include "remus/link/SourceRegistry.hpp"
#include "remus/link/TxScheduler.hpp"
#include "remus/platform/esp32/EspNowDriver.hpp"
#include "remus/platform/esp32/MbedTlsCrypto.hpp"
#include "remus/platform/esp32/NvsCredentials.hpp"

namespace remus::computer {
class ComputerRadioService final : public hal::IRadioEvents {
 public:
  ComputerRadioService(SdDiagnosticLogger* diagnosticLog, RecordingId recordingId) noexcept
      : diagnosticLog_(diagnosticLog), recordingId_(recordingId) {}
  Status start() noexcept;
  RecordingId recordingId() const noexcept { return recordingId_; }
  ComputerId computerId() const noexcept { return computerId_; }
  DeviceBootId bootId() const noexcept { return bootId_; }
  NetworkId networkId() const noexcept { return networkId_; }
  void setCaptureAllowed(bool allowed) noexcept;
  void onRadioReceived(const hal::RadioRxMeta&, const std::uint8_t*, std::size_t) noexcept override;
  void onRadioSendComplete(const MacAddress&, bool) noexcept override;
  std::size_t sourceCount() const noexcept { return registry_.size(); }

 private:
  enum class EventKind : std::uint8_t { Receive, SendComplete, CaptureEnabled };
  struct Event {
    EventKind kind{};
    hal::RadioRxMeta meta{};
    MacAddress destination{};
    std::uint16_t size{};
    bool delivered{};
    std::uint8_t bytes[250]{};
  };
  static constexpr std::size_t kQueueCapacity = 32;
  static void taskEntry(void*) noexcept;
  void run() noexcept;
  void handleReceive(const Event&) noexcept;
  void handleHello(const Event&, const link::DecodedFrame&) noexcept;
  void handlePaired(const Event&) noexcept;
  void handleBindAck(link::SourceEntry&, const link::DecodedFrame&) noexcept;
  void handleDataBatch(link::SourceEntry&, const Event&, const link::DecodedFrame&) noexcept;
  void handleSourceManifest(link::SourceEntry&, const link::DecodedFrame&) noexcept;
  void sendBeacon() noexcept;
  void sendBind(link::SourceEntry&) noexcept;
  bool enqueue(const Event&) noexcept;
  void log(DiagnosticLogLevel, const char*, const char*) noexcept;

  SdDiagnosticLogger* diagnosticLog_{nullptr};
  platform::esp32::MbedTlsCrypto crypto_{};
  platform::esp32::NvsCredentials credentials_{};
  platform::esp32::EspNowDriver radio_{};
  link::FrameCodec codec_{crypto_};
  link::TxScheduler tx_{radio_};
  link::SourceRegistry registry_{};
  link::RadioSupervisor supervisor_{};
  ComputerId computerId_{0};
  DeviceBootId bootId_{0};
  NetworkId networkId_{0};
  RecordingId recordingId_{0};
  struct DataSinkStats {
    BladeId bladeId{0};
    DeviceBootId bladeBootId{0};
    std::uint32_t nextBatchSeq{0};
    SampleIndex nextSampleIndex{0};
    std::uint64_t batches{0};
    std::uint64_t samples{0};
    std::uint32_t gaps{0};
  };
  std::array<DataSinkStats, link::SourceRegistry::capacity()> dataSinks_{};
  std::array<std::uint8_t, 32> secret_{};
  std::array<std::uint8_t, 32> groupKey_{};
  std::uint32_t beaconSeq_{0};
  std::uint32_t messageSeq_{0};
  QueueHandle_t queue_{nullptr};
  StaticQueue_t queueBuffer_{};
  std::uint8_t queueStorage_[kQueueCapacity * sizeof(Event)]{};
  StaticTask_t taskBuffer_{};
  StackType_t taskStack_[5120]{};
  TaskHandle_t task_{nullptr};
  std::atomic<bool> captureAllowed_{false};
};
}  // namespace remus::computer
