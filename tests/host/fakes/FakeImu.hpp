#pragma once

#include <array>

#include "remus/hal/IImu.hpp"

namespace remus::test {
class FakeImu final : public hal::IImu {
 public:
  Status initialize(const core::ImuProfile& profile) noexcept override {
    initializedProfile = profile;
    effective = {profile,
                 0x68,
                 0x68,
                 core::accelRegisterBits(profile.accelRange),
                 core::gyroRegisterBits(profile.gyroRange),
                 profile.dlpfCode,
                 static_cast<std::uint8_t>(1000U / profile.sampleRateHz - 1U),
                 core::accelLsbPerG(profile.accelRange),
                 core::gyroLsbPerDpsX10(profile.gyroRange),
                 true};
    return initializeStatus;
  }
  const core::EffectiveImuProfile& effectiveProfile() const noexcept override { return effective; }
  Status configureFifo(bool enabled) noexcept override {
    fifoEnabled = enabled;
    return Status::success();
  }
  Status read(hal::ImuRaw& out) noexcept override {
    ++readAttempts;
    if (!readStatus.ok()) return readStatus;
    out = next;
    return Status::success();
  }
  Status readFifo(hal::ImuRaw* out, std::size_t capacity, std::size_t& count) noexcept override {
    count = 0;
    if (!out || capacity == 0) return Status::failure(Error::InvalidArgument);
    out[0] = next;
    count = 1;
    return Status::success();
  }
  Status health() noexcept override { return Status::success(); }

  hal::ImuRaw next{1, 2, 3, 4, 5, 6};
  Status initializeStatus{};
  Status readStatus{};
  core::ImuProfile initializedProfile{};
  core::EffectiveImuProfile effective{};
  std::uint32_t readAttempts{0};
  bool fifoEnabled{false};
};
}  // namespace remus::test
