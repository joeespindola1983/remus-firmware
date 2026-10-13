#pragma once

#include <cstdint>

namespace remus::diagnostics {

struct HealthSnapshot {
  std::uint32_t queueOverflows{0};
  std::uint32_t poolExhaustions{0};
  std::uint32_t invalidHandleReleases{0};
  std::uint32_t runtimeStarts{0};
};

}  // namespace remus::diagnostics
