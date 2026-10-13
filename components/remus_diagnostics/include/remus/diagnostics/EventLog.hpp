#pragma once

#include <cstddef>
#include <cstdint>

#include "remus/core/FixedQueue.hpp"
#include "remus/core/Types.hpp"

namespace remus::diagnostics {

enum class EventCode : std::uint16_t {
  RuntimeStarted = 1,
  QueueOverflow = 2,
  PoolExhausted = 3,
  InvalidHandleRelease = 4,
};

struct EventRecord {
  NativeTimestampUs nativeTimestampUs{0};
  EventCode code{EventCode::RuntimeStarted};
  std::uint32_t detail{0};
};

template <std::size_t Capacity>
using EventLog = FixedQueue<EventRecord, Capacity>;

}  // namespace remus::diagnostics
