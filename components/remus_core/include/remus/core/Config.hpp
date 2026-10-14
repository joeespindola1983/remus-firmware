#pragma once

#include <cstddef>
#include <cstdint>

#include "remus/core/Status.hpp"

namespace remus {

constexpr std::uint16_t kNominalSamplingRateHertz = 200;
constexpr std::uint8_t kNominalSamplesPerBatch = 10;
constexpr std::uint8_t kMaximumParticipants = 8;

struct RadioConfig {
  std::uint8_t channel{1};
  std::uint8_t encryptedPeerCapacity{kMaximumParticipants};
};

struct SamplingConfig {
  std::uint16_t samplingRateHertz{kNominalSamplingRateHertz};
  std::uint8_t samplesPerBatch{kNominalSamplesPerBatch};
  std::uint8_t accelerometerRangeG{8};
  std::uint16_t gyroscopeRangeDegreesPerSecond{1000};
  std::uint16_t digitalLowPassFilterHertz{0};
};

struct CapacityConfig {
  std::uint8_t maxParticipants{kMaximumParticipants};
  std::size_t receivePoolCapacity{0};
  std::size_t receiveQueueCapacity{0};
};

// Validates the fixed radio channel, bounded queues, and encrypted-peer
// capacity before any radio resource is started.
Status validateConnectionConfig(const RadioConfig& radio, const CapacityConfig& capacity) noexcept;

}  // namespace remus
