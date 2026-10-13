#pragma once

#include <cstddef>
#include <cstdint>

#include "remus/core/Status.hpp"

namespace remus::hal {

class IGps {
 public:
  virtual ~IGps() = default;
  virtual Status start(std::uint32_t baudRate) noexcept = 0;
  virtual std::size_t readAvailable(std::uint8_t* destination, std::size_t capacity) noexcept = 0;
};

}  // namespace remus::hal
