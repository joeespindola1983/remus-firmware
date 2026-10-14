#pragma once

#include <cstdint>

#include "remus/core/Status.hpp"
#include "remus/link/MessageCodec.hpp"

namespace remus::link {

enum class AcceptDisposition : std::uint8_t { New, Duplicate, OutsideWindow };

struct AcceptResult {
  AcceptDisposition disposition{AcceptDisposition::OutsideWindow};
  bool gapDetected{false};
};

// Tracks one Blade stream. The owner must call accept() before transferring a
// batch to its bounded writer queue, then markQueued() only after that transfer
// succeeds. reject() returns a failed transfer to the recoverable state.
class AckTracker {
 public:
  AcceptResult accept(std::uint32_t batchSeq) noexcept;
  Status markQueued(std::uint32_t batchSeq) noexcept;
  Status reject(std::uint32_t batchSeq) noexcept;
  Status markCheckpointedThrough(std::uint32_t batchSeq) noexcept;
  AckWindow buildAckWindow(BladeId bladeId, DeviceBootId bladeBootId) const noexcept;
  bool nextReplayRequest(BladeId bladeId, DeviceBootId bladeBootId,
                         ReplayRequest& request) const noexcept;
  bool canSealGap(std::uint32_t fromSeq, std::uint32_t throughSeq) const noexcept;
  Status sealGap(std::uint32_t fromSeq, std::uint32_t throughSeq) noexcept;
  std::uint32_t acceptedCount() const noexcept { return acceptedCount_; }

 private:
  bool offsetFor(std::uint32_t batchSeq, std::uint32_t& offset) const noexcept;
  void advanceContiguous() noexcept;

  bool acceptedValid_{false};
  std::uint32_t acceptedThrough_{0};
  std::uint32_t acceptedBitmap_{0};
  std::uint32_t pendingBitmap_{0};
  std::uint32_t acceptedCount_{0};
  bool highestObservedValid_{false};
  std::uint32_t highestObserved_{0};
  bool checkpointedValid_{false};
  std::uint32_t checkpointedThrough_{0};
};

}  // namespace remus::link
