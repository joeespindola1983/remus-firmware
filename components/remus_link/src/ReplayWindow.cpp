#include "remus/link/ReplayWindow.hpp"

#include <cstring>

namespace remus::link {

ReplayWindow::ReplayWindow(std::size_t capacity) noexcept
    : capacity_(capacity == 0 || capacity > kReplayWindowMaximumBatches
                    ? kReplayWindowMaximumBatches
                    : capacity) {}

const ReplayBatch* ReplayWindow::find(RecordingId recordingId,
                                      std::uint32_t batchSeq) const noexcept {
  for (std::size_t index = 0; index < size_; ++index) {
    const ReplayBatch& candidate = entries_[(head_ + index) % capacity_];
    if (candidate.recordingId == recordingId && candidate.batchSeq == batchSeq)
      return &candidate;
  }
  return nullptr;
}

ReplayAppendResult ReplayWindow::append(const ReplayBatch& batch) noexcept {
  if (batch.recordingId == 0 || batch.payloadSize == 0 ||
      batch.payloadSize > batch.payload.size())
    return {};
  if (activeRecordingId_ != 0 && activeRecordingId_ != batch.recordingId && size_ != 0)
    return {ReplayAppendDisposition::Rejected, {}, Status::failure(Error::InvalidArgument)};
  if (activeRecordingId_ != batch.recordingId) {
    activeRecordingId_ = batch.recordingId;
    acceptedValid_ = false;
    checkpointedValid_ = false;
    lostGapRecordingId_ = 0;
    lostGap_ = {};
  }
  if (find(batch.recordingId, batch.batchSeq) != nullptr)
    return {ReplayAppendDisposition::Duplicate, {}, Status::success()};

  ReplayGap gap{};
  ReplayAppendDisposition disposition = ReplayAppendDisposition::Stored;
  if (size_ == capacity_) {
    const ReplayBatch& evicted = entries_[head_];
    gap = {true, evicted.batchSeq, evicted.batchSeq, evicted.firstSampleIndex};
    head_ = (head_ + 1) % capacity_;
    --size_;
    ++overflowCount_;
    disposition = ReplayAppendDisposition::StoredWithGap;
    if (lostGap_.valid && lostGapRecordingId_ == batch.recordingId &&
        lostGap_.throughSeq + 1U == gap.fromSeq) {
      lostGap_.throughSeq = gap.throughSeq;
    } else {
      lostGapRecordingId_ = batch.recordingId;
      lostGap_ = gap;
    }
  }
  const std::size_t tail = (head_ + size_) % capacity_;
  entries_[tail] = batch;
  ++size_;
  return {disposition, gap, Status::success()};
}

bool ReplayWindow::knownLostGap(RecordingId recordingId, std::uint32_t batchSeq,
                                ReplayGap& gap) const noexcept {
  if (!lostGap_.valid || lostGapRecordingId_ != recordingId ||
      batchSeq < lostGap_.fromSeq || batchSeq > lostGap_.throughSeq)
    return false;
  gap = lostGap_;
  return true;
}

Status ReplayWindow::recordLostGap(RecordingId recordingId,
                                   const ReplayGap& gap) noexcept {
  if (recordingId == 0 || !gap.valid || gap.fromSeq > gap.throughSeq ||
      (activeRecordingId_ != 0 && activeRecordingId_ != recordingId))
    return Status::failure(Error::InvalidArgument);
  if (activeRecordingId_ == 0) activeRecordingId_ = recordingId;
  if (lostGap_.valid && lostGapRecordingId_ == recordingId &&
      lostGap_.throughSeq + 1U == gap.fromSeq) {
    lostGap_.throughSeq = gap.throughSeq;
  } else {
    lostGapRecordingId_ = recordingId;
    lostGap_ = gap;
  }
  return Status::success();
}

Status ReplayWindow::prepareReplay(RecordingId recordingId, std::uint32_t batchSeq,
                                   ReplayBatch& output) const noexcept {
  if (activeRecordingId_ != 0 && activeRecordingId_ != recordingId)
    return Status::failure(Error::InvalidArgument);
  const ReplayBatch* stored = find(recordingId, batchSeq);
  if (stored == nullptr) return Status::failure(Error::UnrecoverableGap);
  output = *stored;
  return Status::success();
}

Status ReplayWindow::markAcceptedThrough(RecordingId recordingId,
                                         std::uint32_t batchSeq) noexcept {
  if (activeRecordingId_ != 0 && activeRecordingId_ != recordingId)
    return Status::failure(Error::InvalidArgument);
  if (!acceptedValid_ || batchSeq > acceptedThrough_) {
    acceptedValid_ = true;
    acceptedThrough_ = batchSeq;
  }
  return Status::success();
}

Status ReplayWindow::markCheckpointedThrough(RecordingId recordingId,
                                             std::uint32_t batchSeq) noexcept {
  if (activeRecordingId_ != 0 && activeRecordingId_ != recordingId)
    return Status::failure(Error::InvalidArgument);
  if (!acceptedValid_ || batchSeq > acceptedThrough_)
    return Status::failure(Error::InvalidArgument);
  if (!checkpointedValid_ || batchSeq > checkpointedThrough_) {
    checkpointedValid_ = true;
    checkpointedThrough_ = batchSeq;
  }
  while (size_ != 0) {
    const ReplayBatch& oldest = entries_[head_];
    if (oldest.recordingId != recordingId || oldest.batchSeq > checkpointedThrough_) break;
    head_ = (head_ + 1) % capacity_;
    --size_;
  }
  return Status::success();
}

std::uint32_t ReplayWindow::oldestSeq() const noexcept {
  return size_ == 0 ? 0 : entries_[head_].batchSeq;
}

std::uint32_t ReplayWindow::newestSeq() const noexcept {
  return size_ == 0 ? 0 : entries_[(head_ + size_ - 1) % capacity_].batchSeq;
}

}  // namespace remus::link
