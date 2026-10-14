#include "remus/link/MessageCodec.hpp"

#include <cstring>

namespace remus::link {
namespace {
class Writer {
 public:
  Writer(std::uint8_t* data, std::size_t capacity) : data_(data), capacity_(capacity) {}
  bool u8(std::uint8_t v) {
    if (pos_ + 1 > capacity_) return false;
    data_[pos_++] = v;
    return true;
  }
  bool u16(std::uint16_t v) { return u8(v) && u8(v >> 8); }
  bool i16(std::int16_t v) { return u16(static_cast<std::uint16_t>(v)); }
  bool u32(std::uint32_t v) {
    for (int i = 0; i < 4; ++i)
      if (!u8(v >> (8 * i))) return false;
    return true;
  }
  bool u64(std::uint64_t v) {
    for (int i = 0; i < 8; ++i)
      if (!u8(v >> (8 * i))) return false;
    return true;
  }
  bool zeros(std::size_t n) {
    while (n--)
      if (!u8(0)) return false;
    return true;
  }
  std::size_t size() const { return pos_; }

 private:
  std::uint8_t* data_;
  std::size_t capacity_;
  std::size_t pos_{0};
};
class Reader {
 public:
  Reader(const std::uint8_t* data, std::size_t size) : data_(data), size_(size) {}
  bool u8(std::uint8_t& v) {
    if (pos_ + 1 > size_) return false;
    v = data_[pos_++];
    return true;
  }
  bool u16(std::uint16_t& v) {
    std::uint8_t a, b;
    if (!u8(a) || !u8(b)) return false;
    v = static_cast<std::uint16_t>(a) | (static_cast<std::uint16_t>(b) << 8);
    return true;
  }
  bool i16(std::int16_t& v) {
    std::uint16_t encoded = 0;
    if (!u16(encoded)) return false;
    v = static_cast<std::int16_t>(encoded);
    return true;
  }
  bool u32(std::uint32_t& v) {
    v = 0;
    for (int i = 0; i < 4; ++i) {
      std::uint8_t x;
      if (!u8(x)) return false;
      v |= static_cast<std::uint32_t>(x) << (8 * i);
    }
    return true;
  }
  bool u64(std::uint64_t& v) {
    v = 0;
    for (int i = 0; i < 8; ++i) {
      std::uint8_t x;
      if (!u8(x)) return false;
      v |= static_cast<std::uint64_t>(x) << (8 * i);
    }
    return true;
  }
  bool reserved(std::size_t n) {
    while (n--) {
      std::uint8_t x;
      if (!u8(x) || x != 0) return false;
    }
    return true;
  }

