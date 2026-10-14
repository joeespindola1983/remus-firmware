#include "remus/gps/UbxNavigationDecoder.hpp"

#include <limits>

namespace remus::gps {

std::uint32_t UbxNavigationDecoder::readU32(const std::uint8_t* bytes) noexcept {
  return static_cast<std::uint32_t>(bytes[0]) |
         (static_cast<std::uint32_t>(bytes[1]) << 8U) |
         (static_cast<std::uint32_t>(bytes[2]) << 16U) |
         (static_cast<std::uint32_t>(bytes[3]) << 24U);
}

std::int32_t UbxNavigationDecoder::readI32(const std::uint8_t* bytes) noexcept {
  return static_cast<std::int32_t>(readU32(bytes));
}

void UbxNavigationDecoder::resetFrame() noexcept {
  state_ = State::Sync1;
  payloadLength_ = 0;
  payloadIndex_ = 0;
  checksumA_ = 0;
  checksumB_ = 0;
  payloadOversize_ = false;
}

void UbxNavigationDecoder::feed(std::uint8_t byte,
                                std::uint64_t receivedAtPcMonotonicUs) noexcept {
  ++stats_.bytes;
  const auto checksum = [this](std::uint8_t value) {
    checksumA_ = static_cast<std::uint8_t>(checksumA_ + value);
    checksumB_ = static_cast<std::uint8_t>(checksumB_ + checksumA_);
  };
  switch (state_) {
    case State::Sync1:
      if (byte == 0xB5) state_ = State::Sync2;
      return;
    case State::Sync2:
      if (byte == 0x62) {
        state_ = State::Class;
        checksumA_ = checksumB_ = 0;
      } else {
        state_ = byte == 0xB5 ? State::Sync2 : State::Sync1;
      }
      return;
    case State::Class:
      messageClass_ = byte;
      checksum(byte);
      state_ = State::Id;
      return;
    case State::Id:
      messageId_ = byte;
      checksum(byte);
      state_ = State::Length1;
      return;
    case State::Length1:
      payloadLength_ = byte;
      checksum(byte);
      state_ = State::Length2;
      return;
    case State::Length2:
      payloadLength_ |= static_cast<std::uint16_t>(byte) << 8U;
      checksum(byte);
      payloadIndex_ = 0;
      payloadOversize_ = payloadLength_ > payload_.size();
      state_ = payloadLength_ == 0 ? State::CkA : State::Payload;
      return;
    case State::Payload:
      if (payloadIndex_ < payload_.size()) payload_[payloadIndex_] = byte;
      ++payloadIndex_;
      checksum(byte);
      if (payloadIndex_ == payloadLength_) state_ = State::CkA;
      return;
    case State::CkA:
      receivedChecksumA_ = byte;
      state_ = State::CkB;
      return;
    case State::CkB:
      if (receivedChecksumA_ != checksumA_ || byte != checksumB_) {
        ++stats_.checksumFailures;
      } else if (payloadOversize_) {
        ++stats_.oversizeFrames;
      } else {
        ++stats_.frames;
        processFrame(receivedAtPcMonotonicUs);
      }
      resetFrame();
      return;
  }
}

std::uint16_t UbxNavigationDecoder::configurationBit(std::uint8_t messageClass,
                                                      std::uint8_t messageId) noexcept {
  if (messageClass == 0x06 && messageId == 0x01) return 1U << 0U;
  if (messageClass == 0x06 && messageId == 0x08) return 1U << 1U;
  return 0;
}

void UbxNavigationDecoder::processFrame(std::uint64_t receivedAtPcMonotonicUs) noexcept {
  if ((messageClass_ == 0x05) && payloadLength_ >= 2) {
    const auto bit = configurationBit(payload_[0], payload_[1]);
    if (messageId_ == 0x01) acknowledgedMask_ |= bit;
    if (messageId_ == 0x00) rejectedMask_ |= bit;
    return;
  }
  if (messageClass_ == 0x06 && messageId_ == 0x08 && payloadLength_ == 6) {
    measurementRateMs_ = static_cast<std::uint16_t>(payload_[0]) |
                         (static_cast<std::uint16_t>(payload_[1]) << 8U);
    navigationRate_ = static_cast<std::uint16_t>(payload_[2]) |
                      (static_cast<std::uint16_t>(payload_[3]) << 8U);
    return;
  }
  if (messageClass_ != 0x01) return;
  if (messageId_ == 0x02 && payloadLength_ >= 28) {
    positionTowMs_ = readU32(payload_.data());
    building_.longitudeE7 = readI32(payload_.data() + 4);
    building_.latitudeE7 = readI32(payload_.data() + 8);
    building_.horizontalAccuracyMillimeters = readU32(payload_.data() + 20);
  } else if (messageId_ == 0x12 && payloadLength_ >= 36) {
    velocityTowMs_ = readU32(payload_.data());
    const std::uint32_t centimetersPerSecond = readU32(payload_.data() + 20);
    building_.groundSpeedMillimetersPerSecond =
        centimetersPerSecond > std::numeric_limits<std::uint32_t>::max() / 10U
            ? kUnavailableUnsigned32
            : centimetersPerSecond * 10U;
    const std::int32_t courseDegreesE5 = readI32(payload_.data() + 24);
    building_.courseCentiDegrees =
        courseDegreesE5 >= 0 && courseDegreesE5 < 36000000
            ? static_cast<std::uint16_t>((courseDegreesE5 + 500) / 1000)
            : kUnavailableUnsigned16;
    const std::uint32_t speedAccuracyCentimetersPerSecond = readU32(payload_.data() + 28);
    building_.speedAccuracyMillimetersPerSecond =
        speedAccuracyCentimetersPerSecond > std::numeric_limits<std::uint32_t>::max() / 10U
            ? kUnavailableUnsigned32
            : speedAccuracyCentimetersPerSecond * 10U;
    building_.courseAccuracyCentiDegrees = readU32(payload_.data() + 32) / 1000U;
  } else if (messageId_ == 0x03 && payloadLength_ >= 16) {
    statusTowMs_ = readU32(payload_.data());
    building_.fixType = payload_[4];
    building_.receiverFixValid = (payload_[5] & 0x01U) != 0 && payload_[4] >= 2;
  }
  publishIfCoherent(receivedAtPcMonotonicUs);
}

void UbxNavigationDecoder::publishIfCoherent(
    std::uint64_t receivedAtPcMonotonicUs) noexcept {
  if (positionTowMs_ == UINT32_MAX || positionTowMs_ != velocityTowMs_ ||
      positionTowMs_ != statusTowMs_ || positionTowMs_ == publishedTowMs_)
    return;
  building_.gpsTimeOfWeekMs = positionTowMs_;
  building_.receivedAtPcMonotonicUs = receivedAtPcMonotonicUs;
  if (readyPending_) ++stats_.overwrittenEpochs;
  ready_ = building_;
  readyPending_ = true;
  publishedTowMs_ = positionTowMs_;
  ++stats_.coherentEpochs;
}

bool UbxNavigationDecoder::take(NavigationEpoch& out) noexcept {
  if (!readyPending_) return false;
  out = ready_;
  readyPending_ = false;
  return true;
}

}  // namespace remus::gps
