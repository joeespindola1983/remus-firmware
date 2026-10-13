#pragma once

#include <cstddef>
#include <cstdint>

#include "remus/core/Status.hpp"
#include "remus/core/Types.hpp"

namespace remus::hal {

struct RadioRxMeta {
  MacAddress source{};
  std::int8_t rssiDbm{kUnavailableRssiDbm};
  NativeTimestampUs localRxUs{0};
};

class IRadio {
 public:
  virtual ~IRadio() = default;
  virtual Status start(std::uint8_t channel) noexcept = 0;
  virtual Status addPeer(const MacAddress& address, const std::uint8_t linkKey[16]) noexcept = 0;
  virtual Status removePeer(const MacAddress& address) noexcept = 0;
  virtual Status requestSend(const MacAddress& destination, const std::uint8_t* data,
                             std::size_t size) noexcept = 0;
  virtual Status stop() noexcept = 0;
};

}  // namespace remus::hal