 private:
  const std::uint8_t* data_;
  std::size_t size_;
  std::size_t pos_{0};
};
Status invalid() { return Status::failure(Error::InvalidArgument); }
bool valid(const void* p, std::size_t capacity, std::size_t need) {
  return p != nullptr && capacity >= need;
}
}  // namespace

Status MessageCodec::encode(const Beacon& v, std::uint8_t* out, std::size_t cap,
                            std::size_t& n) noexcept {
  if (!valid(out, cap, kBeaconPayloadBytes)) return invalid();
  Writer w(out, cap);
  if (!w.u64(v.computerId) || !w.u32(v.pcBootId) || !w.u32(v.protocolCapabilities) ||
      !w.u32(v.configFingerprint) || !w.u32(v.beaconSeq) || !w.u8(v.sessionState) || !w.zeros(3))
    return invalid();
  n = w.size();
  return Status::success();
}
Status MessageCodec::decode(const std::uint8_t* in, std::size_t n, Beacon& v) noexcept {
  if (!valid(in, n, kBeaconPayloadBytes) || n != kBeaconPayloadBytes) return invalid();
  Reader r(in, n);
  return r.u64(v.computerId) && r.u32(v.pcBootId) && r.u32(v.protocolCapabilities) &&
                 r.u32(v.configFingerprint) && r.u32(v.beaconSeq) && r.u8(v.sessionState) &&
                 r.reserved(3)
             ? Status::success()
             : invalid();
}

Status MessageCodec::encode(const Hello& v, std::uint8_t* out, std::size_t cap,
                            std::size_t& n) noexcept {
  if (!valid(out, cap, kHelloPayloadBytes)) return invalid();
  Writer w(out, cap);
  if (!w.u64(v.bladeId) || !w.u32(v.bladeBootId) || !w.u64(v.computerIdSeen) ||
      !w.u32(v.pcBootIdSeen) || !w.u32(v.lastBeaconSeq) || !w.u64(v.beaconRxMonoUs) ||
      !w.u32(v.capabilities) || !w.u8(v.peerReady) || !w.zeros(3))
    return invalid();
  n = w.size();
  return Status::success();
}
Status MessageCodec::decode(const std::uint8_t* in, std::size_t n, Hello& v) noexcept {
  if (!valid(in, n, kHelloPayloadBytes) || n != kHelloPayloadBytes) return invalid();
  Reader r(in, n);
  return r.u64(v.bladeId) && r.u32(v.bladeBootId) && r.u64(v.computerIdSeen) &&
                 r.u32(v.pcBootIdSeen) && r.u32(v.lastBeaconSeq) && r.u64(v.beaconRxMonoUs) &&
                 r.u32(v.capabilities) && r.u8(v.peerReady) && r.reserved(3)
             ? Status::success()
             : invalid();
}

Status MessageCodec::encode(const Bind& v, std::uint8_t* out, std::size_t cap,
                            std::size_t& n) noexcept {
  if (!valid(out, cap, kBindPayloadBytes)) return invalid();
  Writer w(out, cap);
  if (!w.u64(v.bladeId) || !w.u32(v.bladeBootId) || !w.u32(v.pcBootId) || !w.u64(v.sessionId) ||
      !w.u32(v.bindEpoch) || !w.u8(v.allowCapture) || !w.zeros(3))
    return invalid();
  n = w.size();
  return Status::success();
}
Status MessageCodec::decode(const std::uint8_t* in, std::size_t n, Bind& v) noexcept {
  if (!valid(in, n, kBindPayloadBytes) || n != kBindPayloadBytes) return invalid();
  Reader r(in, n);
  return r.u64(v.bladeId) && r.u32(v.bladeBootId) && r.u32(v.pcBootId) && r.u64(v.sessionId) &&
                 r.u32(v.bindEpoch) && r.u8(v.allowCapture) && r.reserved(3)
             ? Status::success()
             : invalid();
}

Status MessageCodec::encode(const BindAck& v, std::uint8_t* out, std::size_t cap,
                            std::size_t& n) noexcept {
  if (!valid(out, cap, kBindAckPayloadBytes)) return invalid();
  Writer w(out, cap);
  if (!w.u64(v.bladeId) || !w.u32(v.bladeBootId) || !w.u32(v.pcBootId) || !w.u64(v.sessionId) ||
      !w.u32(v.bindEpoch) || !w.u16(v.resultCode) || !w.zeros(2))
    return invalid();
  n = w.size();
  return Status::success();
}
Status MessageCodec::decode(const std::uint8_t* in, std::size_t n, BindAck& v) noexcept {
  if (!valid(in, n, kBindAckPayloadBytes) || n != kBindAckPayloadBytes) return invalid();
  Reader r(in, n);
  return r.u64(v.bladeId) && r.u32(v.bladeBootId) && r.u32(v.pcBootId) && r.u64(v.sessionId) &&
                 r.u32(v.bindEpoch) && r.u16(v.resultCode) && r.reserved(2)
             ? Status::success()
             : invalid();
}

Status MessageCodec::encode(const SourceManifest& v, std::uint8_t* out, std::size_t cap,
                            std::size_t& n) noexcept {
  if (!valid(out, cap, kSourceManifestPayloadBytes)) return invalid();
  Writer w(out, cap);
  if (!w.u64(v.bladeId) || !w.u32(v.bladeBootId) || !w.u16(v.imuModel) || !w.u8(v.accelFsG) ||
      !w.zeros(1) || !w.u16(v.gyroFsDps) || !w.u16(v.dlpfHz) || !w.u16(v.targetRateHz) ||
      !w.u8(v.mounting) || !w.zeros(1) || !w.u16(v.hardwareRevision) || !w.u8(v.firmwareMajor) ||
      !w.u8(v.firmwareMinor) || !w.u16(v.firmwarePatch) || !w.u16(v.lsbPerG) ||
      !w.u16(v.lsbPerDpsX10))
    return invalid();
  n = w.size();
  return Status::success();
}
Status MessageCodec::decode(const std::uint8_t* in, std::size_t n, SourceManifest& v) noexcept {
  if (!valid(in, n, kSourceManifestPayloadBytes) || n != kSourceManifestPayloadBytes)
    return invalid();
  Reader r(in, n);
  return r.u64(v.bladeId) && r.u32(v.bladeBootId) && r.u16(v.imuModel) && r.u8(v.accelFsG) &&
                 r.reserved(1) && r.u16(v.gyroFsDps) && r.u16(v.dlpfHz) && r.u16(v.targetRateHz) &&
                 r.u8(v.mounting) && r.reserved(1) && r.u16(v.hardwareRevision) &&
                 r.u8(v.firmwareMajor) && r.u8(v.firmwareMinor) && r.u16(v.firmwarePatch) &&
                 r.u16(v.lsbPerG) && r.u16(v.lsbPerDpsX10)
             ? Status::success()
             : invalid();
}

Status MessageCodec::encode(const DataBatch& v, std::uint8_t* out, std::size_t cap,
                            std::size_t& n) noexcept {
  if (v.sampleCount == 0 || v.sampleCount > kMaximumDataBatchSamples ||
      v.nominalPeriodUs == 0) {
    return invalid();
  }
  const auto expected = kDataBatchPrefixBytes + v.sampleCount * kDataBatchSampleBytes;
  if (!valid(out, cap, expected)) return invalid();
  Writer w(out, cap);
  if (!w.u64(v.firstSampleIndex) || !w.u32(v.batchSeq) || !w.u16(v.sampleCount) ||
      !w.u16(v.nominalPeriodUs)) {
    return invalid();
  }
  for (std::size_t index = 0; index < v.sampleCount; ++index) {
    const auto& sample = v.samples[index];
    if (!w.u16(sample.deltaTicks10us) || !w.i16(sample.ax) || !w.i16(sample.ay) ||
        !w.i16(sample.az) || !w.i16(sample.gx) || !w.i16(sample.gy) ||
        !w.i16(sample.gz) || !w.u16(sample.flags)) {
      return invalid();
    }
  }
  n = w.size();
  return Status::success();
}

Status MessageCodec::decode(const std::uint8_t* in, std::size_t n, DataBatch& v) noexcept {
  if (!valid(in, n, kDataBatchPrefixBytes)) return invalid();
  Reader r(in, n);
  if (!r.u64(v.firstSampleIndex) || !r.u32(v.batchSeq) || !r.u16(v.sampleCount) ||
      !r.u16(v.nominalPeriodUs) || v.sampleCount == 0 ||
      v.sampleCount > kMaximumDataBatchSamples || v.nominalPeriodUs == 0 ||
      n != kDataBatchPrefixBytes + v.sampleCount * kDataBatchSampleBytes) {
    return invalid();
  }
  for (std::size_t index = 0; index < v.sampleCount; ++index) {
    auto& sample = v.samples[index];
    if (!r.u16(sample.deltaTicks10us) || !r.i16(sample.ax) || !r.i16(sample.ay) ||
        !r.i16(sample.az) || !r.i16(sample.gx) || !r.i16(sample.gy) ||
        !r.i16(sample.gz) || !r.u16(sample.flags)) {
      return invalid();
    }
  }
  return Status::success();
}
}  // namespace remus::link
