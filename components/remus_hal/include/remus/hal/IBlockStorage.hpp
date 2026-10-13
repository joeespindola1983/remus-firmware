#pragma once

#include <cstddef>
#include <cstdint>

#include "remus/core/Status.hpp"

namespace remus::hal {

class IBlockStorage {
 public:
  virtual ~IBlockStorage() = default;
  virtual Status openNew(const char* path) noexcept = 0;
  virtual Status append(const std::uint8_t* data, std::size_t size,
                        std::size_t& written) noexcept = 0;
  virtual Status sync() noexcept = 0;
  virtual Status close() noexcept = 0;
  virtual Status openExistingRead(const char* path) noexcept = 0;
  virtual Status readAt(std::uint64_t offset, std::uint8_t* out, std::size_t capacity,
                        std::size_t& readBytes) noexcept = 0;
  virtual Status closeRead() noexcept = 0;
  virtual Status health() noexcept = 0;
};

}  // namespace remus::hal
