#include "ComputerRadioService.hpp"

#include <cstdio>
#include <cstring>

#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "remus/link/MessageCodec.hpp"

namespace remus::computer {
namespace {
constexpr char kTag[] = "computer_radio";
constexpr MacAddress kBroadcast{{0xff, 0xff, 0xff, 0xff, 0xff, 0xff}};
constexpr std::int64_t kBeaconIntervalUs = 2000000;
constexpr NativeTimestampUs kCandidateLifetimeUs = 10000000;
}  // namespace

Status ComputerRadioService::start() noexcept {
  const esp_err_t nvs = nvs_flash_init();
  if (nvs != ESP_OK) return Status::failure(Error::NotReady, nvs);
  std::size_t secretSize = 0;
  Status status = credentials_.readDeviceSerialNumber(computerId_);
  if (!status.ok()) return status;
  status = credentials_.readNetworkId(networkId_);
  if (!status.ok()) return status;
  status = credentials_.readNetworkSecret(secret_.data(), secret_.size(), secretSize);
  if (!status.ok()) return status;
  status = link::deriveGroupAuthKey(crypto_, secret_.data(), secretSize, networkId_, groupKey_);
  if (!status.ok()) return status;
  std::array<std::uint8_t, 16> pmk{};
  status = link::derivePmk(crypto_, secret_.data(), secretSize, networkId_, pmk);
  if (!status.ok()) return status;
  bootId_ = esp_random();
  if (!bootId_) bootId_ = 1;
  queue_ = xQueueCreateStatic(kQueueCapacity, sizeof(Event), queueStorage_, &queueBuffer_);
  if (!queue_) return Status::failure(Error::QueueFull);
  radio_.setEventSink(this);
  status = radio_.start(1);
  if (!status.ok()) return status;
  status = radio_.setPrimaryMasterKey(pmk.data());
  if (!status.ok()) {
    radio_.stop();
    return status;
  }
  task_ = xTaskCreateStatic(taskEntry, "computer_radio", 5120, this, tskIDLE_PRIORITY + 3,
                            taskStack_, &taskBuffer_);
  if (!task_) {
    radio_.stop();
    return Status::failure(Error::NotReady);
  }
  ESP_LOGI(kTag, "state=Discovering channel=1 network=%lu", static_cast<unsigned long>(networkId_));
  log(DiagnosticLogLevel::Info, "radio_discovering", "channel=1");
  return Status::success();
}

bool ComputerRadioService::enqueue(const Event& event) noexcept {
  return queue_ && xQueueSend(queue_, &event, 0) == pdTRUE;
}
void ComputerRadioService::onRadioReceived(const hal::RadioRxMeta& meta, const std::uint8_t* bytes,
                                           std::size_t size) noexcept {
  if (!bytes || size > link::kMaximumFrameBytes) return;
  Event event{};
  event.kind = EventKind::Receive;
  event.meta = meta;
  event.size = size;
  std::memcpy(event.bytes, bytes, size);
  enqueue(event);
}
void ComputerRadioService::onRadioSendComplete(const MacAddress& destination,
                                               bool delivered) noexcept {
  Event event{};
  event.kind = EventKind::SendComplete;
  event.destination = destination;
  event.delivered = delivered;
  enqueue(event);
}
void ComputerRadioService::taskEntry(void* context) noexcept {
  static_cast<ComputerRadioService*>(context)->run();
}
void ComputerRadioService::run() noexcept {
  Event event{};
  std::int64_t lastBeaconUs = -kBeaconIntervalUs;
  for (;;) {
    if (xQueueReceive(queue_, &event, pdMS_TO_TICKS(100)) == pdTRUE) {
      if (event.kind == EventKind::Receive)
        handleReceive(event);
      else {
        tx_.onSendComplete(event.destination, event.delivered);
        supervisor_.observe(event.delivered ? link::RadioHealthEvent::FrameDelivered
                                            : link::RadioHealthEvent::DriverFailure,
                            static_cast<NativeTimestampUs>(esp_timer_get_time()));
      }
    }
    const std::int64_t now = esp_timer_get_time();
    if (now - lastBeaconUs >= kBeaconIntervalUs) {
      sendBeacon();
      lastBeaconUs = now;
    }
    for (std::size_t index = 0; index < link::SourceRegistry::capacity(); ++index) {
      auto* source = registry_.entryAt(index);
      if (source && source->state.state() == link::ComputerPeerState::BindPending &&
          source->bindAttempts < 3 &&
          static_cast<NativeTimestampUs>(now) >= source->nextBindAttemptUs)
        sendBind(*source);
    }
    MacAddress removed[link::SourceRegistry::capacity()]{};
    const auto expired =
        registry_.expireCandidates(static_cast<NativeTimestampUs>(now), kCandidateLifetimeUs,
                                   removed, link::SourceRegistry::capacity());
    for (std::size_t index = 0; index < expired; ++index) radio_.removePeer(removed[index]);
    if (expired) log(DiagnosticLogLevel::Warning, "blade_candidate_expired", "bind_timeout=true");
    tx_.poll();
  }
}
void ComputerRadioService::handleReceive(const Event& event) noexcept {
  link::DecodedFrame frame{};
  if (codec_.decode(event.bytes, event.size, groupKey_.data(), groupKey_.size(), frame).ok() &&
      frame.header.type == link::MessageType::Hello && frame.header.networkId == networkId_) {
    handleHello(event, frame);
    return;
  }
  handleBindAck(event);
}
void ComputerRadioService::handleHello(const Event& event,
                                       const link::DecodedFrame& frame) noexcept {
  link::Hello hello{};
  if (!link::MessageCodec::decode(frame.payload.data(), frame.payloadSize, hello).ok() ||
      hello.bladeId != frame.header.senderId || hello.bladeBootId != frame.header.senderBootId ||
      hello.computerIdSeen != computerId_ || hello.pcBootIdSeen != bootId_ || !hello.peerReady)
    return;
  Status status =
      registry_.admit(hello.bladeId, hello.bladeBootId, event.meta.source, event.meta.localRxUs);
  if (!status.ok()) {
    ESP_LOGW(kTag, "HELLO rejected error=%u", static_cast<unsigned>(status.error));
    log(DiagnosticLogLevel::Warning, "blade_rejected", "reason=registry_policy");
    return;
  }
  auto* source = registry_.find(hello.bladeId, hello.bladeBootId);
  if (!source) return;
  if (source->state.state() == link::ComputerPeerState::BindPending) return;
  if (source->state.state() != link::ComputerPeerState::Candidate) return;
  status = link::derivePairKeys(crypto_, secret_.data(), secret_.size(), hello.bladeId, computerId_,
                                source->pairKeys);
  if (!status.ok() || !radio_.addPeer(event.meta.source, source->pairKeys.pairLmk.data()).ok() ||
      !source->state.onPeerRegistered().ok())
    return;
  source->bindEpoch = esp_random();
  if (!source->bindEpoch) source->bindEpoch = 1;
  ESP_LOGI(kTag, "blade candidate boot=%lu rssi=%d sources=%u",
           static_cast<unsigned long>(hello.bladeBootId), event.meta.rssiDbm,
           static_cast<unsigned>(registry_.size()));
  log(DiagnosticLogLevel::Info, "blade_candidate", "peer_authenticated=true");
  sendBind(*source);
}
void ComputerRadioService::sendBind(link::SourceEntry& source) noexcept {
  if (source.bindAttempts >= 3) return;
  link::Bind bind{source.bladeId, source.bladeBootId, bootId_, 0, source.bindEpoch, 0};
  std::uint8_t payload[link::kBindPayloadBytes]{};
  std::size_t payloadSize = 0;
  if (!link::MessageCodec::encode(bind, payload, sizeof(payload), payloadSize).ok()) return;
  link::FrameHeader header{link::MessageType::Bind,
                           0,
                           networkId_,
                           computerId_,
                           bootId_,
                           0,
                           ++messageSeq_,
                           static_cast<NativeTimestampUs>(esp_timer_get_time())};
  link::EncodedFrame frame{};
  if (!codec_
           .encode(header, payload, payloadSize, source.pairKeys.pairAuthKey.data(),
                   source.pairKeys.pairAuthKey.size(), frame)
           .ok() ||
      !tx_.enqueue(source.address, frame.bytes.data(), frame.size).ok())
    return;
  if (source.state.state() == link::ComputerPeerState::Registered)
    source.state.onBindSent(source.bindEpoch);
  ++source.bindAttempts;
  const NativeTimestampUs backoffUs = 1000000ULL << (source.bindAttempts - 1U);
  source.nextBindAttemptUs = static_cast<NativeTimestampUs>(esp_timer_get_time()) + backoffUs;
}
void ComputerRadioService::handleBindAck(const Event& event) noexcept {
  // MAC selects only an existing candidate. Identity and state are accepted only
  // after the complete frame has passed pair-key authentication below.
  auto* source = registry_.findByAddress(event.meta.source);
  if (!source) return;
  link::DecodedFrame clear{};
  if (!codec_
           .decode(event.bytes, event.size, source->pairKeys.pairAuthKey.data(),
                   source->pairKeys.pairAuthKey.size(), clear)
           .ok() ||
      clear.header.type != link::MessageType::BindAck || clear.header.networkId != networkId_)
    return;
  link::BindAck ack{};
  if (!link::MessageCodec::decode(clear.payload.data(), clear.payloadSize, ack).ok() ||
      ack.bladeId != source->bladeId || ack.bladeBootId != source->bladeBootId ||
      ack.pcBootId != bootId_ || ack.resultCode != 0 ||
      !source->state.onBindAck(ack.bindEpoch).ok())
    return;
  ESP_LOGI(kTag, "blade state=Bound boot=%lu sources=%u",
           static_cast<unsigned long>(ack.bladeBootId), static_cast<unsigned>(registry_.size()));
  log(DiagnosticLogLevel::Info, "blade_bound", "application_ack=true");
}
void ComputerRadioService::sendBeacon() noexcept {
  link::Beacon beacon{computerId_, bootId_, 1, 0, ++beaconSeq_, 0};
  std::uint8_t payload[link::kBeaconPayloadBytes]{};
  std::size_t payloadSize = 0;
  if (!link::MessageCodec::encode(beacon, payload, sizeof(payload), payloadSize).ok()) return;
  link::FrameHeader header{link::MessageType::Beacon,
                           0,
                           networkId_,
                           computerId_,
                           bootId_,
                           0,
                           ++messageSeq_,
                           static_cast<NativeTimestampUs>(esp_timer_get_time())};
  link::EncodedFrame frame{};
  if (codec_.encode(header, payload, payloadSize, groupKey_.data(), groupKey_.size(), frame).ok())
    tx_.enqueue(kBroadcast, frame.bytes.data(), frame.size);
}
void ComputerRadioService::log(DiagnosticLogLevel level, const char* event,
                               const char* details) noexcept {
  if (diagnosticLog_ && diagnosticLog_->isReady()) diagnosticLog_->enqueue(level, event, details);
}
}  // namespace remus::computer
