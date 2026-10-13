#pragma once

#include "remus/core/Status.hpp"
#include "remus/diagnostics/HealthCounters.hpp"

namespace remus::computer {

class ComputerRuntime {
 public:
  Status start() noexcept;
  diagnostics::HealthSnapshot healthSnapshot() const noexcept;

 private:
  diagnostics::HealthCounters healthCounters_{};
  bool started_{false};
};

}  // namespace remus::computer
