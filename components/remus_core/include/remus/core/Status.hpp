#pragma once

#include <cstdint>

namespace remus {

enum class Error : std::uint8_t {
  Ok = 0,
  InvalidArgument,
  NotReady,
  Busy,
  QueueFull,
  Timeout,
  VersionMismatch,
  AuthFailed,
  BadCrc,
  PeerNotFound,
  PeerFull,
  StorageUnavailable,
  IoFailure,
  BufferOverrun,
  UnrecoverableGap,
  CryptoError,
  PoolExhausted,
  InvalidHandle,
};

struct Status {
  Error error{Error::Ok};
  std::uint32_t detail{0};

  constexpr bool ok() const noexcept { return error == Error::Ok; }
  static constexpr Status success() noexcept { return {}; }
  static constexpr Status failure(Error errorValue, std::uint32_t detailValue = 0) noexcept {
    return {errorValue, detailValue};
  }
};

}  // namespace remus
