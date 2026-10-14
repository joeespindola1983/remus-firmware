#include <cstring>
#include <iostream>

#include "remus/diagnostics/OperationalSerial.hpp"

namespace {

int failures = 0;

void expect(bool condition, const char* expression, int line) {
  if (!condition) {
    std::cerr << "FAIL line " << line << ": " << expression << '\n';
    ++failures;
  }
}

#define EXPECT(expression) expect((expression), #expression, __LINE__)

void testHealthySnapshotsStaySilent() {
  const remus::diagnostics::OperationalProblemCounters previous{};
  const remus::diagnostics::OperationalProblemCounters current{};
  char line[192]{};

  EXPECT(!remus::diagnostics::formatNewOperationalProblems(previous, current, line,
                                                            sizeof(line)));
  EXPECT(line[0] == '\0');
}

void testOnlyNewProblemsArePrinted() {
  remus::diagnostics::OperationalProblemCounters previous{};
  previous.imuReadErrors = 4;
  previous.sdLogDropped = 2;
  remus::diagnostics::OperationalProblemCounters current = previous;
  current.imuReadErrors = 7;
  current.rmsWriteFailures = 1;
  char line[192]{};

  EXPECT(remus::diagnostics::formatNewOperationalProblems(previous, current, line,
                                                           sizeof(line)));
  EXPECT(std::strstr(line, "imu_read_errors=+3") != nullptr);
  EXPECT(std::strstr(line, "rms_write_failures=+1") != nullptr);
  EXPECT(std::strstr(line, "sd_log_dropped") == nullptr);
  EXPECT(std::strstr(line, "=+0") == nullptr);
}

void testAllFaultClassesCanBeReported() {
  const remus::diagnostics::OperationalProblemCounters previous{};
  remus::diagnostics::OperationalProblemCounters current{};
  current.queueOverflows = 1;
  current.poolExhaustions = 2;
  current.invalidHandleReleases = 3;
  current.sdLogDropped = 4;
  current.sdLogWriteFailures = 5;
  current.imuReadErrors = 6;
  current.imuMissedPeriods = 7;
  current.rmsDropped = 8;
  current.rmsWriteFailures = 9;
  current.duplicateBatches = 10;
  current.outsideWindowBatches = 11;
  current.unrecoverableGaps = 12;
  char line[512]{};

  EXPECT(remus::diagnostics::formatNewOperationalProblems(previous, current, line,
                                                           sizeof(line)));
  EXPECT(std::strstr(line, "queue_overflows=+1") != nullptr);
  EXPECT(std::strstr(line, "pool_exhaustions=+2") != nullptr);
  EXPECT(std::strstr(line, "invalid_handle_releases=+3") != nullptr);
  EXPECT(std::strstr(line, "sd_log_dropped=+4") != nullptr);
  EXPECT(std::strstr(line, "sd_log_write_failures=+5") != nullptr);
  EXPECT(std::strstr(line, "imu_read_errors=+6") != nullptr);
  EXPECT(std::strstr(line, "imu_missed_periods=+7") != nullptr);
  EXPECT(std::strstr(line, "rms_dropped=+8") != nullptr);
  EXPECT(std::strstr(line, "rms_write_failures=+9") != nullptr);
  EXPECT(std::strstr(line, "duplicate_batches=+10") != nullptr);
  EXPECT(std::strstr(line, "outside_window_batches=+11") != nullptr);
  EXPECT(std::strstr(line, "unrecoverable_gaps=+12") != nullptr);
}

void testImuPreviewIsOneCompactRawSampleLine() {
  const remus::diagnostics::ImuRawPreview sample{101, -202, 303, -404, 505, -606, 3};
  char localLine[160]{};
  char bladeLine[160]{};

  EXPECT(remus::diagnostics::formatImuRawPreview("PC", 0, sample, localLine,
                                                 sizeof(localLine)));
  EXPECT(std::strcmp(localLine,
                     "IMU PC raw ax=101 ay=-202 az=303 gx=-404 gy=505 gz=-606 flags=0x0003") ==
         0);
  EXPECT(remus::diagnostics::formatImuRawPreview("BLADE", 0x12ab34cd, sample, bladeLine,
                                                 sizeof(bladeLine)));
  EXPECT(std::strcmp(bladeLine,
                     "IMU BLADE boot=12ab34cd raw ax=101 ay=-202 az=303 gx=-404 gy=505 "
                     "gz=-606 flags=0x0003") == 0);
}

void testImuPreviewCadenceIsOncePerSecondAtTwentyBatches() {
  EXPECT(!remus::diagnostics::shouldEmitImuPreview(0));
  EXPECT(!remus::diagnostics::shouldEmitImuPreview(19));
  EXPECT(remus::diagnostics::shouldEmitImuPreview(20));
  EXPECT(!remus::diagnostics::shouldEmitImuPreview(21));
  EXPECT(remus::diagnostics::shouldEmitImuPreview(40));
}

}  // namespace

int main() {
  testHealthySnapshotsStaySilent();
  testOnlyNewProblemsArePrinted();
  testAllFaultClassesCanBeReported();
  testImuPreviewIsOneCompactRawSampleLine();
  testImuPreviewCadenceIsOncePerSecondAtTwentyBatches();
  if (failures == 0) std::cout << "operational serial tests passed\n";
  return failures == 0 ? 0 : 1;
}
