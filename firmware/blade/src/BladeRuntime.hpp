#pragma once

#include "remus/core/Status.hpp"
#include "remus/diagnostics/HealthCounters.hpp"

namespace remus::blade {

class BladeRuntime {
 public:
  Status start() noexcept;
  diagnostics::HealthSnapshot healthSnapshot() const noexcept;

 private:
  diagnostics::HealthCounters healthCounters_{};
  bool started_{false};
};

}  // namespace remus::blade
