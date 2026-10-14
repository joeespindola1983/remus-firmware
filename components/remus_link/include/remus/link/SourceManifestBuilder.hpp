#pragma once

#include "remus/core/ImuProfile.hpp"
#include "remus/link/MessageCodec.hpp"

namespace remus::link {

inline Status buildSourceManifest(BladeId sourceId, DeviceBootId bootId,
                                  const core::EffectiveImuProfile& profile, SourceManifest& out,
                                  std::uint8_t mounting = 0, std::uint16_t hardwareRevision = 0,
                                  std::uint8_t firmwareMajor = 0, std::uint8_t firmwareMinor = 0,
                                  std::uint16_t firmwarePatch = 0) noexcept {
  if (sourceId == 0 || bootId == 0 || !profile.verified) return Status::failure(Error::NotReady);
  if (!core::validateImuProfile(profile.requested).ok())
    return Status::failure(Error::InvalidArgument);
  const auto expectedDivider =
      static_cast<std::uint8_t>(1000U / profile.requested.sampleRateHz - 1U);
  if (profile.accelRegister != core::accelRegisterBits(profile.requested.accelRange) ||
      profile.gyroRegister != core::gyroRegisterBits(profile.requested.gyroRange) ||
      (profile.filterRegister & 0x07U) != profile.requested.dlpfCode ||
      profile.sampleDivider != expectedDivider ||
      profile.accelLsbPerG != core::accelLsbPerG(profile.requested.accelRange) ||
      profile.gyroLsbPerDpsX10 != core::gyroLsbPerDpsX10(profile.requested.gyroRange))
    return Status::failure(Error::InvalidArgument);
  out = {sourceId,
         bootId,
         profile.whoAmI,
         static_cast<std::uint8_t>(profile.requested.accelRange),
         static_cast<std::uint16_t>(profile.requested.gyroRange),
         profile.requested.gyroDlpfBandwidthHz(),
         profile.requested.sampleRateHz,
         mounting,
         hardwareRevision,
         firmwareMajor,
         firmwareMinor,
         firmwarePatch,
         profile.accelLsbPerG,
         profile.gyroLsbPerDpsX10};
  return Status::success();
}

}  // namespace remus::link
