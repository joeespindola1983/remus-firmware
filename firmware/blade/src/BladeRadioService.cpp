#include "BladeRadioService.hpp"

#include <cstring>

#include "BladeAcquisitionService.hpp"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "remus/link/MessageCodec.hpp"

namespace remus::blade {
namespace {
constexpr char kTag[] = "blade_radio";
constexpr MacAddress kBroadcast{{0xff, 0xff, 0xff, 0xff, 0xff, 0xff}};
}  // namespace
Status BladeRadioService::start() noexcept {
  esp_err_t nvs = nvs_flash_init();
  if (nvs != ESP_OK) return Status::failure(Error::NotReady, nvs);
  std::size_t secretSize = 0;
  Status s = credentials_.readDeviceSerialNumber(deviceId_);
  if (!s.ok()) return s;
  s = credentials_.readNetworkId(networkId_);
  if (!s.ok()) return s;
  s = credentials_.readNetworkSecret(secret_.data(), secret_.size(), secretSize);
  if (!s.ok()) return s;
  s = link::deriveGroupAuthKey(crypto_, secret_.data(), secretSize, networkId_, groupKey_);
  if (!s.ok()) return s;
  std::array<std::uint8_t, 16> pmk{};
  s = link::derivePmk(crypto_, secret_.data(), secretSize, networkId_, pmk);
  if (!s.ok()) return s;
  bootId_ = esp_random();
  if (bootId_ == 0) bootId_ = 1;
  queue_ = xQueueCreateStatic(kQueueCapacity, sizeof(Event), queueStorage_, &queueBuffer_);
  if (!queue_) return Status::failure(Error::QueueFull);
  radio_.setEventSink(this);
  s = radio_.start(1);
  if (!s.ok()) {
    state_.onRadioFailed();
    return s;
  }
  s = radio_.setPrimaryMasterKey(pmk.data());
  if (!s.ok()) return s;
  task_ = xTaskCreateStatic(taskEntry, "blade_radio", 4096, this, tskIDLE_PRIORITY + 3, taskStack_,
                            &taskBuffer_);
  if (!task_) {
    radio_.stop();
    return Status::failure(Error::NotReady);
  }
  state_.onRadioStarted();
  ESP_LOGI(kTag, "state=Discovering channel=1 network=%lu", static_cast<unsigned long>(networkId_));
  return Status::success();
}
bool BladeRadioService::enqueue(const Event& event) noexcept {
  return queue_ && xQueueSend(queue_, &event, 0) == pdTRUE;
}
void BladeRadioService::onRadioReceived(const hal::RadioRxMeta& meta, const std::uint8_t* bytes,
                                        std::size_t size) noexcept {
  if (!bytes || size > 250) return;
  Event e{};
  e.kind = EventKind::Receive;
  e.meta = meta;
  e.size = size;
  std::memcpy(e.bytes, bytes, size);
  enqueue(e);
}
void BladeRadioService::onRadioSendComplete(const MacAddress& destination,
                                            bool delivered) noexcept {
  Event e{};
  e.kind = EventKind::SendComplete;
  e.destination = destination;
  e.delivered = delivered;
  enqueue(e);
}
void BladeRadioService::taskEntry(void* p) noexcept { static_cast<BladeRadioService*>(p)->run(); }
void BladeRadioService::run() noexcept {
  Event e{};
  std::int64_t lastHello = 0;
  for (;;) {
    if (xQueueReceive(queue_, &e, pdMS_TO_TICKS(100)) == pdTRUE) {
      if (e.kind == EventKind::Receive)
        handleReceive(e);
      else if (e.kind == EventKind::SendComplete) {
        tx_.onSendComplete(e.destination, e.delivered);
        supervisor_.observe(e.delivered ? link::RadioHealthEvent::FrameDelivered
                                        : link::RadioHealthEvent::DriverFailure,
                            static_cast<NativeTimestampUs>(esp_timer_get_time()));
      } else {
        sendDataBatch(e);
      }
    }
    const auto now = esp_timer_get_time();
    if (state_.state() == link::BladeLinkState::HelloReady && now - lastHello >= 800000) {
      sendHello();
      lastHello = now;
    }
    tx_.poll();
  }
}
void BladeRadioService::handleReceive(const Event& e) noexcept {
  link::DecodedFrame frame{};
  if (state_.state() == link::BladeLinkState::Discovering) {
    if (!codec_.decode(e.bytes, e.size, groupKey_.data(), groupKey_.size(), frame).ok() ||
        frame.header.type != link::MessageType::Beacon || frame.header.networkId != networkId_)
      return;
    link::Beacon beacon{};
    if (!link::MessageCodec::decode(frame.payload.data(), frame.payloadSize, beacon).ok() ||
        beacon.computerId != frame.header.senderId || beacon.pcBootId != frame.header.senderBootId)
      return;
    if (!link::derivePairKeys(crypto_, secret_.data(), secret_.size(), deviceId_, beacon.computerId,
                              pairKeys_)
             .ok() ||
        !radio_.addPeer(e.meta.source, pairKeys_.pairLmk.data()).ok() ||
        !state_.onAuthenticatedBeacon(beacon.computerId, beacon.pcBootId).ok())
      return;
    computerAddress_ = e.meta.source;
    lastBeaconSeq_ = beacon.beaconSeq;
    lastBeaconRxUs_ = e.meta.localRxUs;
    ESP_LOGI(kTag, "state=HelloReady pc_boot=%lu rssi=%d",
             static_cast<unsigned long>(beacon.pcBootId), e.meta.rssiDbm);
    sendHello();
    return;
  }
  if (!codec_
           .decode(e.bytes, e.size, pairKeys_.pairAuthKey.data(), pairKeys_.pairAuthKey.size(),
                   frame)
           .ok() ||
      frame.header.type != link::MessageType::Bind || frame.header.networkId != networkId_ ||
      frame.header.senderId != state_.computerId())
    return;
  link::Bind bind{};
  if (!link::MessageCodec::decode(frame.payload.data(), frame.payloadSize, bind).ok() ||
      bind.bladeId != deviceId_ || bind.bladeBootId != bootId_ ||
      !state_.onBindAccepted(bind.pcBootId, bind.sessionId, bind.bindEpoch).ok())
    return;
  if (bind.allowCapture != 0 &&
      (acquisition_ == nullptr || !acquisition_->beginCapture(bind.sessionId).ok())) {
    ESP_LOGE(kTag, "capture grant rejected recording_id=%llu",
             static_cast<unsigned long long>(bind.sessionId));
    return;
  }
  link::BindAck ack{deviceId_, bootId_, bind.pcBootId, bind.sessionId, bind.bindEpoch, 0};
  std::uint8_t payload[link::kBindAckPayloadBytes]{};
  std::size_t n = 0;
  link::MessageCodec::encode(ack, payload, sizeof(payload), n);
  link::FrameHeader h{link::MessageType::BindAck,
                      0,
                      networkId_,
                      deviceId_,
                      bootId_,
                      bind.sessionId,
                      ++messageSeq_,
                      static_cast<NativeTimestampUs>(esp_timer_get_time())};
  link::EncodedFrame out{};
  if (codec_.encode(h, payload, n, pairKeys_.pairAuthKey.data(), pairKeys_.pairAuthKey.size(), out)
          .ok())
    tx_.enqueue(computerAddress_, out.bytes.data(), out.size);
  ESP_LOGI(kTag, "state=Bound bind_epoch=%lu", static_cast<unsigned long>(bind.bindEpoch));
}

Status BladeRadioService::queueDataBatch(RecordingId recordingId, NativeTimestampUs originMonoUs,
                                         const link::DataBatch& batch) noexcept {
  Event event{};
  event.kind = EventKind::DataBatch;
  event.recordingId = recordingId;
  event.originMonoUs = originMonoUs;
  std::size_t payloadSize = 0;
  const auto status =
      link::MessageCodec::encode(batch, event.bytes, sizeof(event.bytes), payloadSize);
  if (!status.ok()) return status;
  event.size = static_cast<std::uint16_t>(payloadSize);
  return enqueue(event) ? Status::success() : Status::failure(Error::QueueFull);
}

void BladeRadioService::sendDataBatch(const Event& event) noexcept {
  if (state_.state() != link::BladeLinkState::Bound ||
      event.recordingId != state_.recordingId())
    return;
  link::FrameHeader header{link::MessageType::DataBatch,
                           0,
                           networkId_,
                           deviceId_,
                           bootId_,
                           event.recordingId,
                           ++messageSeq_,
                           event.originMonoUs};
  link::EncodedFrame frame{};
  if (codec_
          .encode(header, event.bytes, event.size, pairKeys_.pairAuthKey.data(),
                  pairKeys_.pairAuthKey.size(), frame)
          .ok()) {
    tx_.enqueue(computerAddress_, frame.bytes.data(), frame.size);
  }
}
void BladeRadioService::sendHello() noexcept {
  link::Hello hello{deviceId_,
                    bootId_,
                    state_.computerId(),
                    state_.computerBootId(),
                    lastBeaconSeq_,
                    lastBeaconRxUs_,
                    1,
                    1};
  std::uint8_t payload[link::kHelloPayloadBytes]{};
  std::size_t n = 0;
  if (!link::MessageCodec::encode(hello, payload, sizeof(payload), n).ok()) return;
  link::FrameHeader h{link::MessageType::Hello,
                      0,
                      networkId_,
                      deviceId_,
                      bootId_,
                      0,
                      ++messageSeq_,
                      static_cast<NativeTimestampUs>(esp_timer_get_time())};
  link::EncodedFrame out{};
  if (codec_.encode(h, payload, n, groupKey_.data(), groupKey_.size(), out).ok())
    tx_.enqueue(kBroadcast, out.bytes.data(), out.size);
}
}  // namespace remus::blade
