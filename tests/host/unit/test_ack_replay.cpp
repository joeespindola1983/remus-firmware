#include <cstdint>
#include <iostream>
#include <string>

#include "remus/link/AckTracker.hpp"
#include "remus/link/MessageCodec.hpp"
#include "remus/link/ReplayWindow.hpp"

namespace {
int failures = 0;
void expect(bool value, const char* expression, int line) {
  if (!value) {
    std::cerr << "FAIL line " << line << ": " << expression << '\n';
    ++failures;
  }
}
#define EXPECT(value) expect((value), #value, __LINE__)

std::string toHex(const std::uint8_t* bytes, std::size_t size) {
  constexpr char digits[] = "0123456789abcdef";
  std::string result(size * 2, '0');
  for (std::size_t index = 0; index < size; ++index) {
    result[index * 2] = digits[bytes[index] >> 4];
    result[index * 2 + 1] = digits[bytes[index] & 0x0F];
  }
  return result;
}

remus::link::ReplayBatch batch(std::uint64_t recordingId, std::uint32_t sequence,
                               std::uint64_t firstSampleIndex = 0) {
  remus::link::ReplayBatch result{};
  result.recordingId = recordingId;
  result.batchSeq = sequence;
  result.firstSampleIndex = firstSampleIndex;
  result.originMonoUs = sequence * 50000ULL;
  result.payloadSize = 4;
  result.payload[0] = static_cast<std::uint8_t>(sequence);
  return result;
}

void testAckWindowTracksOutOfOrderAndBackpressure() {
  remus::link::AckTracker tracker;

  const auto zero = tracker.accept(0);
  EXPECT(zero.disposition == remus::link::AcceptDisposition::New);
  EXPECT(!zero.gapDetected);
  EXPECT(!tracker.buildAckWindow(1001, 77).acceptedValid);

  EXPECT(tracker.markQueued(0).ok());
  EXPECT(tracker.markQueued(1).error == remus::Error::InvalidArgument);

  const auto first = tracker.buildAckWindow(1001, 77);
  EXPECT(first.acceptedValid);
  EXPECT(first.acceptedThrough == 0);
  EXPECT(first.receivedBitmap == 0);
  EXPECT(!first.checkpointedValid);

  EXPECT(tracker.accept(1).disposition == remus::link::AcceptDisposition::New);
  EXPECT(tracker.markQueued(1).ok());
  const auto three = tracker.accept(3);
  EXPECT(three.disposition == remus::link::AcceptDisposition::New);
  EXPECT(three.gapDetected);
  EXPECT(tracker.markQueued(3).ok());

  const auto gap = tracker.buildAckWindow(1001, 77);
  EXPECT(gap.acceptedThrough == 1);
  EXPECT(gap.receivedBitmap == 0x00000002U);

  remus::link::ReplayRequest request{};
  EXPECT(tracker.nextReplayRequest(1001, 77, request));
  EXPECT(request.firstSeq == 2);
  EXPECT(request.count == 1);

  EXPECT(tracker.accept(3).disposition == remus::link::AcceptDisposition::Duplicate);
  EXPECT(tracker.accept(2).disposition == remus::link::AcceptDisposition::New);
  EXPECT(tracker.markQueued(2).ok());
  const auto recovered = tracker.buildAckWindow(1001, 77);
  EXPECT(recovered.acceptedThrough == 3);
  EXPECT(recovered.receivedBitmap == 0);
  EXPECT(!tracker.nextReplayRequest(1001, 77, request));
}

void testAckWindowDoesNotInventCheckpointProgress() {
  remus::link::AckTracker tracker;
  EXPECT(tracker.accept(0).disposition == remus::link::AcceptDisposition::New);
  EXPECT(tracker.markQueued(0).ok());
  const auto ack = tracker.buildAckWindow(9, 10);
  EXPECT(ack.acceptedValid);
  EXPECT(!ack.checkpointedValid);
  EXPECT(ack.checkpointedThrough == 0);
  EXPECT((ack.receiverHealth & remus::link::kReceiverHealthAcceptedValid) != 0);
  EXPECT((ack.receiverHealth & remus::link::kReceiverHealthCheckpointedValid) == 0);
  EXPECT(tracker.markCheckpointedThrough(1).error == remus::Error::InvalidArgument);
  EXPECT(tracker.markCheckpointedThrough(0).ok());
  const auto checkpointed = tracker.buildAckWindow(9, 10);
  EXPECT(checkpointed.checkpointedValid);
  EXPECT(checkpointed.checkpointedThrough == 0);
  EXPECT((checkpointed.receiverHealth & remus::link::kReceiverHealthCheckpointedValid) != 0);
}

void testGapSealingIsBoundedAndAtomic() {
  remus::link::AckTracker tracker;
  EXPECT(tracker.sealGap(0, 63).ok());
  const auto longGap = tracker.buildAckWindow(9, 10);
  EXPECT(longGap.acceptedValid);
  EXPECT(longGap.acceptedThrough == 63);
  EXPECT(!longGap.checkpointedValid);

  remus::link::AckTracker nonContiguous;
  EXPECT(nonContiguous.sealGap(5, 40).error == remus::Error::InvalidArgument);
  const auto unchanged = nonContiguous.buildAckWindow(9, 10);
  EXPECT(!unchanged.acceptedValid);
  EXPECT(unchanged.receivedBitmap == 0);
}

void testReplayWindowRetainsAndReleasesOnlyCheckpointedBatches() {
  remus::link::ReplayWindow replay(3);
  EXPECT(replay.append(batch(55, 0, 0)).disposition ==
         remus::link::ReplayAppendDisposition::Stored);
  EXPECT(replay.append(batch(55, 1, 10)).disposition ==
         remus::link::ReplayAppendDisposition::Stored);
  EXPECT(replay.append(batch(55, 2, 20)).disposition ==
         remus::link::ReplayAppendDisposition::Stored);
  EXPECT(replay.append(batch(55, 2, 20)).disposition ==
         remus::link::ReplayAppendDisposition::Duplicate);
  EXPECT(replay.size() == 3);

  EXPECT(replay.markAcceptedThrough(55, 1).ok());
  EXPECT(replay.size() == 3);
  EXPECT(replay.markCheckpointedThrough(55, 2).error == remus::Error::InvalidArgument);
  EXPECT(replay.markCheckpointedThrough(55, 1).ok());
  EXPECT(replay.size() == 1);
  EXPECT(replay.oldestSeq() == 2);

  remus::link::ReplayBatch found{};
  EXPECT(replay.prepareReplay(55, 2, found).ok());
  EXPECT(found.batchSeq == 2);
  EXPECT(found.payload[0] == 2);
  EXPECT(replay.prepareReplay(99, 2, found).error == remus::Error::InvalidArgument);
  EXPECT(replay.append(batch(99, 0)).disposition ==
         remus::link::ReplayAppendDisposition::Rejected);
  EXPECT(replay.markAcceptedThrough(55, 2).ok());
  EXPECT(replay.markCheckpointedThrough(55, 2).ok());
  EXPECT(replay.append(batch(99, 0)).disposition ==
         remus::link::ReplayAppendDisposition::Stored);
  EXPECT(replay.prepareReplay(55, 2, found).error == remus::Error::InvalidArgument);
}

void testReplayOverflowProducesExplicitGap() {
  remus::link::ReplayWindow replay(2);
  replay.append(batch(55, 7, 70));
  replay.append(batch(55, 8, 80));
  const auto overflow = replay.append(batch(55, 9, 90));
  EXPECT(overflow.disposition == remus::link::ReplayAppendDisposition::StoredWithGap);
  EXPECT(overflow.gap.valid);
  EXPECT(overflow.gap.fromSeq == 7);
  EXPECT(overflow.gap.throughSeq == 7);
  EXPECT(overflow.gap.firstSampleIndex == 70);
  EXPECT(replay.overflowCount() == 1);
  EXPECT(replay.oldestSeq() == 8);

  remus::link::ReplayGap queuedLoss{true, 10, 11, 100};
  EXPECT(replay.recordLostGap(55, queuedLoss).ok());
  remus::link::ReplayGap known{};
  EXPECT(replay.knownLostGap(55, 10, known));
  EXPECT(known.throughSeq == 11);
  EXPECT(replay.recordLostGap(99, queuedLoss).error == remus::Error::InvalidArgument);
}

void testAckReplayAndGapPayloadCodecs() {
  std::uint8_t payload[remus::link::kMaximumPayloadBytes]{};
  std::size_t size = 0;

  remus::link::AckWindow ack{1, 2, 3, 0x80000001U, 0,
                             remus::link::kReceiverHealthAcceptedValid, 0};
  EXPECT(remus::link::MessageCodec::encode(ack, payload, sizeof(payload), size).ok());
  EXPECT(size == remus::link::kAckWindowPayloadBytes);
  EXPECT(toHex(payload, size) ==
         "01000000000000000200000003000000010000800000000001000000");
  remus::link::AckWindow decodedAck{};
  EXPECT(remus::link::MessageCodec::decode(payload, size, decodedAck).ok());
  EXPECT(decodedAck.bladeId == 1);
  EXPECT(decodedAck.receivedBitmap == 0x80000001U);
  EXPECT(decodedAck.acceptedValid);
  EXPECT(!decodedAck.checkpointedValid);

  ack.receiverHealth = remus::link::kReceiverHealthCheckpointedValid;
  EXPECT(remus::link::MessageCodec::encode(ack, payload, sizeof(payload), size).error ==
         remus::Error::InvalidArgument);
  ack.receiverHealth = remus::link::kReceiverHealthAcceptedValid |
                       remus::link::kReceiverHealthCheckpointedValid;
  ack.acceptedThrough = 3;
  ack.checkpointedThrough = 4;
  EXPECT(remus::link::MessageCodec::encode(ack, payload, sizeof(payload), size).error ==
         remus::Error::InvalidArgument);

  remus::link::ReplayRequest request{1, 2, 7, 16, 1};
  EXPECT(remus::link::MessageCodec::encode(request, payload, sizeof(payload), size).ok());
  EXPECT(size == remus::link::kReplayRequestPayloadBytes);
  EXPECT(toHex(payload, size) == "0100000000000000020000000700000010010000");
  remus::link::ReplayRequest decodedRequest{};
  EXPECT(remus::link::MessageCodec::decode(payload, size, decodedRequest).ok());
  EXPECT(decodedRequest.firstSeq == 7);
  EXPECT(decodedRequest.count == 16);
  request.count = 17;
  EXPECT(remus::link::MessageCodec::encode(request, payload, sizeof(payload), size).error ==
         remus::Error::InvalidArgument);

  remus::link::GapNotice gap{1, 2, 7, 9, 70, 1};
  EXPECT(remus::link::MessageCodec::encode(gap, payload, sizeof(payload), size).ok());
  EXPECT(size == remus::link::kGapNoticePayloadBytes);
  EXPECT(toHex(payload, size) ==
         "0100000000000000020000000700000009000000460000000000000001000000");
  remus::link::GapNotice decodedGap{};
  EXPECT(remus::link::MessageCodec::decode(payload, size, decodedGap).ok());
  EXPECT(decodedGap.throughSeq == 9);
  EXPECT(decodedGap.firstSampleIndex == 70);
}

void testLostBatchIsRecoveredEndToEnd() {
  remus::link::ReplayWindow replay(8);
  remus::link::AckTracker tracker;
  for (std::uint32_t sequence = 0; sequence < 4; ++sequence)
    EXPECT(replay.append(batch(55, sequence, sequence * 10)).status.ok());

  for (const std::uint32_t received : {0U, 1U, 3U}) {
    EXPECT(tracker.accept(received).disposition == remus::link::AcceptDisposition::New);
    EXPECT(tracker.markQueued(received).ok());
  }
  remus::link::ReplayRequest request{};
  EXPECT(tracker.nextReplayRequest(1001, 77, request));
  EXPECT(request.firstSeq == 2);
  remus::link::ReplayBatch recovered{};
  EXPECT(replay.prepareReplay(55, request.firstSeq, recovered).ok());
  EXPECT(tracker.accept(recovered.batchSeq).disposition ==
         remus::link::AcceptDisposition::New);
  EXPECT(tracker.markQueued(recovered.batchSeq).ok());
  EXPECT(tracker.buildAckWindow(1001, 77).acceptedThrough == 3);
  EXPECT(tracker.markCheckpointedThrough(3).ok());
  EXPECT(replay.markAcceptedThrough(55, 3).ok());
  EXPECT(replay.markCheckpointedThrough(55, 3).ok());
  EXPECT(replay.size() == 0);
}
}  // namespace

int main() {
  testAckWindowTracksOutOfOrderAndBackpressure();
  testAckWindowDoesNotInventCheckpointProgress();
  testGapSealingIsBoundedAndAtomic();
  testReplayWindowRetainsAndReleasesOnlyCheckpointedBatches();
  testReplayOverflowProducesExplicitGap();
  testAckReplayAndGapPayloadCodecs();
  testLostBatchIsRecoveredEndToEnd();
  return failures == 0 ? 0 : 1;
}
