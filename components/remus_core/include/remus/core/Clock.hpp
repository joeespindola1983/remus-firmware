#pragma once

#include "remus/core/Types.hpp"

namespace remus {

class IClock {
 public:
  virtual ~IClock() = default;
  virtual NativeTimestampUs monotonicUs() const noexcept = 0;
};

}  // namespace remus
