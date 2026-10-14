#include "remus/core/Config.hpp"

static_assert(remus::kMaximumParticipants == 8);
static_assert(remus::kNominalSamplingRateHertz == 200);

remus::Status remus::validateConnectionConfig(const RadioConfig& radio,
                                              const CapacityConfig& capacity) noexcept {
  if (radio.channel < 1 || radio.channel > 11 || capacity.maxParticipants == 0 ||
      capacity.maxParticipants > kMaximumParticipants || capacity.receivePoolCapacity == 0 ||
      capacity.receiveQueueCapacity == 0) {
    return Status::failure(Error::InvalidArgument);
  }
  if (radio.encryptedPeerCapacity < capacity.maxParticipants) {
    return Status::failure(Error::PeerFull, capacity.maxParticipants);
  }
  return Status::success();
}
