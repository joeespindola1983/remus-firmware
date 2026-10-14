#pragma once

#include <cstdint>

#include "remus/core/Types.hpp"

namespace remus::link {
enum class RadioHealthEvent : std::uint8_t {
  FrameDelivered,
  DriverFailure,
  PeerStalled,
  Reinitialized,
};
enum class SupervisorAction : std::uint8_t {
  None,
  RetryFrame,
  RebindPeer,
  EnterDegraded,
  ReinitializeDriver,
};
struct RadioSupervisorSnapshot {
  std::uint32_t consecutiveFailures{0};
  std::uint32_t totalFailures{0};
  std::uint32_t reinitializations{0};
  NativeTimestampUs reinitializeAfterUs{0};
  bool degraded{false};
};

// Evaluated only by the radio service owner. It records callback outcomes but
// never restarts a driver, blocks, allocates, or performs hardware I/O itself.
class RadioSupervisor {
 public:
  void observe(RadioHealthEvent, NativeTimestampUs nowUs) noexcept;
  SupervisorAction evaluate(NativeTimestampUs nowUs) const noexcept;
  bool canReinitialize(NativeTimestampUs nowUs) const noexcept;
  RadioSupervisorSnapshot snapshot() const noexcept { return snapshot_; }

 private:
  RadioSupervisorSnapshot snapshot_{};
  RadioHealthEvent lastEvent_{RadioHealthEvent::FrameDelivered};
};
}  // namespace remus::link
