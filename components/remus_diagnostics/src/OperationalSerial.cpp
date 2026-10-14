#include "remus/diagnostics/OperationalSerial.hpp"

#include <cstdio>

namespace remus::diagnostics {
namespace {

bool appendIncrease(char* output, std::size_t capacity, std::size_t& used,
                    const char* name, std::uint64_t previous,
                    std::uint64_t current) noexcept {
  if (current <= previous) return false;
  if (used < capacity) {
    const int written = std::snprintf(output + used, capacity - used, " %s=+%llu", name,
                                      static_cast<unsigned long long>(current - previous));
    if (written > 0) {
      const auto count = static_cast<std::size_t>(written);
      used += count < capacity - used ? count : capacity - used - 1;
    }
  }
  return true;
}

}  // namespace

bool formatNewOperationalProblems(const OperationalProblemCounters& previous,
                                  const OperationalProblemCounters& current,
                                  char* output, std::size_t outputCapacity) noexcept {
  if (output == nullptr || outputCapacity == 0) return false;
  output[0] = '\0';
  std::size_t used = 0;
  bool found = false;

  const int prefix = std::snprintf(output, outputCapacity, "problems:");
  if (prefix > 0) {
    const auto count = static_cast<std::size_t>(prefix);
    used = count < outputCapacity ? count : outputCapacity - 1;
  }

#define REMUS_APPEND_PROBLEM(field, label)                                      \
  found = appendIncrease(output, outputCapacity, used, label, previous.field,   \
                         current.field) ||                                      \
          found
  REMUS_APPEND_PROBLEM(queueOverflows, "queue_overflows");
  REMUS_APPEND_PROBLEM(poolExhaustions, "pool_exhaustions");
  REMUS_APPEND_PROBLEM(invalidHandleReleases, "invalid_handle_releases");
  REMUS_APPEND_PROBLEM(sdLogDropped, "sd_log_dropped");
  REMUS_APPEND_PROBLEM(sdLogWriteFailures, "sd_log_write_failures");
  REMUS_APPEND_PROBLEM(imuReadErrors, "imu_read_errors");
  REMUS_APPEND_PROBLEM(imuMissedPeriods, "imu_missed_periods");
  REMUS_APPEND_PROBLEM(rmsDropped, "rms_dropped");
  REMUS_APPEND_PROBLEM(rmsWriteFailures, "rms_write_failures");
  REMUS_APPEND_PROBLEM(duplicateBatches, "duplicate_batches");
  REMUS_APPEND_PROBLEM(outsideWindowBatches, "outside_window_batches");
  REMUS_APPEND_PROBLEM(unrecoverableGaps, "unrecoverable_gaps");
#undef REMUS_APPEND_PROBLEM

  if (!found) output[0] = '\0';
  return found;
}

}  // namespace remus::diagnostics
