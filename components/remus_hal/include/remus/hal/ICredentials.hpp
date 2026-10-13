#pragma once

#include <cstddef>
#include <cstdint>

#include "remus/core/Status.hpp"
#include "remus/core/Types.hpp"

namespace remus::hal {

class ICredentials {
 public:
  virtual ~ICredentials() = default;
  virtual Status readDeviceSerialNumber(DeviceSerialNumber& out) noexcept = 0;
  virtual Status readNetworkId(NetworkId& out) noexcept = 0;
  virtual Status readNetworkSecret(std::uint8_t* out, std::size_t capacity,
                                   std::size_t& written) noexcept = 0;
};

}  // namespace remus::hal
