#include "remus/link/AckTracker.hpp"

namespace remus::link {
namespace {
constexpr std::uint32_t kWindowBits = 32;
}

bool AckTracker::offsetFor(std::uint32_t batchSeq, std::uint32_t& offset) const noexcept {
  const std::uint32_t base = acceptedValid_ ? acceptedThrough_ + 1U : 0U;
  if (acceptedValid_ && batchSeq <= acceptedThrough_) return false;
  offset = batchSeq - base;
  return offset < kWindowBits;
}

AcceptResult AckTracker::accept(std::uint32_t batchSeq) noexcept {
  if (acceptedValid_ && batchSeq <= acceptedThrough_)
    return {AcceptDisposition::Duplicate, false};
  const bool newGap = !highestObservedValid_ ? batchSeq > 0
                                             : batchSeq > highestObserved_ &&
                                                   batchSeq - highestObserved_ > 1U;
  if (!highestObservedValid_ || batchSeq > highestObserved_) {
    highestObservedValid_ = true;
    highestObserved_ = batchSeq;
  }
  std::uint32_t offset = 0;
  if (!offsetFor(batchSeq, offset)) return {AcceptDisposition::OutsideWindow, true};
  const std::uint32_t mask = std::uint32_t{1} << offset;
  if ((acceptedBitmap_ & mask) != 0 || (pendingBitmap_ & mask) != 0)
    return {AcceptDisposition::Duplicate, false};
  pendingBitmap_ |= mask;
  return {AcceptDisposition::New, newGap};
}

Status AckTracker::markQueued(std::uint32_t batchSeq) noexcept {
  std::uint32_t offset = 0;
  if (!offsetFor(batchSeq, offset)) return Status::failure(Error::InvalidArgument);
  const std::uint32_t mask = std::uint32_t{1} << offset;
  if ((pendingBitmap_ & mask) == 0) return Status::failure(Error::InvalidArgument);
  pendingBitmap_ &= ~mask;
  acceptedBitmap_ |= mask;
  ++acceptedCount_;
  advanceContiguous();
  return Status::success();
}

Status AckTracker::reject(std::uint32_t batchSeq) noexcept {
  std::uint32_t offset = 0;
  if (!offsetFor(batchSeq, offset)) return Status::failure(Error::InvalidArgument);
  const std::uint32_t mask = std::uint32_t{1} << offset;
  if ((pendingBitmap_ & mask) == 0) return Status::failure(Error::InvalidArgument);
  pendingBitmap_ &= ~mask;
  return Status::success();
}

Status AckTracker::markCheckpointedThrough(std::uint32_t batchSeq) noexcept {
  if (!acceptedValid_ || batchSeq > acceptedThrough_)
    return Status::failure(Error::InvalidArgument);
  if (!checkpointedValid_ || batchSeq > checkpointedThrough_) {
    checkpointedValid_ = true;
    checkpointedThrough_ = batchSeq;
  }
  return Status::success();
}

void AckTracker::advanceContiguous() noexcept {
  while ((acceptedBitmap_ & 1U) != 0) {
    acceptedThrough_ = acceptedValid_ ? acceptedThrough_ + 1U : 0U;
    acceptedValid_ = true;
    acceptedBitmap_ >>= 1U;
    pendingBitmap_ >>= 1U;
  }
}

AckWindow AckTracker::buildAckWindow(BladeId bladeId,
                                     DeviceBootId bladeBootId) const noexcept {
  AckWindow ack{};
  ack.bladeId = bladeId;
  ack.bladeBootId = bladeBootId;
  ack.acceptedThrough = acceptedValid_ ? acceptedThrough_ : 0;
  ack.receivedBitmap = acceptedBitmap_;
  ack.checkpointedThrough = checkpointedValid_ ? checkpointedThrough_ : 0;
  ack.receiverHealth = (acceptedValid_ ? kReceiverHealthAcceptedValid : 0) |
                       (checkpointedValid_ ? kReceiverHealthCheckpointedValid : 0);
  ack.acceptedValid = acceptedValid_;
  ack.checkpointedValid = checkpointedValid_;
  return ack;
}

bool AckTracker::nextReplayRequest(BladeId bladeId, DeviceBootId bladeBootId,
                                   ReplayRequest& request) const noexcept {
  const std::uint32_t base = acceptedValid_ ? acceptedThrough_ + 1U : 0U;
  if (!highestObservedValid_ || base > highestObserved_) return false;
  if (acceptedBitmap_ == 0) {
    const std::uint32_t available = highestObserved_ - base + 1U;
    request = {bladeId, bladeBootId, base,
               static_cast<std::uint8_t>(available < 4U ? available : 4U), 1};
    return true;
  }
  std::uint32_t highest = 31;
  while (highest > 0 && (acceptedBitmap_ & (std::uint32_t{1} << highest)) == 0) --highest;
  std::uint32_t firstMissing = 0;
  while (firstMissing < highest &&
         (acceptedBitmap_ & (std::uint32_t{1} << firstMissing)) != 0)
    ++firstMissing;
  if (firstMissing >= highest) {
    const std::uint32_t afterBitmap = base + highest + 1U;
    if (afterBitmap > highestObserved_) return false;
    const std::uint32_t available = highestObserved_ - afterBitmap + 1U;
    request = {bladeId, bladeBootId, afterBitmap,
               static_cast<std::uint8_t>(available < 4U ? available : 4U), 1};
    return true;
  }
  std::uint32_t count = 0;
  while (firstMissing + count < highest && count < 4 &&
         (acceptedBitmap_ & (std::uint32_t{1} << (firstMissing + count))) == 0)
    ++count;
  request = {bladeId, bladeBootId,
             base + firstMissing,
             static_cast<std::uint8_t>(count), 1};
  return count != 0;
}

bool AckTracker::canSealGap(std::uint32_t fromSeq, std::uint32_t throughSeq) const noexcept {
  if (fromSeq > throughSeq) return false;
  const std::uint32_t base = acceptedValid_ ? acceptedThrough_ + 1U : 0U;
  if (throughSeq < base) return true;
  const std::uint32_t effectiveFrom = fromSeq < base ? base : fromSeq;
  return effectiveFrom == base ||
         (effectiveFrom - base < kWindowBits && throughSeq - base < kWindowBits);
}

Status AckTracker::sealGap(std::uint32_t fromSeq, std::uint32_t throughSeq) noexcept {
  if (!canSealGap(fromSeq, throughSeq)) return Status::failure(Error::InvalidArgument);

  const std::uint32_t base = acceptedValid_ ? acceptedThrough_ + 1U : 0U;
  if (throughSeq < base) return Status::success();
  const std::uint32_t effectiveFrom = fromSeq < base ? base : fromSeq;

  for (std::uint32_t sequence = effectiveFrom;; ++sequence) {
    std::uint32_t offset = 0;
    if (!offsetFor(sequence, offset)) return Status::failure(Error::InvalidArgument);
    const std::uint32_t mask = std::uint32_t{1} << offset;
    pendingBitmap_ &= ~mask;
    acceptedBitmap_ |= mask;
    advanceContiguous();
    if (sequence == throughSeq) break;
  }
  return Status::success();
}

}  // namespace remus::link
