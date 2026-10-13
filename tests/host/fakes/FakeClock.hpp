#pragma once

#include "remus/core/Clock.hpp"

namespace remus::test {

class FakeClock final : public IClock {
 public:
  NativeTimestampUs monotonicUs() const noexcept override { return nowUs_; }
  NativeTimestampUs now() const noexcept { return nowUs_; }
  void advanceUs(NativeTimestampUs deltaUs) noexcept { nowUs_ += deltaUs; }
  void setUs(NativeTimestampUs valueUs) noexcept { nowUs_ = valueUs; }

 private:
  NativeTimestampUs nowUs_{0};
};

}  // namespace remus::test
