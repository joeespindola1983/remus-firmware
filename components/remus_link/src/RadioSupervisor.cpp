#include "remus/link/RadioSupervisor.hpp"

namespace remus::link {
namespace {
constexpr std::uint32_t kPersistentFailureCount = 3;
constexpr NativeTimestampUs kInitialBackoffUs = 1000000;
}  // namespace
void RadioSupervisor::observe(RadioHealthEvent event, NativeTimestampUs now) noexcept {
  lastEvent_ = event;
  switch (event) {
    case RadioHealthEvent::FrameDelivered:
      snapshot_.consecutiveFailures = 0;
      break;
    case RadioHealthEvent::DriverFailure:
      ++snapshot_.consecutiveFailures;
      ++snapshot_.totalFailures;
      if (snapshot_.consecutiveFailures >= kPersistentFailureCount) {
        snapshot_.degraded = true;
        snapshot_.reinitializeAfterUs = now + kInitialBackoffUs;
      }
      break;
    case RadioHealthEvent::PeerStalled:
      break;
    case RadioHealthEvent::Reinitialized:
      ++snapshot_.reinitializations;
      snapshot_.consecutiveFailures = 0;
      snapshot_.degraded = false;
      snapshot_.reinitializeAfterUs = 0;
      break;
  }
}
SupervisorAction RadioSupervisor::evaluate(NativeTimestampUs now) const noexcept {
  if (snapshot_.degraded && canReinitialize(now)) return SupervisorAction::ReinitializeDriver;
  if (snapshot_.degraded) return SupervisorAction::EnterDegraded;
  if (lastEvent_ == RadioHealthEvent::PeerStalled) return SupervisorAction::RebindPeer;
  if (lastEvent_ == RadioHealthEvent::DriverFailure) return SupervisorAction::RetryFrame;
  return SupervisorAction::None;
}
bool RadioSupervisor::canReinitialize(NativeTimestampUs now) const noexcept {
  return snapshot_.degraded && now >= snapshot_.reinitializeAfterUs;
}
}  // namespace remus::link
