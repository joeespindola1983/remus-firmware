#pragma once

#include "remus/core/Status.hpp"
#include "remus/core/Types.hpp"

namespace remus::link {
enum class BladeLinkState : std::uint8_t { Boot, RadioDegraded, Discovering, HelloReady, Bound };

class BladeLinkStateMachine {
 public:
  void onRadioStarted() noexcept;
  void onRadioFailed() noexcept;
  Status onAuthenticatedBeacon(ComputerId computerId, DeviceBootId computerBootId) noexcept;
  Status onBindAccepted(DeviceBootId computerBootId, RecordingId recordingId,
                        std::uint32_t bindEpoch) noexcept;
  BladeLinkState state() const noexcept { return state_; }
  ComputerId computerId() const noexcept { return computerId_; }
  DeviceBootId computerBootId() const noexcept { return computerBootId_; }
  RecordingId recordingId() const noexcept { return recordingId_; }
  std::uint32_t bindEpoch() const noexcept { return bindEpoch_; }

 private:
  BladeLinkState state_{BladeLinkState::Boot};
  ComputerId computerId_{0};
  DeviceBootId computerBootId_{0};
  RecordingId recordingId_{0};
  std::uint32_t bindEpoch_{0};
};
}  // namespace remus::link
