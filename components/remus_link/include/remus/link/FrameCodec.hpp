#pragma once

#include <cstddef>
#include <cstdint>

#include "remus/core/Status.hpp"
#include "remus/hal/ICrypto.hpp"
#include "remus/link/Protocol.hpp"

namespace remus::link {
class FrameCodec {
 public:
  explicit FrameCodec(hal::ICrypto& crypto) noexcept : crypto_(crypto) {}

  // Encodes one complete frame into fixed caller-owned storage. Payload is
  // limited to 182 bytes; key bytes are read only and never retained.
  Status encode(const FrameHeader& header, const std::uint8_t* payload, std::size_t payloadSize,
                const std::uint8_t* authKey, std::size_t authKeySize, EncodedFrame& out) noexcept;
  // Authenticates a 68..250-byte frame before copying decoded fields to out.
  // The caller selects the key allowed by the current connection state.
  Status decode(const std::uint8_t* frame, std::size_t frameSize, const std::uint8_t* authKey,
                std::size_t authKeySize, DecodedFrame& out) noexcept;

 private:
  hal::ICrypto& crypto_;
};
}  // namespace remus::link
