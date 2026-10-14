#pragma once

#include <cstdint>

namespace remus::gps {

constexpr std::int32_t kUnavailableCoordinateE7 = INT32_MIN;
constexpr std::uint32_t kUnavailableUnsigned32 = UINT32_MAX;
constexpr std::uint16_t kUnavailableUnsigned16 = UINT16_MAX;
constexpr std::uint8_t kUnavailableUnsigned8 = UINT8_MAX;

enum GpsFixFlags : std::uint16_t {
  kGpsFlagReceiverFixValid = 1U << 0U,
  kGpsFlagPositionValid = 1U << 1U,
  kGpsFlagSpeedValid = 1U << 2U,
  kGpsFlagCourseValid = 1U << 3U,
  kGpsFlagUtcValid = 1U << 4U,
  kGpsFlagHdopValid = 1U << 5U,
  kGpsFlagUbxCoherent = 1U << 6U,
};

struct NavigationEpoch {
  std::uint32_t gpsTimeOfWeekMs{0};
  std::int32_t latitudeE7{kUnavailableCoordinateE7};
  std::int32_t longitudeE7{kUnavailableCoordinateE7};
  std::uint32_t groundSpeedMillimetersPerSecond{kUnavailableUnsigned32};
  std::uint32_t speedAccuracyMillimetersPerSecond{kUnavailableUnsigned32};
  std::uint16_t courseCentiDegrees{kUnavailableUnsigned16};
  std::uint32_t courseAccuracyCentiDegrees{kUnavailableUnsigned32};
  std::uint32_t horizontalAccuracyMillimeters{kUnavailableUnsigned32};
  std::uint8_t fixType{0};
  bool receiverFixValid{false};
  std::uint64_t receivedAtPcMonotonicUs{0};
};

struct NmeaSnapshot {
  std::int32_t latitudeE7{kUnavailableCoordinateE7};
  std::int32_t longitudeE7{kUnavailableCoordinateE7};
  std::uint32_t speedMillimetersPerSecond{kUnavailableUnsigned32};
  std::uint16_t courseCentiDegrees{kUnavailableUnsigned16};
  std::uint8_t satellites{kUnavailableUnsigned8};
  std::uint8_t fixQuality{0};
  std::uint16_t hdopX100{kUnavailableUnsigned16};
  std::int64_t utcMilliseconds{0};
  std::uint64_t receivedAtPcMonotonicUs{0};
  std::uint32_t revision{0};
  bool positionValid{false};
  bool speedValid{false};
  bool courseValid{false};
  bool utcValid{false};
};

struct DecoderStats {
  std::uint64_t bytes{0};
  std::uint32_t frames{0};
  std::uint32_t checksumFailures{0};
  std::uint32_t oversizeFrames{0};
  std::uint32_t coherentEpochs{0};
  std::uint32_t overwrittenEpochs{0};
};

}  // namespace remus::gps
