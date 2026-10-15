#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "remus/gps/Types.hpp"

namespace remus::gps {

class UbxNavigationDecoder {
 public:
  void feed(std::uint8_t byte, std::uint64_t receivedAtPcMonotonicUs) noexcept;
  bool take(NavigationEpoch& out) noexcept;
  DecoderStats stats() const noexcept { return stats_; }
  std::uint16_t acknowledgedConfigurationMask() const noexcept { return acknowledgedMask_; }
  std::uint16_t rejectedConfigurationMask() const noexcept { return rejectedMask_; }
  std::uint16_t measurementRateMs() const noexcept { return measurementRateMs_; }
  std::uint16_t navigationRate() const noexcept { return navigationRate_; }
  void resetConfigurationEvidence() noexcept {
    acknowledgedMask_ = 0;
    rejectedMask_ = 0;
    measurementRateMs_ = 0;
    navigationRate_ = 0;
  }

 private:
  enum class State : std::uint8_t { Sync1, Sync2, Class, Id, Length1, Length2, Payload, CkA, CkB };
  void resetFrame() noexcept;
  void processFrame(std::uint64_t receivedAtPcMonotonicUs) noexcept;
  void publishIfCoherent(std::uint64_t receivedAtPcMonotonicUs) noexcept;
  static std::uint32_t readU32(const std::uint8_t*) noexcept;
  static std::int32_t readI32(const std::uint8_t*) noexcept;
  static std::uint16_t configurationBit(std::uint8_t messageClass,
                                        std::uint8_t messageId) noexcept;

  static constexpr std::size_t kMaximumPayloadBytes = 64;
  State state_{State::Sync1};
  std::uint8_t messageClass_{0};
  std::uint8_t messageId_{0};
  std::uint16_t payloadLength_{0};
  std::uint16_t payloadIndex_{0};
  std::array<std::uint8_t, kMaximumPayloadBytes> payload_{};
  std::uint8_t checksumA_{0};
  std::uint8_t checksumB_{0};
  std::uint8_t receivedChecksumA_{0};
  bool payloadOversize_{false};
  DecoderStats stats_{};

  NavigationEpoch building_{};
  static constexpr std::size_t kReadyCapacity = 8;
  std::array<NavigationEpoch, kReadyCapacity> ready_{};
  std::size_t readyHead_{0};
  std::size_t readySize_{0};
  std::uint32_t positionTowMs_{UINT32_MAX};
  std::uint32_t velocityTowMs_{UINT32_MAX};
  std::uint32_t statusTowMs_{UINT32_MAX};
  std::uint32_t publishedTowMs_{UINT32_MAX};
  std::uint16_t acknowledgedMask_{0};
  std::uint16_t rejectedMask_{0};
  std::uint16_t measurementRateMs_{0};
  std::uint16_t navigationRate_{0};
};

}  // namespace remus::gps
