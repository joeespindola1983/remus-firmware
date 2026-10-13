#pragma once

#include <atomic>
#include <cstdint>

#include "remus/diagnostics/HealthSnapshot.hpp"

namespace remus::diagnostics {

class HealthCounters {
 public:
  void noteQueueOverflow() noexcept;
  void notePoolExhaustion() noexcept;
  void noteInvalidHandleRelease() noexcept;
  void noteRuntimeStart() noexcept;
  HealthSnapshot snapshot() const noexcept;

 private:
  std::atomic<std::uint32_t> queueOverflows_{0};
  std::atomic<std::uint32_t> poolExhaustions_{0};
  std::atomic<std::uint32_t> invalidHandleReleases_{0};
  std::atomic<std::uint32_t> runtimeStarts_{0};
};

}  // namespace remus::diagnostics
