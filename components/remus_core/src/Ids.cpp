#include "remus/core/Ids.hpp"

#include <cstdint>

namespace remus {

bool MacAddress::operator==(const MacAddress& other) const noexcept {
  for (std::size_t i = 0; i < sizeof(bytes); ++i) {
    if (bytes[i] != other.bytes[i]) {
      return false;
    }
  }
  return true;
}

bool isAssigned(DeviceSerialNumber deviceSerialNumber) noexcept {
  return deviceSerialNumber != kUnassignedDeviceSerialNumber;
}

bool isAssignedRecording(RecordingId recordingId) noexcept {
  return recordingId != kUnassignedRecordingId;
}

bool sequenceAfter(StreamSeq candidate, StreamSeq reference) noexcept {
  return static_cast<std::int32_t>(candidate - reference) > 0;
}

bool sequenceBefore(StreamSeq candidate, StreamSeq reference) noexcept {
  return sequenceAfter(reference, candidate);
}

}  // namespace remus
