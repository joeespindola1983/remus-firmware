#pragma once

#include <array>
#include <atomic>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "remus/hal/IRadio.hpp"
#include "remus/link/BladeLinkStateMachine.hpp"
#include "remus/link/FrameCodec.hpp"
#include "remus/link/KeyDerivation.hpp"
#include "remus/link/MessageCodec.hpp"
#include "remus/link/RadioSupervisor.hpp"
#include "remus/link/ReplayWindow.hpp"
#include "remus/link/TxScheduler.hpp"
#include "remus/platform/esp32/EspNowDriver.hpp"
#include "remus/platform/esp32/MbedTlsCrypto.hpp"
#include "remus/platform/esp32/NvsCredentials.hpp"

namespace remus::blade {
class BladeAcquisitionService;
struct BladeReplayStats {
  std::uint32_t depth{0};
  std::uint32_t capacity{0};
  std::uint32_t overflows{0};
  std::uint32_t ackWindows{0};
  std::uint32_t replayQueued{0};
  std::uint32_t replayUnavailable{0};
  std::uint32_t gapNotices{0};
};
class BladeRadioService final : public hal::IRadioEvents {
 public:
  Status start() noexcept;
  void setAcquisitionService(BladeAcquisitionService* service) noexcept { acquisition_ = service; }
  Status queueDataBatch(RecordingId, NativeTimestampUs, const link::DataBatch&) noexcept;
  void onRadioReceived(const hal::RadioRxMeta&, const std::uint8_t*, std::size_t) noexcept override;
  void onRadioSendComplete(const MacAddress&, bool) noexcept override;
  link::BladeLinkState state() const noexcept { return state_.state(); }
  BladeReplayStats replayStats() const noexcept;

 private:
  enum class EventKind : std::uint8_t { Receive, SendComplete, DataBatch };
  struct Event {
    EventKind kind{};
    hal::RadioRxMeta meta{};
    MacAddress destination{};
    std::uint16_t size{};
    bool delivered{};
    RecordingId recordingId{};
    NativeTimestampUs originMonoUs{};
    std::uint8_t bytes[250]{};
  };
  static void taskEntry(void*) noexcept;
  void run() noexcept;
  void handleReceive(const Event&) noexcept;
  void sendHello() noexcept;
  void sendSourceManifest() noexcept;
  void sendDataBatch(const Event&) noexcept;
  void sendReplay(const link::ReplayBatch&) noexcept;
  void sendGapNotice(const link::ReplayGap&, std::uint16_t reason) noexcept;
  void handleAckWindow(const link::DecodedFrame&) noexcept;
  void handleReplayRequest(const link::DecodedFrame&) noexcept;
  void recordDroppedBatch(RecordingId, const link::DataBatch&) noexcept;
  bool takeDroppedGap(RecordingId&, link::ReplayGap&) noexcept;
  Status recoverRadio(NativeTimestampUs nowUs) noexcept;
  bool enqueue(const Event&) noexcept;
  static constexpr std::size_t kQueueCapacity = 16;
  platform::esp32::MbedTlsCrypto crypto_{};
  BladeAcquisitionService* acquisition_{nullptr};
  platform::esp32::NvsCredentials credentials_{};
  platform::esp32::EspNowDriver radio_{};
  link::FrameCodec codec_{crypto_};
  link::TxScheduler tx_{radio_};
  link::BladeLinkStateMachine state_{};
  link::RadioSupervisor supervisor_{};
  link::ReplayWindow replay_{};
  DeviceSerialNumber deviceId_{0};
  DeviceBootId bootId_{0};
  NetworkId networkId_{0};
  std::array<std::uint8_t, 32> secret_{};
  std::array<std::uint8_t, 32> groupKey_{};
  link::DerivedKeys pairKeys_{};
  MacAddress computerAddress_{};
  std::uint32_t lastBeaconSeq_{0};
  NativeTimestampUs lastBeaconRxUs_{0};
  std::uint32_t messageSeq_{0};
  QueueHandle_t queue_{nullptr};
  StaticQueue_t queueBuffer_{};
  std::uint8_t queueStorage_[kQueueCapacity * sizeof(Event)]{};
  StaticTask_t taskBuffer_{};
  StackType_t taskStack_[4096]{};
  TaskHandle_t task_{nullptr};
  std::atomic<std::uint32_t> replayDepth_{0};
  std::atomic<std::uint32_t> replayOverflows_{0};
  std::atomic<std::uint32_t> ackWindows_{0};
  std::atomic<std::uint32_t> replayQueued_{0};
  std::atomic<std::uint32_t> replayUnavailable_{0};
  std::atomic<std::uint32_t> gapNotices_{0};
  struct PendingGap {
    RecordingId recordingId{0};
    link::ReplayGap gap{};
  };
  static constexpr std::size_t kPendingGapCapacity = 16;
  std::array<PendingGap, kPendingGapCapacity> pendingGaps_{};
  std::size_t pendingGapCount_{0};
  portMUX_TYPE pendingGapMux_ = portMUX_INITIALIZER_UNLOCKED;
};
}  // namespace remus::blade
