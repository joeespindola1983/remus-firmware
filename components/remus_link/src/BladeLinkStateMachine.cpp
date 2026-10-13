#include "remus/link/BladeLinkStateMachine.hpp"

namespace remus::link {
void BladeLinkStateMachine::onRadioStarted() noexcept { state_ = BladeLinkState::Discovering; }
void BladeLinkStateMachine::onRadioFailed() noexcept { state_ = BladeLinkState::RadioDegraded; }
Status BladeLinkStateMachine::onAuthenticatedBeacon(ComputerId id, DeviceBootId boot) noexcept {
  if (id == 0 || boot == 0) return Status::failure(Error::InvalidArgument);
  if ((state_ == BladeLinkState::HelloReady || state_ == BladeLinkState::Bound) &&
      (id != computerId_ || boot != computerBootId_))
    return Status::failure(Error::Busy);
  if (state_ != BladeLinkState::Discovering && state_ != BladeLinkState::HelloReady &&
      state_ != BladeLinkState::Bound)
    return Status::failure(Error::NotReady);
  computerId_ = id;
  computerBootId_ = boot;
  if (state_ != BladeLinkState::Bound) state_ = BladeLinkState::HelloReady;
  return Status::success();
}
Status BladeLinkStateMachine::onBindAccepted(DeviceBootId boot, RecordingId recording,
                                             std::uint32_t epoch) noexcept {
  if (state_ == BladeLinkState::Bound) {
    return boot == computerBootId_ && recording == recordingId_ && epoch == bindEpoch_
               ? Status::success()
               : Status::failure(Error::InvalidArgument);
  }
  if (state_ != BladeLinkState::HelloReady || boot != computerBootId_)
    return Status::failure(Error::NotReady);
  if (epoch == 0) return Status::failure(Error::InvalidArgument);
  recordingId_ = recording;
  bindEpoch_ = epoch;
  state_ = BladeLinkState::Bound;
  return Status::success();
}
}  // namespace remus::link
