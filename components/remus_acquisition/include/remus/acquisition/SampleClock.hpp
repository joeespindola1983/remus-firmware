#pragma once

#include <cstdint>

#include "remus/core/Status.hpp"
#include "remus/core/Types.hpp"

namespace remus::acquisition {

struct DueSample {
  bool due{false};
  SampleIndex index{0};
  std::uint64_t missedPeriods{0};
  NativeTimestampUs scheduledAtUs{0};
};

class SampleClock {
 public:
  Status reset(std::uint16_t rateHz, NativeTimestampUs originUs = 0) noexcept;
  DueSample consumeDue(NativeTimestampUs nowUs) noexcept;
  std::uint32_t periodUs() const noexcept { return periodUs_; }

 private:
  std::uint32_t periodUs_{0};
  NativeTimestampUs nextDueUs_{0};
  SampleIndex nextIndex_{0};
};

}  // namespace remus::acquisition
