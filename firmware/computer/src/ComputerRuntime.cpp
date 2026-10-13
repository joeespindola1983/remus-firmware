#include "ComputerRuntime.hpp"

namespace remus::computer {

Status ComputerRuntime::start() noexcept {
  if (started_) {
    return Status::success();
  }
  started_ = true;
  healthCounters_.noteRuntimeStart();
  return Status::success();
}

diagnostics::HealthSnapshot ComputerRuntime::healthSnapshot() const noexcept {
  return healthCounters_.snapshot();
}

}  // namespace remus::computer
