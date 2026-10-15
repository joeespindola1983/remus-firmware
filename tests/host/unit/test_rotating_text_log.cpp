#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include <unistd.h>

#include "remus/diagnostics/ComputerDiagnosticLogProfile.hpp"
#include "remus/diagnostics/RotatingTextLog.hpp"

namespace {

int failures = 0;

void expect(bool condition, const char* expression, int line) {
  if (!condition) {
    std::cerr << "FAIL line " << line << ": " << expression << '\n';
    ++failures;
  }
}

#define EXPECT(expression) expect((expression), #expression, __LINE__)

std::string readAll(const std::filesystem::path& path) {
  std::ifstream input(path);
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void testComputerProfileRetainsExtendedBoundedHistory() {
  using remus::diagnostics::ComputerDiagnosticLogProfile;
  EXPECT(ComputerDiagnosticLogProfile::segmentBytes >= 16U * 1024U * 1024U);
  EXPECT(ComputerDiagnosticLogProfile::segmentCount == 2);
  EXPECT(ComputerDiagnosticLogProfile::retentionBytes() >= 32U * 1024U * 1024U);
}

void testRotationPreservesNewestBoundedSegments() {
  const std::filesystem::path directory =
      std::filesystem::temp_directory_path() /
      ("remus-rotating-log-" + std::to_string(static_cast<long>(getpid())));
  std::filesystem::create_directories(directory);
  const std::filesystem::path active = directory / "remus-pc.log";
  const std::filesystem::path archive = directory / "remus-pc.log.1";

  remus::diagnostics::RotatingTextLog log(
      {active.c_str(), archive.c_str(), 24, 16});
  EXPECT(log.open().ok());
  EXPECT(log.appendLine("first-0000").ok());
  EXPECT(log.appendLine("second-000").ok());
  EXPECT(log.appendLine("third-0000").ok());
  EXPECT(log.close().ok());

  EXPECT(std::filesystem::file_size(active) <= 24);
  EXPECT(std::filesystem::file_size(archive) <= 24);
  EXPECT(readAll(active).find("third-0000") != std::string::npos);
  EXPECT(readAll(archive).find("first-0000") != std::string::npos);
  EXPECT(log.stats().rotations == 1);

  std::filesystem::remove_all(directory);
}

void testRejectsOversizedLine() {
  const std::filesystem::path directory =
      std::filesystem::temp_directory_path() /
      ("remus-rotating-log-limit-" + std::to_string(static_cast<long>(getpid())));
  std::filesystem::create_directories(directory);
  const std::filesystem::path active = directory / "active.log";
  const std::filesystem::path archive = directory / "archive.log";

  remus::diagnostics::RotatingTextLog log({active.c_str(), archive.c_str(), 32, 8});
  EXPECT(log.open().ok());
  EXPECT(log.appendLine("123456789").error == remus::Error::BufferOverrun);
  EXPECT(log.close().ok());

  std::filesystem::remove_all(directory);
}

void testReopenAppendsExistingLog() {
  const std::filesystem::path directory =
      std::filesystem::temp_directory_path() /
      ("remus-rotating-log-reopen-" + std::to_string(static_cast<long>(getpid())));
  std::filesystem::create_directories(directory);
  const std::filesystem::path active = directory / "active.log";
  const std::filesystem::path archive = directory / "archive.log";

  {
    remus::diagnostics::RotatingTextLog log({active.c_str(), archive.c_str(), 64, 24});
    EXPECT(log.open().ok());
    EXPECT(log.appendLine("before-restart").ok());
  }
  {
    remus::diagnostics::RotatingTextLog log({active.c_str(), archive.c_str(), 64, 24});
    EXPECT(log.open().ok());
    EXPECT(log.appendLine("after-restart").ok());
  }

  const std::string contents = readAll(active);
  EXPECT(contents.find("before-restart") != std::string::npos);
  EXPECT(contents.find("after-restart") != std::string::npos);
  std::filesystem::remove_all(directory);
}

void testOpenRotatesWhenMaximumLineCannotFit() {
  const std::filesystem::path directory =
      std::filesystem::temp_directory_path() /
      ("remus-rotating-log-open-capacity-" +
       std::to_string(static_cast<long>(getpid())));
  std::filesystem::create_directories(directory);
  const std::filesystem::path active = directory / "active.log";
  const std::filesystem::path archive = directory / "archive.log";
  const std::string previous(58, 'x');
  {
    std::ofstream output(active, std::ios::binary);
    output << previous;
  }

  remus::diagnostics::RotatingTextLog log(
      {active.c_str(), archive.c_str(), 64, 16});
  EXPECT(log.open().ok());
  EXPECT(log.stats().rotations == 1);
  EXPECT(std::filesystem::file_size(active) == 0);
  EXPECT(readAll(archive) == previous);
  EXPECT(log.appendLine("new-entry").ok());
  EXPECT(log.close().ok());

  std::filesystem::remove_all(directory);
}

void testCanDeferDurabilityToPeriodicCheckpoint() {
  const std::filesystem::path directory =
      std::filesystem::temp_directory_path() /
      ("remus-rotating-log-deferred-sync-" +
       std::to_string(static_cast<long>(getpid())));
  std::filesystem::create_directories(directory);
  const std::filesystem::path active = directory / "active.log";
  const std::filesystem::path archive = directory / "archive.log";

  remus::diagnostics::RotatingTextLog log(
      {active.c_str(), archive.c_str(), 64, 24, false});
  EXPECT(log.open().ok());
  EXPECT(log.appendLine("batched-entry").ok());
  EXPECT(log.stats().syncs == 0);
  EXPECT(log.sync().ok());
  EXPECT(log.stats().syncs == 1);
  EXPECT(log.close().ok());

  std::filesystem::remove_all(directory);
}

}  // namespace

int main() {
  testComputerProfileRetainsExtendedBoundedHistory();
  testRotationPreservesNewestBoundedSegments();
  testRejectsOversizedLine();
  testReopenAppendsExistingLog();
  testOpenRotatesWhenMaximumLineCannotFit();
  testCanDeferDurabilityToPeriodicCheckpoint();
  if (failures == 0) {
    std::cout << "rotating text log tests passed\n";
  }
  return failures == 0 ? 0 : 1;
}
