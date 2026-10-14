#include "remus/link/ComputerPeerStateMachine.hpp"
namespace remus::link {
Status ComputerPeerStateMachine::onAuthenticatedHello(BladeId id, DeviceBootId boot,
                                                      NativeTimestampUs now) noexcept {
  if (id == 0 || boot == 0) return Status::failure(Error::InvalidArgument);
  if (state_ != ComputerPeerState::Empty) {
    return id == bladeId_ && boot == bladeBootId_ ? Status::success()
                                                  : Status::failure(Error::AuthFailed);
  }
  bladeId_ = id;
  bladeBootId_ = boot;
  admittedAtUs_ = now;
  state_ = ComputerPeerState::Candidate;
  return Status::success();
}
Status ComputerPeerStateMachine::onPeerRegistered() noexcept {
  if (state_ != ComputerPeerState::Candidate) return Status::failure(Error::NotReady);
  state_ = ComputerPeerState::Registered;
  return Status::success();
}
Status ComputerPeerStateMachine::onBindSent(std::uint32_t epoch) noexcept {
  if (state_ != ComputerPeerState::Registered || epoch == 0)
    return Status::failure(Error::NotReady);
  bindEpoch_ = epoch;
  state_ = ComputerPeerState::BindPending;
  return Status::success();
}
Status ComputerPeerStateMachine::onBindAck(std::uint32_t epoch) noexcept {
  if (state_ != ComputerPeerState::BindPending) return Status::failure(Error::NotReady);
  if (epoch != bindEpoch_) return Status::failure(Error::InvalidArgument);
  state_ = ComputerPeerState::Bound;
  return Status::success();
}
}  // namespace remus::link
