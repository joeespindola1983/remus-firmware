#pragma once

#include <cstddef>
#include <cstdint>

namespace remus {

using DeviceSerialNumber = std::uint64_t;
using BladeId = DeviceSerialNumber;
using ComputerId = DeviceSerialNumber;
using DeviceBootId = std::uint32_t;
using BootId = DeviceBootId;
using RecordingId = std::uint64_t;
using CaptureSessionId = RecordingId;
using NetworkId = std::uint32_t;
using StreamSeq = std::uint32_t;
using NativeTimestampUs = std::uint64_t;
using TimestampUs = NativeTimestampUs;
using SampleIndex = std::uint64_t;

constexpr DeviceSerialNumber kUnassignedDeviceSerialNumber = 0;
constexpr RecordingId kUnassignedRecordingId = 0;
constexpr std::uint8_t kUnassignedCrewSeatNumber = 0xFF;
constexpr std::int8_t kUnavailableRssiDbm = -127;

using CrewSeatNumber = std::uint8_t;

enum class BoatSide : std::uint8_t {
  Unknown = 0,
  Port = 1,
  Starboard = 2,
};

enum class RecordingStartReason : std::uint16_t {
  NormalStart = 1,
  DeviceRestarted = 2,
  SuspectedClockDiscontinuity = 3,
  StreamReconnect = 4,
  StreamStart = 5,
};

enum class RecordingEndReason : std::uint16_t {
  NormalStop = 1,
  DeviceRestarted = 2,
  SuspectedClockDiscontinuity = 3,
  StreamDisconnect = 4,
  StreamTimeout = 5,
  Interrupted = 6,
};

struct MacAddress {
  std::uint8_t bytes[6]{};

  bool operator==(const MacAddress& other) const noexcept;
  bool operator!=(const MacAddress& other) const noexcept { return !(*this == other); }
};

struct LinkStamp {
  DeviceBootId originBoot{0};
  RecordingId recording{kUnassignedRecordingId};
  NativeTimestampUs originMonoUs{0};
};

}  // namespace remus
