#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "remus/gps/Types.hpp"

namespace remus::gps {

class NmeaDecoder {
 public:
  void feed(std::uint8_t byte, std::uint64_t receivedAtPcMonotonicUs) noexcept;
  NmeaSnapshot snapshot() const noexcept { return snapshot_; }
  std::uint32_t validSentences() const noexcept { return validSentences_; }
  std::uint32_t checksumFailures() const noexcept { return checksumFailures_; }

 private:
  void parseLine(std::uint64_t receivedAtPcMonotonicUs) noexcept;
  static bool parseCoordinate(const char* value, const char* hemisphere,
                              bool latitude, std::int32_t& out) noexcept;
  static bool parseUnsigned(const char*, std::uint32_t&) noexcept;
  static bool parseDecimalScaled(const char*, std::uint32_t scale,
                                 std::uint32_t&) noexcept;
  static bool parseUtc(const char* time, const char* date,
                       std::int64_t& utcMilliseconds) noexcept;

  std::array<char, 128> line_{};
  std::size_t lineSize_{0};
  bool collecting_{false};
  NmeaSnapshot snapshot_{};
  std::uint32_t validSentences_{0};
  std::uint32_t checksumFailures_{0};
};

}  // namespace remus::gps
