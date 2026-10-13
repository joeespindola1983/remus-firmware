#pragma once

#include <cstddef>

#include "remus/core/Status.hpp"

namespace remus::hal {

class IConsole {
 public:
  virtual ~IConsole() = default;
  virtual Status write(const char* data, std::size_t size) noexcept = 0;
};

}  // namespace remus::hal
