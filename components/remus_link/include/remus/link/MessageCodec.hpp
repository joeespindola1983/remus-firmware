#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "remus/core/Status.hpp"
#include "remus/core/Types.hpp"

namespace remus::link {
constexpr std::size_t kBeaconPayloadBytes = 28;
constexpr std::size_t kHelloPayloadBytes = 44;
constexpr std::size_t kBindPayloadBytes = 32;
constexpr std::size_t kBindAckPayloadBytes = 32;
constexpr std::size_t kSourceManifestPayloadBytes = 34;
constexpr std::size_t kDataBatchPrefixBytes = 16;
constexpr std::size_t kDataBatchSampleBytes = 16;
constexpr std::size_t kMaximumDataBatchSamples = 10;
constexpr std::size_t kMaximumDataBatchPayloadBytes =
    kDataBatchPrefixBytes + kMaximumDataBatchSamples * kDataBatchSampleBytes;

struct Beacon {
  ComputerId computerId{0};
  DeviceBootId pcBootId{0};
  std::uint32_t protocolCapabilities{0};
  std::uint32_t configFingerprint{0};
  std::uint32_t beaconSeq{0};
  std::uint8_t sessionState{0};
};
struct Hello {
  BladeId bladeId{0};
  DeviceBootId bladeBootId{0};
  ComputerId computerIdSeen{0};
  DeviceBootId pcBootIdSeen{0};
  std::uint32_t lastBeaconSeq{0};
  NativeTimestampUs beaconRxMonoUs{0};
  std::uint32_t capabilities{0};
  std::uint8_t peerReady{0};
};
struct Bind {
  BladeId bladeId{0};
  DeviceBootId bladeBootId{0};
  DeviceBootId pcBootId{0};
  RecordingId sessionId{0};
  std::uint32_t bindEpoch{0};
  std::uint8_t allowCapture{0};
};
struct BindAck {
  BladeId bladeId{0};
  DeviceBootId bladeBootId{0};
  DeviceBootId pcBootId{0};
  RecordingId sessionId{0};
  std::uint32_t bindEpoch{0};
  std::uint16_t resultCode{0};
};
struct SourceManifest {
  BladeId bladeId{0};
  DeviceBootId bladeBootId{0};
  std::uint16_t imuModel{0};
  std::uint8_t accelFsG{0};
  std::uint16_t gyroFsDps{0};
  std::uint16_t dlpfHz{0};
  std::uint16_t targetRateHz{0};
  std::uint8_t mounting{0};
  std::uint16_t hardwareRevision{0};
  std::uint8_t firmwareMajor{0};
  std::uint8_t firmwareMinor{0};
  std::uint16_t firmwarePatch{0};
  std::uint16_t lsbPerG{0};
  std::uint16_t lsbPerDpsX10{0};
};
struct DataBatchSample {
  std::uint16_t deltaTicks10us{0};
  std::int16_t ax{0};
  std::int16_t ay{0};
  std::int16_t az{0};
  std::int16_t gx{0};
  std::int16_t gy{0};
  std::int16_t gz{0};
  std::uint16_t flags{0};
};
struct DataBatch {
  SampleIndex firstSampleIndex{0};
  std::uint32_t batchSeq{0};
  std::uint16_t sampleCount{0};
  std::uint16_t nominalPeriodUs{0};
  std::array<DataBatchSample, kMaximumDataBatchSamples> samples{};
};

class MessageCodec {
 public:
  // Encode methods write an exact-size payload into caller-owned storage and
  // report validation failures without allocation or partial exposure.
  static Status encode(const Beacon&, std::uint8_t*, std::size_t, std::size_t&) noexcept;
  static Status encode(const Hello&, std::uint8_t*, std::size_t, std::size_t&) noexcept;
  static Status encode(const Bind&, std::uint8_t*, std::size_t, std::size_t&) noexcept;
  static Status encode(const BindAck&, std::uint8_t*, std::size_t, std::size_t&) noexcept;
  static Status encode(const SourceManifest&, std::uint8_t*, std::size_t, std::size_t&) noexcept;
  static Status encode(const DataBatch&, std::uint8_t*, std::size_t, std::size_t&) noexcept;
  // Decode methods require the exact normative payload length and zero-valued
  // reserved bytes. Callers use the result only when Status::ok().
  static Status decode(const std::uint8_t*, std::size_t, Beacon&) noexcept;
  static Status decode(const std::uint8_t*, std::size_t, Hello&) noexcept;
  static Status decode(const std::uint8_t*, std::size_t, Bind&) noexcept;
  static Status decode(const std::uint8_t*, std::size_t, BindAck&) noexcept;
  static Status decode(const std::uint8_t*, std::size_t, SourceManifest&) noexcept;
  static Status decode(const std::uint8_t*, std::size_t, DataBatch&) noexcept;
};
}  // namespace remus::link
