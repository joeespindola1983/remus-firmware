#pragma once

#include <array>
#include <cstddef>
#include <utility>

namespace remus {

template <typename T, std::size_t Capacity>
class FixedQueue {
  static_assert(Capacity > 0, "FixedQueue capacity must be positive");

 public:
  constexpr std::size_t capacity() const noexcept { return Capacity; }
  std::size_t size() const noexcept { return size_; }
  bool empty() const noexcept { return size_ == 0; }
  bool full() const noexcept { return size_ == Capacity; }

  bool push(const T& value) noexcept(noexcept(storage_[tail_] = value)) {
    if (full()) {
      return false;
    }
    storage_[tail_] = value;
    advanceTail();
    return true;
  }

  bool push(T&& value) noexcept(noexcept(storage_[tail_] = std::move(value))) {
    if (full()) {
      return false;
    }
    storage_[tail_] = std::move(value);
    advanceTail();
    return true;
  }

  bool pop(T& out) noexcept(noexcept(out = std::move(storage_[head_]))) {
    if (empty()) {
      return false;
    }
    out = std::move(storage_[head_]);
    head_ = (head_ + 1U) % Capacity;
    --size_;
    return true;
  }

  void clear() noexcept {
    head_ = 0;
    tail_ = 0;
    size_ = 0;
  }

 private:
  void advanceTail() noexcept {
    tail_ = (tail_ + 1U) % Capacity;
    ++size_;
  }

  std::array<T, Capacity> storage_{};
  std::size_t head_{0};
  std::size_t tail_{0};
  std::size_t size_{0};
};

}  // namespace remus
