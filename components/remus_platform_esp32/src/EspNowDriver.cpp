#include "remus/platform/esp32/EspNowDriver.hpp"

#include <cstring>

#include "esp_event.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "remus/hal/RadioEventAdmission.hpp"

namespace remus::platform::esp32 {
namespace {
constexpr std::uint8_t kBroadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
Status failure(Error error, esp_err_t detail) noexcept {
  return Status::failure(error, static_cast<std::uint32_t>(detail));
}
MacAddress addressOf(const std::uint8_t* bytes) noexcept {
  MacAddress address{};
  if (bytes != nullptr) std::memcpy(address.bytes, bytes, sizeof(address.bytes));
  return address;
}
}  // namespace

EspNowDriver* EspNowDriver::active_ = nullptr;

Status EspNowDriver::start(std::uint8_t channel) noexcept {
  if (started_) return channel == channel_ ? Status::success() : Status::failure(Error::Busy);
  if (channel < 1 || channel > 11 || active_ != nullptr)
    return Status::failure(Error::InvalidArgument);

  esp_err_t result = esp_netif_init();
  if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) return failure(Error::NotReady, result);
  result = esp_event_loop_create_default();
  if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) return failure(Error::NotReady, result);
  wifi_init_config_t wifiConfig = WIFI_INIT_CONFIG_DEFAULT();
  result = esp_wifi_init(&wifiConfig);
  if (result != ESP_OK) return failure(Error::NotReady, result);
  if ((result = esp_wifi_set_storage(WIFI_STORAGE_RAM)) != ESP_OK ||
      (result = esp_wifi_set_mode(WIFI_MODE_STA)) != ESP_OK ||
      (result = esp_wifi_start()) != ESP_OK ||
      (result = esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE)) != ESP_OK ||
      (result = esp_now_init()) != ESP_OK) {
    esp_wifi_stop();
    esp_wifi_deinit();
    return failure(Error::NotReady, result);
  }

  queue_ = xQueueCreateStatic(kEventCapacity, sizeof(DriverEvent), queueStorage_, &queueBuffer_);
  if (queue_ == nullptr) {
    stop();
    return Status::failure(Error::QueueFull);
  }
  active_ = this;
  if ((result = esp_now_register_recv_cb(receiveCallback)) != ESP_OK ||
      (result = esp_now_register_send_cb(sendCallback)) != ESP_OK) {
    stop();
    return failure(Error::NotReady, result);
  }
  task_ = xTaskCreateStatic(taskEntry, "espnow_events", kTaskStackDepth, this, tskIDLE_PRIORITY + 2,
                            taskStack_, &taskBuffer_);
  if (task_ == nullptr) {
    stop();
    return Status::failure(Error::NotReady);
  }

  esp_now_peer_info_t broadcast{};
  std::memcpy(broadcast.peer_addr, kBroadcast, sizeof(kBroadcast));
  broadcast.channel = channel;
  broadcast.ifidx = WIFI_IF_STA;
  broadcast.encrypt = false;
  result = esp_now_add_peer(&broadcast);
  if (result != ESP_OK && result != ESP_ERR_ESPNOW_EXIST) {
    stop();
    return failure(result == ESP_ERR_ESPNOW_FULL ? Error::PeerFull : Error::NotReady, result);
  }
  channel_ = channel;
  started_ = true;
  return Status::success();
}

Status EspNowDriver::setPrimaryMasterKey(const std::uint8_t key[16]) noexcept {
  if (!started_ || key == nullptr) return Status::failure(Error::NotReady);
  const esp_err_t result = esp_now_set_pmk(key);
  return result == ESP_OK ? Status::success() : failure(Error::CryptoError, result);
}

Status EspNowDriver::addPeer(const MacAddress& address, const std::uint8_t key[16]) noexcept {
  if (!started_ || key == nullptr) return Status::failure(Error::NotReady);
  esp_now_peer_info_t peer{};
  std::memcpy(peer.peer_addr, address.bytes, sizeof(address.bytes));
  std::memcpy(peer.lmk, key, sizeof(peer.lmk));
  peer.channel = channel_;
  peer.ifidx = WIFI_IF_STA;
  peer.encrypt = true;
  esp_err_t result = esp_now_add_peer(&peer);
  if (result == ESP_ERR_ESPNOW_EXIST) result = esp_now_mod_peer(&peer);
  if (result == ESP_OK) return Status::success();
  if (result == ESP_ERR_ESPNOW_FULL) return failure(Error::PeerFull, result);
  return failure(Error::NotReady, result);
}

Status EspNowDriver::removePeer(const MacAddress& address) noexcept {
  if (!started_) return Status::failure(Error::NotReady);
  const esp_err_t result = esp_now_del_peer(address.bytes);
  if (result == ESP_OK || result == ESP_ERR_ESPNOW_NOT_FOUND) return Status::success();
  return failure(Error::PeerNotFound, result);
}

