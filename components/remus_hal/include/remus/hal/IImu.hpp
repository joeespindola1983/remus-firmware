#pragma once

#include <cstddef>
#include <cstdint>

#include "remus/core/ImuProfile.hpp"
#include "remus/core/Status.hpp"

namespace remus::hal {

struct ImuRaw {
  std::int16_t ax{0};
  std::int16_t ay{0};
  std::int16_t az{0};
  std::int16_t gx{0};
  std::int16_t gy{0};
  std::int16_t gz{0};
};

class IImu {
 public:
  virtual ~IImu() = default;
  virtual Status initialize(const core::ImuProfile& profile) noexcept = 0;
  virtual const core::EffectiveImuProfile& effectiveProfile() const noexcept = 0;
  virtual Status configureFifo(bool enabled) noexcept = 0;
  virtual Status read(ImuRaw& out) noexcept = 0;
  virtual Status readFifo(ImuRaw* destination, std::size_t maxSamples,
                          std::size_t& readSamples) noexcept = 0;
  virtual Status health() noexcept = 0;
};

}  // namespace remus::hal
