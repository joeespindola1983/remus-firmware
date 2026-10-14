#pragma once

#include <cstdint>

#include "remus/core/Status.hpp"

namespace remus::link {

struct BindAcceptanceDecision {
  bool sendBindAck{false};
  bool reportCaptureFailure{false};
  std::uint16_t bindResultCode{0};
};

// A valid BIND confirms the radio/application association. Capture readiness is
// reported separately so an unavailable IMU cannot turn a healthy link into a
// misleading BIND timeout.
inline BindAcceptanceDecision decideBindAcceptance(Status bindStatus, bool captureRequested,
                                                    Status captureStatus) noexcept {
  if (!bindStatus.ok()) return {};
  return {true, captureRequested && !captureStatus.ok(), 0};
}

}  // namespace remus::link
