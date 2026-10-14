#pragma once

#include "remus/core/Status.hpp"
#include "remus/core/Types.hpp"

namespace remus::link {
enum class ComputerPeerState : std::uint8_t { Empty, Candidate, Registered, BindPending, Bound };
class ComputerPeerStateMachine {
 public:
  Status onAuthenticatedHello(BladeId, DeviceBootId, NativeTimestampUs) noexcept;
  Status onPeerRegistered() noexcept;
  Status onBindSent(std::uint32_t bindEpoch) noexcept;
  Status onBindAck(std::uint32_t bindEpoch) noexcept;
  ComputerPeerState state() const noexcept { return state_; }
  BladeId bladeId() const noexcept { return bladeId_; }
  DeviceBootId bladeBootId() const noexcept { return bladeBootId_; }
  NativeTimestampUs admittedAtUs() const noexcept { return admittedAtUs_; }

 private:
  ComputerPeerState state_{ComputerPeerState::Empty};
  BladeId bladeId_{0};
  DeviceBootId bladeBootId_{0};
  NativeTimestampUs admittedAtUs_{0};
  std::uint32_t bindEpoch_{0};
};
}  // namespace remus::link
