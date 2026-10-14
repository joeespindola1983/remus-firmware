#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "remus/core/Status.hpp"
#include "remus/core/Types.hpp"
#include "remus/hal/IRadio.hpp"
#include "remus/link/Protocol.hpp"

namespace remus::link {
struct TxSchedulerStats {
  std::uint32_t queued{0};
  std::uint32_t queueDropped{0};
  std::uint32_t sdkRejected{0};
  std::uint32_t delivered{0};
  std::uint32_t failed{0};
  std::uint32_t lateCallbacks{0};
  std::uint32_t callbackTimeouts{0};
  std::uint32_t recoveryResets{0};
};

// Single-owner scheduler. Call enqueue, poll, and onSendComplete from one
// service execution context; it permits exactly one SDK transmission in flight.
class TxScheduler {
 public:
  explicit TxScheduler(hal::IRadio& radio) noexcept : radio_(radio) {}
  Status enqueue(const MacAddress&, const std::uint8_t*, std::size_t) noexcept;
  Status poll(NativeTimestampUs nowUs) noexcept;
  void onSendComplete(const MacAddress&, bool delivered) noexcept;
  void resetAfterRadioRestart(NativeTimestampUs nowUs) noexcept;
  bool inFlight() const noexcept { return inFlight_; }
  bool recoveryRequired() const noexcept { return recoveryRequired_; }
  std::size_t depth() const noexcept { return size_; }
  TxSchedulerStats stats() const noexcept { return stats_; }

 private:
  struct PendingFrame {
    MacAddress destination{};
    std::array<std::uint8_t, kMaximumFrameBytes> bytes{};
    std::uint16_t size{0};
  };
  static constexpr std::size_t kCapacity = 16;
  static constexpr NativeTimestampUs kCallbackTimeoutUs = 500000;
  static constexpr NativeTimestampUs kPostRestartQuarantineUs = 100000;
  hal::IRadio& radio_;
  std::array<PendingFrame, kCapacity> queue_{};
  std::size_t head_{0};
  std::size_t tail_{0};
  std::size_t size_{0};
  bool inFlight_{false};
  bool recoveryRequired_{false};
  NativeTimestampUs requestedAtUs_{0};
  NativeTimestampUs quarantineUntilUs_{0};
  TxSchedulerStats stats_{};
};
}  // namespace remus::link
