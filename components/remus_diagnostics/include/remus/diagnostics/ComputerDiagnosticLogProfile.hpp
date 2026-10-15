#pragma once

#include <cstddef>

namespace remus::diagnostics {

struct ComputerDiagnosticLogProfile {
  static constexpr std::size_t segmentBytes = 16U * 1024U * 1024U;
  static constexpr std::size_t segmentCount = 2;

  static constexpr std::size_t retentionBytes() noexcept {
    return segmentBytes * segmentCount;
  }
};

}  // namespace remus::diagnostics
