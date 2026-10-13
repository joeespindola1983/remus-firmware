#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "remus/core/Status.hpp"

namespace remus {

template <typename T, std::size_t Capacity>
class PacketPool {
  static_assert(Capacity > 0, "PacketPool capacity must be positive");
  static_assert(Capacity <= UINT16_MAX, "PacketPool handle index is 16-bit");

 public:
  struct Handle {
    std::uint16_t index{UINT16_MAX};
    std::uint16_t generation{0};
  };

  constexpr std::size_t capacity() const noexcept { return Capacity; }
  std::size_t inUse() const noexcept { return inUse_; }

  Status acquire(Handle& out) noexcept {
    for (std::size_t i = 0; i < Capacity; ++i) {
      Slot& slot = slots_[i];
      if (!slot.occupied) {
        slot.occupied = true;
        ++inUse_;
        out = {static_cast<std::uint16_t>(i), slot.generation};
        return Status::success();
      }
    }
    return Status::failure(Error::PoolExhausted);
  }

  T* get(Handle handle) noexcept {
    Slot* slot = validSlot(handle);
    return slot == nullptr ? nullptr : &slot->value;
  }

  const T* get(Handle handle) const noexcept {
    const Slot* slot = validSlot(handle);
    return slot == nullptr ? nullptr : &slot->value;
  }

  Status release(Handle handle) noexcept {
    Slot* slot = validSlot(handle);
    if (slot == nullptr) {
      return Status::failure(Error::InvalidHandle);
    }
    slot->occupied = false;
    ++slot->generation;
    --inUse_;
    return Status::success();
  }

 private:
  struct Slot {
    T value{};
    std::uint16_t generation{1};
    bool occupied{false};
  };

  Slot* validSlot(Handle handle) noexcept {
    if (handle.index >= Capacity) {
      return nullptr;
    }
    Slot& slot = slots_[handle.index];
    return slot.occupied && slot.generation == handle.generation ? &slot : nullptr;
  }

  const Slot* validSlot(Handle handle) const noexcept {
    if (handle.index >= Capacity) {
      return nullptr;
    }
    const Slot& slot = slots_[handle.index];
    return slot.occupied && slot.generation == handle.generation ? &slot : nullptr;
  }

  std::array<Slot, Capacity> slots_{};
  std::size_t inUse_{0};
};

}  // namespace remus
