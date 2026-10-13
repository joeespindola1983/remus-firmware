#include "BladeRuntime.hpp"

namespace remus::blade {

Status BladeRuntime::start() noexcept {
  if (started_) {
    return Status::success();
  }
  started_ = true;
  healthCounters_.noteRuntimeStart();
  return Status::success();
}

diagnostics::HealthSnapshot BladeRuntime::healthSnapshot() const noexcept {
  return healthCounters_.snapshot();
}

}  // namespace remus::blade
