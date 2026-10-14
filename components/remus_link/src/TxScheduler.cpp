#include "remus/link/TxScheduler.hpp"

#include <cstring>

namespace remus::link {
Status TxScheduler::enqueue(const MacAddress& destination, const std::uint8_t* bytes,
                            std::size_t size) noexcept {
  if (bytes == nullptr || size == 0 || size > kMaximumFrameBytes)
    return Status::failure(Error::InvalidArgument);
  if (size_ == queue_.size()) {
    ++stats_.queueDropped;
    return Status::failure(Error::QueueFull);
  }
  PendingFrame& frame = queue_[tail_];
  frame.destination = destination;
  frame.size = static_cast<std::uint16_t>(size);
  std::memcpy(frame.bytes.data(), bytes, size);
  tail_ = (tail_ + 1) % queue_.size();
  ++size_;
  ++stats_.queued;
  return Status::success();
}

Status TxScheduler::poll(NativeTimestampUs nowUs) noexcept {
  if (recoveryRequired_) return Status::failure(Error::NotReady);
  if (nowUs < quarantineUntilUs_) return Status::success();
  if (inFlight_) {
    if (nowUs >= requestedAtUs_ && nowUs - requestedAtUs_ >= kCallbackTimeoutUs) {
      inFlight_ = false;
      recoveryRequired_ = true;
      ++stats_.callbackTimeouts;
      return Status::failure(Error::Timeout);
    }
    return Status::success();
  }
  if (size_ == 0) return Status::success();
  PendingFrame& frame = queue_[head_];
  const Status status = radio_.requestSend(frame.destination, frame.bytes.data(), frame.size);
  if (status.ok()) {
    inFlight_ = true;
    requestedAtUs_ = nowUs;
    return status;
  }
  ++stats_.sdkRejected;
  return status;
}

void TxScheduler::resetAfterRadioRestart(NativeTimestampUs nowUs) noexcept {
  if (!recoveryRequired_) return;
  if (size_ > 0) {
    head_ = (head_ + 1) % queue_.size();
    --size_;
    ++stats_.failed;
  }
  inFlight_ = false;
  recoveryRequired_ = false;
  requestedAtUs_ = 0;
  quarantineUntilUs_ = nowUs + kPostRestartQuarantineUs;
  ++stats_.recoveryResets;
}

void TxScheduler::onSendComplete(const MacAddress& destination, bool delivered) noexcept {
  if (!inFlight_ || size_ == 0 || queue_[head_].destination != destination) {
    ++stats_.lateCallbacks;
    return;
  }
  delivered ? ++stats_.delivered : ++stats_.failed;
  head_ = (head_ + 1) % queue_.size();
  --size_;
  inFlight_ = false;
}
}  // namespace remus::link
