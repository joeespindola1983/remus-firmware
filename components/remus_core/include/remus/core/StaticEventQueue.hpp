#pragma once

#include <cstddef>

#include "remus/core/FixedQueue.hpp"

namespace remus {

template <typename Event, std::size_t Capacity>
using StaticEventQueue = FixedQueue<Event, Capacity>;

}  // namespace remus
