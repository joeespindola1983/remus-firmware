#pragma once

#include "remus/core/Types.hpp"

namespace remus {

bool isAssigned(DeviceSerialNumber deviceSerialNumber) noexcept;
bool isAssignedRecording(RecordingId recordingId) noexcept;

// Modular ordering is valid only when compared values are within 2^31 steps.
bool sequenceAfter(StreamSeq candidate, StreamSeq reference) noexcept;
bool sequenceBefore(StreamSeq candidate, StreamSeq reference) noexcept;

}  // namespace remus
