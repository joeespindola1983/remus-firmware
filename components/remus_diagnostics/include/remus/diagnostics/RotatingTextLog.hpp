#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "remus/core/Status.hpp"

namespace remus::diagnostics {

struct RotatingTextLogConfig {
  const char* activePath{nullptr};
  const char* archivePath{nullptr};
  std::size_t segmentBytes{0};
  std::size_t maximumLineBytes{0};
};

struct RotatingTextLogStats {
  std::uint64_t activeBytes{0};
  std::uint32_t rotations{0};
  std::uint32_t appendFailures{0};
  std::uint32_t syncFailures{0};
};

class RotatingTextLog {
 public:
  explicit RotatingTextLog(RotatingTextLogConfig config) noexcept;
  ~RotatingTextLog();

  RotatingTextLog(const RotatingTextLog&) = delete;
  RotatingTextLog& operator=(const RotatingTextLog&) = delete;

  Status open() noexcept;
  Status appendLine(const char* line) noexcept;
  Status close() noexcept;
  bool isOpen() const noexcept { return file_ != nullptr; }
  RotatingTextLogStats stats() const noexcept { return stats_; }

 private:
  Status rotate() noexcept;
  Status sync() noexcept;

  RotatingTextLogConfig config_;
  std::FILE* file_{nullptr};
  RotatingTextLogStats stats_{};
};

}  // namespace remus::diagnostics
