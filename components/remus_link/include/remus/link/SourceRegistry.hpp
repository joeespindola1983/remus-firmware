#pragma once

#include <array>
#include <cstddef>

#include "remus/core/Config.hpp"
#include "remus/core/Status.hpp"
#include "remus/core/Types.hpp"
#include "remus/link/ComputerPeerStateMachine.hpp"
#include "remus/link/KeyDerivation.hpp"

namespace remus::link {
struct SourceEntry {
  bool active{false};
  BladeId bladeId{0};
  DeviceBootId bladeBootId{0};
  MacAddress address{};
  ComputerPeerStateMachine state{};
  DerivedKeys pairKeys{};
  std::uint32_t bindEpoch{0};
  std::uint8_t bindAttempts{0};
  NativeTimestampUs nextBindAttemptUs{0};
  NativeTimestampUs lastSeenUs{0};
  bool staleReported{false};
};

class SourceRegistry {
 public:
  Status admit(BladeId, DeviceBootId, const MacAddress&, NativeTimestampUs) noexcept;
  SourceEntry* find(BladeId, DeviceBootId) noexcept;
  const SourceEntry* find(BladeId, DeviceBootId) const noexcept;
  SourceEntry* findByAddress(const MacAddress&) noexcept;
  SourceEntry* entryAt(std::size_t index) noexcept {
    return index < entries_.size() && entries_[index].active ? &entries_[index] : nullptr;
  }
  std::size_t expireCandidates(NativeTimestampUs nowUs, NativeTimestampUs lifetimeUs,
                               MacAddress* removed = nullptr,
                               std::size_t removedCapacity = 0) noexcept;
  std::size_t size() const noexcept { return size_; }
  std::size_t boundCount() const noexcept;
  std::size_t liveBoundCount(NativeTimestampUs nowUs,
                             NativeTimestampUs timeoutUs) const noexcept;
  static constexpr std::size_t capacity() noexcept { return kMaximumParticipants; }

 private:
  std::array<SourceEntry, kMaximumParticipants> entries_{};
  std::size_t size_{0};
};
}  // namespace remus::link
