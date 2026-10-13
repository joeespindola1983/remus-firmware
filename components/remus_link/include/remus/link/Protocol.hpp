#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "remus/core/Types.hpp"

namespace remus::link {
constexpr std::size_t kHeaderBytes = 48;
constexpr std::size_t kCrcBytes = 4;
constexpr std::size_t kAuthTagBytes = 16;
constexpr std::size_t kMinimumFrameBytes = 68;
constexpr std::size_t kMaximumFrameBytes = 250;
constexpr std::size_t kMaximumPayloadBytes = 182;
constexpr std::uint8_t kWireVersion = 1;

enum class MessageType : std::uint8_t {
  Beacon = 0x01,
  Hello = 0x02,
  Bind = 0x03,
  BindAck = 0x04,
  Start = 0x05,
  StartAck = 0x06,
  Stop = 0x07,
  StopAck = 0x08,
  DataBatch = 0x09,
  AckWindow = 0x0A,
  ReplayRequest = 0x0B,
  GapNotice = 0x0C,
  SyncRequest = 0x0D,
  SyncResponse = 0x0E,
  Health = 0x0F,
  Error = 0x10,
  SourceManifest = 0x11,
};

struct FrameHeader {
  MessageType type{MessageType::Beacon};
  std::uint8_t flags{0};
  NetworkId networkId{0};
  DeviceSerialNumber senderId{0};
  DeviceBootId senderBootId{0};
  RecordingId recordingId{0};
  StreamSeq messageSeq{0};
  NativeTimestampUs originMonoUs{0};
};

struct EncodedFrame {
  std::array<std::uint8_t, kMaximumFrameBytes> bytes{};
  std::size_t size{0};
};

struct DecodedFrame {
  FrameHeader header{};
  std::array<std::uint8_t, kMaximumPayloadBytes> payload{};
  std::size_t payloadSize{0};
};
}  // namespace remus::link
