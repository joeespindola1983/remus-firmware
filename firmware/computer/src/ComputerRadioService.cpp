#include "ComputerRadioService.hpp"

#include <cstdio>
#include <cstring>

#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "remus/diagnostics/OperationalSerial.hpp"
#include "remus/link/MessageCodec.hpp"

namespace remus::computer {
namespace {
constexpr char kTag[] = "computer_radio";
constexpr MacAddress kBroadcast{{0xff, 0xff, 0xff, 0xff, 0xff, 0xff}};
constexpr std::int64_t kBeaconIntervalUs = 2000000;
constexpr NativeTimestampUs kCandidateLifetimeUs = 10000000;
constexpr NativeTimestampUs kSourceLivenessTimeoutUs = 5000000;
constexpr NativeTimestampUs kRejectedHelloLogIntervalUs = 5000000;
constexpr NativeTimestampUs kAckHeartbeatUs = 500000;
constexpr NativeTimestampUs kReplayThrottleUs = 200000;
constexpr std::uint32_t kAckBatchInterval = 10;
}  // namespace

ComputerRecoveryStats ComputerRadioService::recoveryStats() const noexcept {
  return {ackFramesQueued_.load(), checkpointAcksQueued_.load(),
          replayRequestsQueued_.load(), duplicateBatches_.load(),
          outsideWindowBatches_.load(), unrecoverableGaps_.load()};
}

Status ComputerRadioService::start() noexcept {
  if (recordingId_ == 0) return Status::failure(Error::InvalidArgument);
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
  ESP_LOGI(kTag, "BLADE: waiting for connection");
  log(DiagnosticLogLevel::Info, "radio_discovering", "channel=1");
  return Status::success();
}

bool ComputerRadioService::enqueue(const Event& event) noexcept {
  return queue_ && xQueueSend(queue_, &event, 0) == pdTRUE;
}
void ComputerRadioService::setCaptureAllowed(bool allowed) noexcept {
  const bool wasAllowed = captureAllowed_.exchange(allowed);
  if (allowed && !wasAllowed) {
    Event event{};
    event.kind = EventKind::CaptureEnabled;
    enqueue(event);
  }
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
  std::int64_t lastRecoveryAttemptUs = -1000000;
  for (;;) {
    if (xQueueReceive(queue_, &event, pdMS_TO_TICKS(100)) == pdTRUE) {
      if (event.kind == EventKind::Receive)
        handleReceive(event);
      else if (event.kind == EventKind::SendComplete) {
        tx_.onSendComplete(event.destination, event.delivered);
        supervisor_.observe(event.delivered ? link::RadioHealthEvent::FrameDelivered
                                            : link::RadioHealthEvent::DriverFailure,
                            static_cast<NativeTimestampUs>(esp_timer_get_time()));
      } else {
        // A Blade may have completed BIND during the short interval between
        // radio startup and durable RMS1 creation. Re-send the idempotent BIND
        // with allowCapture=1 so that such a Blade cannot remain silently idle.
        for (std::size_t index = 0; index < link::SourceRegistry::capacity(); ++index) {
          auto* source = registry_.entryAt(index);
          if (source && source->state.state() == link::ComputerPeerState::Bound) {
            source->bindAttempts = 0;
            sendBind(*source);
          }
        }
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
    for (auto& sink : dataSinks_) {
      if (sink.bladeId == 0 || sink.ack.acceptedCount() == 0 ||
          static_cast<NativeTimestampUs>(now) - sink.lastAckSentUs < kAckHeartbeatUs)
        continue;
      auto* source = registry_.find(sink.bladeId, sink.bladeBootId);
      if (source != nullptr) sendAckAndReplay(*source, sink, true, now);
    }
    for (std::size_t index = 0; index < link::SourceRegistry::capacity(); ++index) {
      auto* source = registry_.entryAt(index);
      if (source == nullptr || source->state.state() != link::ComputerPeerState::Bound ||
          source->staleReported || source->lastSeenUs == 0 ||
          static_cast<NativeTimestampUs>(now) < source->lastSeenUs ||
          static_cast<NativeTimestampUs>(now) - source->lastSeenUs <= kSourceLivenessTimeoutUs)
        continue;
      source->staleReported = true;
      const auto active = registry_.liveBoundCount(static_cast<NativeTimestampUs>(now),
                                                   kSourceLivenessTimeoutUs);
      char details[160]{};
      std::snprintf(details, sizeof(details),
                    "blade_id=%llu boot=%lu active=%u reason=data_timeout",
                    static_cast<unsigned long long>(source->bladeId),
                    static_cast<unsigned long>(source->bladeBootId),
                    static_cast<unsigned>(active));
      ESP_LOGW(kTag, "BLADE: LOST %s", details);
      log(DiagnosticLogLevel::Warning, "blade_stale", details);
    }
    MacAddress removed[link::SourceRegistry::capacity()]{};
    const auto expired =
        registry_.expireCandidates(static_cast<NativeTimestampUs>(now), kCandidateLifetimeUs,
                                   removed, link::SourceRegistry::capacity());
    for (std::size_t index = 0; index < expired; ++index) radio_.removePeer(removed[index]);
    if (expired) {
      ESP_LOGW(kTag, "BLADE: connection failed (BIND timeout)");
      log(DiagnosticLogLevel::Warning, "blade_candidate_expired", "bind_timeout=true");
    }
    const Status txStatus = tx_.poll(static_cast<NativeTimestampUs>(now));
    if (txStatus.error == Error::Timeout) {
      ESP_LOGE(kTag, "tx callback timeout; restarting ESP-NOW before next frame");
      log(DiagnosticLogLevel::Error, "tx_callback_timeout", "radio_restart=true");
      supervisor_.observe(link::RadioHealthEvent::DriverFailure,
                          static_cast<NativeTimestampUs>(now));
    }
    if (tx_.recoveryRequired() && now - lastRecoveryAttemptUs >= 1000000) {
      lastRecoveryAttemptUs = now;
      const Status recovery = recoverRadio(static_cast<NativeTimestampUs>(now));
      if (!recovery.ok()) {
        ESP_LOGE(kTag, "radio recovery failed error=%u detail=%lu",
                 static_cast<unsigned>(recovery.error),
                 static_cast<unsigned long>(recovery.detail));
        log(DiagnosticLogLevel::Error, "radio_recovery_failed", "retry_pending=true");
      }
    }
  }
}

Status ComputerRadioService::recoverRadio(NativeTimestampUs nowUs) noexcept {
  std::array<std::uint8_t, 16> pmk{};
  Status status = link::derivePmk(crypto_, secret_.data(), secret_.size(), networkId_, pmk);
  if (!status.ok()) return status;
  radio_.stop();
  status = radio_.start(1);
  if (status.ok()) status = radio_.setPrimaryMasterKey(pmk.data());
  for (std::size_t index = 0; status.ok() && index < link::SourceRegistry::capacity(); ++index) {
    auto* source = registry_.entryAt(index);
    if (source != nullptr &&
        source->state.state() != link::ComputerPeerState::Candidate)
      status = radio_.addPeer(source->address, source->pairKeys.pairLmk.data());
  }
  if (!status.ok()) return status;
  tx_.resetAfterRadioRestart(nowUs);
  supervisor_.observe(link::RadioHealthEvent::Reinitialized, nowUs);
  log(DiagnosticLogLevel::Warning, "radio_reinitialized", "reason=tx_callback_timeout");
  return Status::success();
}
void ComputerRadioService::handleReceive(const Event& event) noexcept {
  link::DecodedFrame frame{};
  if (codec_.decode(event.bytes, event.size, groupKey_.data(), groupKey_.size(), frame).ok() &&
      frame.header.type == link::MessageType::Hello && frame.header.networkId == networkId_) {
    handleHello(event, frame);
    return;
  }
  handlePaired(event);
}
void ComputerRadioService::handleHello(const Event& event,
                                       const link::DecodedFrame& frame) noexcept {
  link::Hello hello{};
  if (!link::MessageCodec::decode(frame.payload.data(), frame.payloadSize, hello).ok() ||
      hello.bladeId != frame.header.senderId || hello.bladeBootId != frame.header.senderBootId ||
      hello.computerIdSeen != computerId_ || hello.pcBootIdSeen != bootId_ || !hello.peerReady)
    return;
  const auto* previous = registry_.findByAddress(event.meta.source);
  const bool restarted = previous != nullptr && previous->bladeId == hello.bladeId &&
                         previous->bladeBootId != hello.bladeBootId;
  const DeviceBootId previousBootId = restarted ? previous->bladeBootId : 0;
  Status status =
      registry_.admit(hello.bladeId, hello.bladeBootId, event.meta.source, event.meta.localRxUs);
  if (!status.ok()) {
    if (lastRejectedHelloUs_ == 0 || event.meta.localRxUs < lastRejectedHelloUs_ ||
        event.meta.localRxUs - lastRejectedHelloUs_ >= kRejectedHelloLogIntervalUs) {
      char details[160]{};
      std::snprintf(details, sizeof(details),
                    "blade_id=%llu boot=%lu error=%u reason=registry_policy",
                    static_cast<unsigned long long>(hello.bladeId),
                    static_cast<unsigned long>(hello.bladeBootId),
                    static_cast<unsigned>(status.error));
      ESP_LOGW(kTag, "BLADE: REJECTED %s", details);
      log(DiagnosticLogLevel::Warning, "blade_rejected", details);
      lastRejectedHelloUs_ = event.meta.localRxUs;
    }
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
  char details[192]{};
  if (restarted) {
    std::snprintf(details, sizeof(details),
                  "blade_id=%llu previous_boot=%08lx boot=%08lx rssi=%d",
                  static_cast<unsigned long long>(hello.bladeId),
                  static_cast<unsigned long>(previousBootId),
                  static_cast<unsigned long>(hello.bladeBootId), event.meta.rssiDbm);
    ESP_LOGW(kTag, "BLADE: RESTARTED %s", details);
    log(DiagnosticLogLevel::Warning, "blade_restarted", details);
  } else {
    std::snprintf(details, sizeof(details), "blade_id=%llu boot=%08lx rssi=%d",
                  static_cast<unsigned long long>(hello.bladeId),
                  static_cast<unsigned long>(hello.bladeBootId), event.meta.rssiDbm);
    ESP_LOGI(kTag, "BLADE: detected %s", details);
    log(DiagnosticLogLevel::Info, "blade_candidate", details);
  }
  sendBind(*source);
}
void ComputerRadioService::sendBind(link::SourceEntry& source) noexcept {
  if (source.bindAttempts >= 3) return;
  link::Bind bind{source.bladeId, source.bladeBootId, bootId_, recordingId_, source.bindEpoch,
                  static_cast<std::uint8_t>(captureAllowed_.load() ? 1 : 0)};
  std::uint8_t payload[link::kBindPayloadBytes]{};
  std::size_t payloadSize = 0;
  if (!link::MessageCodec::encode(bind, payload, sizeof(payload), payloadSize).ok()) return;
  link::FrameHeader header{link::MessageType::Bind,
                           0,
                           networkId_,
                           computerId_,
                           bootId_,
                           recordingId_,
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
void ComputerRadioService::handlePaired(const Event& event) noexcept {
  // MAC selects only an existing candidate. Identity and state are accepted only
  // after the complete frame has passed pair-key authentication below.
  auto* source = registry_.findByAddress(event.meta.source);
  if (!source) return;
  link::DecodedFrame clear{};
  if (!codec_
           .decode(event.bytes, event.size, source->pairKeys.pairAuthKey.data(),
                   source->pairKeys.pairAuthKey.size(), clear)
           .ok() ||
      clear.header.networkId != networkId_ || clear.header.senderId != source->bladeId ||
      clear.header.senderBootId != source->bladeBootId)
    return;
  const bool resumed = source->staleReported;
  source->lastSeenUs = event.meta.localRxUs;
  source->staleReported = false;
  if (resumed) {
    char details[128]{};
    std::snprintf(details, sizeof(details), "blade_id=%llu boot=%08lx",
                  static_cast<unsigned long long>(source->bladeId),
                  static_cast<unsigned long>(source->bladeBootId));
    ESP_LOGI(kTag, "BLADE: RESUMED %s", details);
    log(DiagnosticLogLevel::Info, "blade_resumed", details);
  }
  if (clear.header.type == link::MessageType::BindAck)
    handleBindAck(*source, clear);
  else if (clear.header.type == link::MessageType::SourceManifest)
    handleSourceManifest(*source, clear);
  else if (clear.header.type == link::MessageType::DataBatch)
    handleDataBatch(*source, event, clear);
  else if (clear.header.type == link::MessageType::GapNotice)
    handleGapNotice(*source, clear);
}

void ComputerRadioService::handleSourceManifest(link::SourceEntry& source,
                                                const link::DecodedFrame& clear) noexcept {
  if (source.state.state() != link::ComputerPeerState::Bound ||
      clear.header.recordingId != recordingId_)
    return;
  link::SourceManifest manifest{};
  if (!link::MessageCodec::decode(clear.payload.data(), clear.payloadSize, manifest).ok() ||
      manifest.bladeId != source.bladeId || manifest.bladeBootId != source.bladeBootId ||
      diagnosticLog_ == nullptr ||
      !diagnosticLog_->enqueueSourceManifest(source.bladeId, source.bladeBootId,
                                             clear.payload.data(), clear.payloadSize)) {
    log(DiagnosticLogLevel::Error, "source_manifest_rejected", "invalid_or_queue_full=true");
    return;
  }
  log(DiagnosticLogLevel::Info, "source_manifest_recorded", "imu_profile_received=true");
}

void ComputerRadioService::handleBindAck(link::SourceEntry& source,
                                         const link::DecodedFrame& clear) noexcept {
  link::BindAck ack{};
  if (!link::MessageCodec::decode(clear.payload.data(), clear.payloadSize, ack).ok() ||
      ack.bladeId != source.bladeId || ack.bladeBootId != source.bladeBootId ||
      ack.pcBootId != bootId_ || ack.sessionId != recordingId_ || ack.resultCode != 0 ||
      !source.state.onBindAck(ack.bindEpoch).ok())
    return;
  const auto active = registry_.liveBoundCount(
      static_cast<NativeTimestampUs>(esp_timer_get_time()), kSourceLivenessTimeoutUs);
  char details[160]{};
  std::snprintf(details, sizeof(details), "blade_id=%llu boot=%08lx active=%u",
                static_cast<unsigned long long>(ack.bladeId),
                static_cast<unsigned long>(ack.bladeBootId),
                static_cast<unsigned>(active));
  ESP_LOGI(kTag, "BLADE: CONNECTED %s", details);
  log(DiagnosticLogLevel::Info, "blade_bound", details);
}

void ComputerRadioService::handleDataBatch(link::SourceEntry& source, const Event& event,
                                           const link::DecodedFrame& clear) noexcept {
  if (source.state.state() != link::ComputerPeerState::Bound ||
      clear.header.recordingId != recordingId_)
    return;
  link::DataBatch batch{};
  if (!link::MessageCodec::decode(clear.payload.data(), clear.payloadSize, batch).ok()) {
    log(DiagnosticLogLevel::Warning, "data_batch_rejected", "reason=payload_invalid");
    return;
  }
  DataSinkStats* sink = dataSink(source.bladeId, source.bladeBootId);
  if (sink == nullptr) {
    log(DiagnosticLogLevel::Error, "data_batch_rejected", "reason=sink_capacity");
    return;
  }
  const auto admission = sink->ack.accept(batch.batchSeq);
  if (admission.disposition == link::AcceptDisposition::Duplicate) {
    ++sink->duplicates;
    ++duplicateBatches_;
    sendAckAndReplay(source, *sink, true, event.meta.localRxUs);
    return;
  }
  if (admission.disposition == link::AcceptDisposition::OutsideWindow) {
    ++sink->outsideWindow;
    ++outsideWindowBatches_;
    ++sink->gaps;
    sendAckAndReplay(source, *sink, true, event.meta.localRxUs);
    log(DiagnosticLogLevel::Warning, "data_outside_ack_window", "replay_requested=true");
    return;
  }
  if (diagnosticLog_ == nullptr ||
      !diagnosticLog_->enqueueImuBatch(source.bladeId, source.bladeBootId, batch.batchSeq,
                                       clear.header.originMonoUs, event.meta.localRxUs,
                                       event.meta.rssiDbm, clear.payload.data(),
                                       clear.payloadSize)) {
    sink->ack.reject(batch.batchSeq);
    log(DiagnosticLogLevel::Error, "rms_batch_rejected", "reason=storage_queue_full");
    return;
  }
  if (!sink->ack.markQueued(batch.batchSeq).ok()) {
    log(DiagnosticLogLevel::Error, "data_batch_rejected", "reason=ack_state");
    return;
  }
  if (admission.gapDetected) {
    ++sink->gaps;
    char details[192]{};
    std::snprintf(details, sizeof(details),
                  "blade_id=%llu actual_batch=%lu actual_sample=%llu replay_requested=true",
                  static_cast<unsigned long long>(source.bladeId),
                  static_cast<unsigned long>(batch.batchSeq),
                  static_cast<unsigned long long>(batch.firstSampleIndex));
    ESP_LOGW(kTag, "DATA gap %s", details);
    log(DiagnosticLogLevel::Warning, "data_gap", details);
  }
  ++sink->batches;
  sink->samples += batch.sampleCount;
  if (admission.gapDetected ||
      sink->ack.acceptedCount() - sink->acceptedAtLastAck >= kAckBatchInterval)
    sendAckAndReplay(source, *sink, true, event.meta.localRxUs);
  if (sink->batches % 20 == 0) {
    if (batch.sampleCount > 0) {
      const auto& sample = batch.samples[batch.sampleCount - 1];
      const diagnostics::ImuRawPreview preview{sample.ax, sample.ay, sample.az, sample.gx,
                                               sample.gy, sample.gz, sample.flags};
      char line[160]{};
      if (diagnostics::formatImuRawPreview("BLADE", source.bladeBootId, preview, line,
                                           sizeof(line)))
        ESP_LOGI(kTag, "%s", line);
    }
    char details[224]{};
    std::snprintf(details, sizeof(details),
                  "blade_id=%llu boot=%lu batches=%llu samples=%llu gaps=%lu rssi=%d "
                  "duplicates=%lu outside_window=%lu last_batch=%lu",
                  static_cast<unsigned long long>(source.bladeId),
                  static_cast<unsigned long>(source.bladeBootId),
                  static_cast<unsigned long long>(sink->batches),
                  static_cast<unsigned long long>(sink->samples),
                  static_cast<unsigned long>(sink->gaps), event.meta.rssiDbm,
                  static_cast<unsigned long>(sink->duplicates),
                  static_cast<unsigned long>(sink->outsideWindow),
                  static_cast<unsigned long>(batch.batchSeq));
    log(DiagnosticLogLevel::Info, "data_sink", details);
  }
}

ComputerRadioService::DataSinkStats* ComputerRadioService::dataSink(
    BladeId bladeId, DeviceBootId bladeBootId) noexcept {
  DataSinkStats* empty = nullptr;
  DataSinkStats* previousBoot = nullptr;
  for (auto& candidate : dataSinks_) {
    if (candidate.bladeId == bladeId && candidate.bladeBootId == bladeBootId) return &candidate;
    if (candidate.bladeId == bladeId && previousBoot == nullptr) previousBoot = &candidate;
    if (candidate.bladeId == 0 && empty == nullptr) empty = &candidate;
  }
  if (previousBoot != nullptr) {
    *previousBoot = DataSinkStats{};
    previousBoot->bladeId = bladeId;
    previousBoot->bladeBootId = bladeBootId;
    return previousBoot;
  }
  if (empty != nullptr) {
    empty->bladeId = bladeId;
    empty->bladeBootId = bladeBootId;
  }
  return empty;
}

void ComputerRadioService::sendAckAndReplay(link::SourceEntry& source, DataSinkStats& sink,
                                            bool allowReplay,
                                            NativeTimestampUs nowUs) noexcept {
  std::uint32_t checkpointed = 0;
  if (diagnosticLog_ != nullptr &&
      diagnosticLog_->checkpointedThrough(source.bladeId, source.bladeBootId, checkpointed))
    sink.ack.markCheckpointedThrough(checkpointed);

  const link::AckWindow ack = sink.ack.buildAckWindow(source.bladeId, source.bladeBootId);
  std::uint8_t payload[link::kAckWindowPayloadBytes]{};
  std::size_t payloadSize = 0;
  if (link::MessageCodec::encode(ack, payload, sizeof(payload), payloadSize).ok()) {
    const link::FrameHeader header{link::MessageType::AckWindow,
                                   0,
                                   networkId_,
                                   computerId_,
                                   bootId_,
                                   recordingId_,
                                   ++messageSeq_,
                                   nowUs};
    link::EncodedFrame frame{};
    if (codec_.encode(header, payload, payloadSize, source.pairKeys.pairAuthKey.data(),
                      source.pairKeys.pairAuthKey.size(), frame).ok() &&
        tx_.enqueue(source.address, frame.bytes.data(), frame.size).ok()) {
      sink.lastAckSentUs = nowUs;
      sink.acceptedAtLastAck = sink.ack.acceptedCount();
      ++ackFramesQueued_;
      if (ack.checkpointedValid) ++checkpointAcksQueued_;
    }
  }

  link::ReplayRequest request{};
  if (!allowReplay || !sink.ack.nextReplayRequest(source.bladeId, source.bladeBootId, request) ||
      (sink.lastReplayValid && sink.lastReplayFirstSeq == request.firstSeq &&
       nowUs - sink.lastReplayRequestUs < kReplayThrottleUs))
    return;
  std::uint8_t replayPayload[link::kReplayRequestPayloadBytes]{};
  payloadSize = 0;
  if (!link::MessageCodec::encode(request, replayPayload, sizeof(replayPayload), payloadSize).ok())
    return;
  const link::FrameHeader replayHeader{link::MessageType::ReplayRequest,
                                       0,
                                       networkId_,
                                       computerId_,
                                       bootId_,
                                       recordingId_,
                                       ++messageSeq_,
                                       nowUs};
  link::EncodedFrame replayFrame{};
  if (codec_.encode(replayHeader, replayPayload, payloadSize,
                    source.pairKeys.pairAuthKey.data(), source.pairKeys.pairAuthKey.size(),
                    replayFrame).ok() &&
      tx_.enqueue(source.address, replayFrame.bytes.data(), replayFrame.size).ok()) {
    sink.lastReplayValid = true;
    sink.lastReplayFirstSeq = request.firstSeq;
    sink.lastReplayRequestUs = nowUs;
    ++replayRequestsQueued_;
  }
}

void ComputerRadioService::handleGapNotice(link::SourceEntry& source,
                                           const link::DecodedFrame& clear) noexcept {
  if (source.state.state() != link::ComputerPeerState::Bound ||
      clear.header.recordingId != recordingId_)
    return;
  link::GapNotice gap{};
  if (!link::MessageCodec::decode(clear.payload.data(), clear.payloadSize, gap).ok() ||
      gap.bladeId != source.bladeId || gap.bladeBootId != source.bladeBootId)
    return;
  DataSinkStats* sink = dataSink(source.bladeId, source.bladeBootId);
  if (sink == nullptr || !sink->ack.canSealGap(gap.fromSeq, gap.throughSeq)) {
    log(DiagnosticLogLevel::Error, "gap_rejected", "reason=ack_window");
    return;
  }
  if (diagnosticLog_ == nullptr ||
      !diagnosticLog_->enqueueGap(source.bladeId, source.bladeBootId, gap.fromSeq,
                                  gap.throughSeq, gap.firstSampleIndex, gap.reason)) {
    log(DiagnosticLogLevel::Error, "gap_rejected", "reason=storage_queue_full");
    return;
  }
  sink->ack.sealGap(gap.fromSeq, gap.throughSeq);
  ++sink->unrecoverableGaps;
  ++unrecoverableGaps_;
  char details[160]{};
  std::snprintf(details, sizeof(details),
                "blade_id=%llu from_batch=%lu through_batch=%lu first_sample=%llu",
                static_cast<unsigned long long>(source.bladeId),
                static_cast<unsigned long>(gap.fromSeq),
                static_cast<unsigned long>(gap.throughSeq),
                static_cast<unsigned long long>(gap.firstSampleIndex));
  ESP_LOGE(kTag, "unrecoverable DATA gap %s", details);
  log(DiagnosticLogLevel::Error, "unrecoverable_data_gap", details);
  sendAckAndReplay(source, *sink, true,
                   static_cast<NativeTimestampUs>(esp_timer_get_time()));
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
