#include "remus/link/SourceRegistry.hpp"

namespace remus::link {
Status SourceRegistry::admit(BladeId id, DeviceBootId boot, const MacAddress& address,
                             NativeTimestampUs now) noexcept {
  if (id == 0 || boot == 0) return Status::failure(Error::InvalidArgument);
  SourceEntry* restarted = nullptr;
  for (auto& entry : entries_) {
    if (!entry.active) continue;
    if (entry.bladeId == id) {
      if (entry.address != address) return Status::failure(Error::AuthFailed);
      if (entry.bladeBootId == boot) {
        entry.lastSeenUs = now;
        return Status::success();
      }
      restarted = &entry;
      continue;
    }
    if (entry.address == address) return Status::failure(Error::AuthFailed);
  }
  if (restarted != nullptr) {
    SourceEntry replacement{};
    const Status status = replacement.state.onAuthenticatedHello(id, boot, now);
    if (!status.ok()) return status;
    replacement.active = true;
    replacement.bladeId = id;
    replacement.bladeBootId = boot;
    replacement.address = address;
    replacement.lastSeenUs = now;
    *restarted = replacement;
    return Status::success();
  }
  if (size_ == entries_.size()) return Status::failure(Error::PeerFull);
  for (auto& entry : entries_) {
    if (entry.active) continue;
    const Status status = entry.state.onAuthenticatedHello(id, boot, now);
    if (!status.ok()) return status;
    entry.active = true;
    entry.bladeId = id;
    entry.bladeBootId = boot;
    entry.address = address;
    entry.lastSeenUs = now;
    ++size_;
    return Status::success();
  }
  return Status::failure(Error::PeerFull);
}
SourceEntry* SourceRegistry::find(BladeId id, DeviceBootId boot) noexcept {
  for (auto& entry : entries_)
    if (entry.active && entry.bladeId == id && entry.bladeBootId == boot) return &entry;
  return nullptr;
}
const SourceEntry* SourceRegistry::find(BladeId id, DeviceBootId boot) const noexcept {
  for (const auto& entry : entries_)
    if (entry.active && entry.bladeId == id && entry.bladeBootId == boot) return &entry;
  return nullptr;
}
std::size_t SourceRegistry::boundCount() const noexcept {
  std::size_t count = 0;
  for (const auto& entry : entries_)
    if (entry.active && entry.state.state() == ComputerPeerState::Bound) ++count;
  return count;
}
std::size_t SourceRegistry::liveBoundCount(NativeTimestampUs now,
                                           NativeTimestampUs timeout) const noexcept {
  std::size_t count = 0;
  for (const auto& entry : entries_) {
    if (!entry.active || entry.state.state() != ComputerPeerState::Bound ||
        entry.lastSeenUs == 0 || now < entry.lastSeenUs || now - entry.lastSeenUs > timeout)
      continue;
    ++count;
  }
  return count;
}
SourceEntry* SourceRegistry::findByAddress(const MacAddress& address) noexcept {
  for (auto& entry : entries_)
    if (entry.active && entry.address == address) return &entry;
  return nullptr;
}
std::size_t SourceRegistry::expireCandidates(NativeTimestampUs now, NativeTimestampUs lifetime,
                                             MacAddress* removed,
                                             std::size_t removedCapacity) noexcept {
  std::size_t expired = 0;
  for (auto& entry : entries_) {
    if (!entry.active || entry.state.state() == ComputerPeerState::Bound) continue;
    const auto admitted = entry.state.admittedAtUs();
    if (now < admitted || now - admitted <= lifetime) continue;
    if (removed != nullptr && expired < removedCapacity) removed[expired] = entry.address;
    entry = SourceEntry{};
    --size_;
    ++expired;
  }
  return expired;
}
}  // namespace remus::link
