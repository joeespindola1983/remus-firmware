#pragma once

#include <cstddef>
#include <cstdint>

namespace remus::diagnostics {

// Only fault counters belong here. Throughput, uptime, memory and other
// routine telemetry stay in the durable SD diagnostic log.
struct OperationalProblemCounters {
  std::uint64_t queueOverflows{0};
  std::uint64_t poolExhaustions{0};
  std::uint64_t invalidHandleReleases{0};
  std::uint64_t sdLogDropped{0};
  std::uint64_t sdLogWriteFailures{0};
  std::uint64_t imuReadErrors{0};
  std::uint64_t imuMissedPeriods{0};
  std::uint64_t rmsDropped{0};
  std::uint64_t rmsWriteFailures{0};
  std::uint64_t duplicateBatches{0};
  std::uint64_t outsideWindowBatches{0};
  std::uint64_t unrecoverableGaps{0};
};

struct ImuRawPreview {
  std::int16_t ax{0};
  std::int16_t ay{0};
  std::int16_t az{0};
  std::int16_t gx{0};
  std::int16_t gy{0};
  std::int16_t gz{0};
  std::uint16_t flags{0};
};

constexpr bool shouldEmitImuPreview(std::uint64_t completedBatchCount) noexcept {
  return completedBatchCount != 0 && completedBatchCount % 20 == 0;
}

// A zero boot ID omits the boot field for the Computer-local source.
bool formatImuRawPreview(const char* source, std::uint32_t bootId,
                         const ImuRawPreview& sample, char* output,
                         std::size_t outputCapacity) noexcept;

// Produces one short Serial line containing only counters that increased.
// Returns false and writes an empty string while operation remains healthy.
bool formatNewOperationalProblems(const OperationalProblemCounters& previous,
                                  const OperationalProblemCounters& current,
                                  char* output, std::size_t outputCapacity) noexcept;

}  // namespace remus::diagnostics