Status EspNowDriver::requestSend(const MacAddress& destination, const std::uint8_t* data,
                                 std::size_t size) noexcept {
  if (!started_ || data == nullptr || size == 0 || size > ESP_NOW_MAX_DATA_LEN)
    return Status::failure(Error::InvalidArgument);
  const esp_err_t result = esp_now_send(destination.bytes, data, size);
  if (result == ESP_OK) return Status::success();
  if (result == ESP_ERR_ESPNOW_NO_MEM) return failure(Error::Busy, result);
  if (result == ESP_ERR_ESPNOW_NOT_FOUND) return failure(Error::PeerNotFound, result);
  return failure(Error::NotReady, result);
}

Status EspNowDriver::restartLinkLayer() noexcept {
  if (!started_ || active_ != this) return Status::failure(Error::NotReady);

  esp_now_unregister_recv_cb();
  esp_now_unregister_send_cb();
  esp_now_deinit();

  esp_err_t result = esp_now_init();
  if (result != ESP_OK) return failure(Error::NotReady, result);
  if ((result = esp_now_register_recv_cb(receiveCallback)) != ESP_OK ||
      (result = esp_now_register_send_cb(sendCallback)) != ESP_OK) {
    esp_now_deinit();
    return failure(Error::NotReady, result);
  }

  esp_now_peer_info_t broadcast{};
  std::memcpy(broadcast.peer_addr, kBroadcast, sizeof(kBroadcast));
  broadcast.channel = channel_;
  broadcast.ifidx = WIFI_IF_STA;
  broadcast.encrypt = false;
  result = esp_now_add_peer(&broadcast);
  if (result != ESP_OK && result != ESP_ERR_ESPNOW_EXIST) {
    esp_now_unregister_recv_cb();
    esp_now_unregister_send_cb();
    esp_now_deinit();
    return failure(result == ESP_ERR_ESPNOW_FULL ? Error::PeerFull : Error::NotReady, result);
  }
  return Status::success();
}

Status EspNowDriver::stop() noexcept {
  if (active_ == this) {
    esp_now_unregister_recv_cb();
    esp_now_unregister_send_cb();
    active_ = nullptr;
  }
  if (task_ != nullptr) {
    vTaskDelete(task_);
    task_ = nullptr;
  }
  esp_now_deinit();
  esp_wifi_stop();
  esp_wifi_deinit();
  started_ = false;
  channel_ = 0;
  queue_ = nullptr;
  return Status::success();
}

void EspNowDriver::receiveCallback(const esp_now_recv_info_t* info, const std::uint8_t* data,
                                   int size) noexcept {
  if (active_ == nullptr || info == nullptr || info->src_addr == nullptr || data == nullptr ||
      size <= 0 || size > static_cast<int>(ESP_NOW_MAX_DATA_LEN))
    return;
  DriverEvent event{};
  event.kind = EventKind::Receive;
  event.meta.source = addressOf(info->src_addr);
  event.meta.rssiDbm = info->rx_ctrl == nullptr ? kUnavailableRssiDbm : info->rx_ctrl->rssi;
  event.meta.localRxUs = static_cast<NativeTimestampUs>(esp_timer_get_time());
  event.size = static_cast<std::uint16_t>(size);
  std::memcpy(event.bytes, data, event.size);
  active_->enqueueFromCallback(event);
}

void EspNowDriver::sendCallback(const esp_now_send_info_t* info,
                                esp_now_send_status_t status) noexcept {
  if (active_ == nullptr || info == nullptr || info->des_addr == nullptr) return;
  DriverEvent event{};
  event.kind = EventKind::SendComplete;
  event.destination = addressOf(info->des_addr);
  event.delivered = status == ESP_NOW_SEND_SUCCESS;
  active_->enqueueFromCallback(event);
}

bool EspNowDriver::enqueueFromCallback(const DriverEvent& event) noexcept {
  ++callbackEvents_;
  const bool receiveCapacityAvailable =
      event.kind != EventKind::Receive ||
      (queue_ != nullptr && hal::canAdmitRadioReceiveEvent(
                                uxQueueMessagesWaiting(queue_), kEventCapacity));
  const bool accepted = queue_ != nullptr && receiveCapacityAvailable &&
                        xQueueSend(queue_, &event, 0) == pdTRUE;
  if (!accepted) ++callbackDrops_;
  return accepted;
}

void EspNowDriver::taskEntry(void* context) noexcept { static_cast<EspNowDriver*>(context)->run(); }

void EspNowDriver::run() noexcept {
  DriverEvent event{};
  for (;;) {
    if (xQueueReceive(queue_, &event, portMAX_DELAY) != pdTRUE || sink_ == nullptr) continue;
    if (event.kind == EventKind::Receive) {
      sink_->onRadioReceived(event.meta, event.bytes, event.size);
    } else {
      sink_->onRadioSendComplete(event.destination, event.delivered);
    }
  }
}
}  // namespace remus::platform::esp32
