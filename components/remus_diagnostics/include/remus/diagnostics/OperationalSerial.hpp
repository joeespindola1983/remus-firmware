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
  std::uint64_t gpsChecksumFailures{0};
  std::uint64_t gpsDecoderOverflows{0};
  std::uint64_t gpsRecordDrops{0};
  std::uint64_t replayOverflows{0};
  std::uint64_t replayUnavailable{0};
  std::uint64_t gapNotices{0};
};

// Produces one short Serial line containing only counters that increased.
// Returns false and writes an empty string while operation remains healthy.
bool formatNewOperationalProblems(const OperationalProblemCounters& previous,
                                  const OperationalProblemCounters& current,
                                  char* output, std::size_t outputCapacity) noexcept;

}  // namespace remus::diagnostics
