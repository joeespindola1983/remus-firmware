#pragma once

#include <cstddef>

namespace remus::hal {

// RX traffic is lossy by design. Keep bounded queue capacity available for the
// single in-flight TX completion, otherwise a receive burst can manufacture a
// callback timeout and trigger an unnecessary radio recovery.
constexpr bool canAdmitRadioReceiveEvent(std::size_t queueDepth,
                                         std::size_t queueCapacity,
                                         std::size_t reservedCompletions = 1) noexcept {
  return queueCapacity > reservedCompletions &&
         queueDepth < queueCapacity - reservedCompletions;
}

}  // namespace remus::hal
