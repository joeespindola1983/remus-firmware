#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "esp_now.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "remus/hal/IRadio.hpp"

namespace remus::platform::esp32 {
struct EspNowDriverStats {
  std::uint32_t callbackEvents{0};
  std::uint32_t callbackDrops{0};
};
class EspNowDriver final : public hal::IRadio {
 public:
  void setEventSink(hal::IRadioEvents* sink) noexcept override { sink_ = sink; }
  Status start(std::uint8_t channel) noexcept override;
  Status setPrimaryMasterKey(const std::uint8_t key[16]) noexcept override;
  Status addPeer(const MacAddress&, const std::uint8_t linkKey[16]) noexcept override;
  Status removePeer(const MacAddress&) noexcept override;
  Status requestSend(const MacAddress&, const std::uint8_t*, std::size_t) noexcept override;
  Status stop() noexcept override;
  EspNowDriverStats stats() const noexcept {
    return {callbackEvents_.load(), callbackDrops_.load()};
  }

 private:
  enum class EventKind : std::uint8_t { Receive, SendComplete };
  struct DriverEvent {
    EventKind kind{EventKind::Receive};
    hal::RadioRxMeta meta{};
    MacAddress destination{};
    std::uint16_t size{0};
    bool delivered{false};
    std::uint8_t bytes[250]{};
  };
  static constexpr std::size_t kEventCapacity = 32;
  static constexpr std::uint32_t kTaskStackDepth = 4096;
  static void receiveCallback(const esp_now_recv_info_t*, const std::uint8_t*, int) noexcept;
  static void sendCallback(const esp_now_send_info_t*, esp_now_send_status_t) noexcept;
  static void taskEntry(void*) noexcept;
  void run() noexcept;
  bool enqueueFromCallback(const DriverEvent&) noexcept;

  static EspNowDriver* active_;
  hal::IRadioEvents* sink_{nullptr};
  QueueHandle_t queue_{nullptr};
  StaticQueue_t queueBuffer_{};
  std::uint8_t queueStorage_[kEventCapacity * sizeof(DriverEvent)]{};
  StaticTask_t taskBuffer_{};
  StackType_t taskStack_[kTaskStackDepth]{};
  TaskHandle_t task_{nullptr};
  std::uint8_t channel_{0};
  bool started_{false};
  std::atomic<std::uint32_t> callbackEvents_{0};
  std::atomic<std::uint32_t> callbackDrops_{0};
};
}  // namespace remus::platform::esp32
