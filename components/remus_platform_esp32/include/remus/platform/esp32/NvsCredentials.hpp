#pragma once

#include "remus/hal/ICredentials.hpp"

namespace remus::platform::esp32 {
class NvsCredentials final : public hal::ICredentials {
 public:
  explicit NvsCredentials(const char* nameSpace = "remus") noexcept : nameSpace_(nameSpace) {}
  // Reads only the exact credential keys from an existing NVS namespace.
  // These calls never initialize, erase, format, provision, or log NVS data.
  Status readDeviceSerialNumber(DeviceSerialNumber&) noexcept override;
  Status readNetworkId(NetworkId&) noexcept override;
  Status readNetworkSecret(std::uint8_t*, std::size_t, std::size_t&) noexcept override;

 private:
  const char* nameSpace_;
};
}  // namespace remus::platform::esp32
