#include "remus/diagnostics/HealthCounters.hpp"

#include <atomic>

namespace remus::diagnostics {
namespace {
constexpr std::memory_order kCounterMemoryOrder = std::memory_order_relaxed;
}

void HealthCounters::noteQueueOverflow() noexcept {
  queueOverflows_.fetch_add(1, kCounterMemoryOrder);
}

void HealthCounters::notePoolExhaustion() noexcept {
  poolExhaustions_.fetch_add(1, kCounterMemoryOrder);
}

void HealthCounters::noteInvalidHandleRelease() noexcept {
  invalidHandleReleases_.fetch_add(1, kCounterMemoryOrder);
}

void HealthCounters::noteRuntimeStart() noexcept {
  runtimeStarts_.fetch_add(1, kCounterMemoryOrder);
}

HealthSnapshot HealthCounters::snapshot() const noexcept {
  return {
      queueOverflows_.load(kCounterMemoryOrder),
      poolExhaustions_.load(kCounterMemoryOrder),
      invalidHandleReleases_.load(kCounterMemoryOrder),
      runtimeStarts_.load(kCounterMemoryOrder),
  };
}

}  // namespace remus::diagnostics
