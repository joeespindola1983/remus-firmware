#include <array>
#include <cstdint>
#include <iostream>
#include <string>

#include "FakeCrypto.hpp"
#include "remus/core/Config.hpp"
#include "remus/link/FrameCodec.hpp"
#include "remus/link/KeyDerivation.hpp"
#include "remus/link/MessageCodec.hpp"

namespace {
int failures = 0;
void expect(bool value, const char* expression, int line) {
  if (!value) {
    std::cerr << "FAIL line " << line << ": " << expression << '\n';
    ++failures;
  }
}
#define EXPECT(value) expect((value), #value, __LINE__)

constexpr std::array<std::uint8_t, 32> kSecret{0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10,
                                               11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21,
                                               22, 23, 24, 25, 26, 27, 28, 29, 30, 31};

std::string toHex(const std::uint8_t* bytes, std::size_t size) {
  constexpr char digits[] = "0123456789abcdef";
  std::string result(size * 2, '0');
  for (std::size_t index = 0; index < size; ++index) {
    result[index * 2] = digits[bytes[index] >> 4];
    result[index * 2 + 1] = digits[bytes[index] & 0x0F];
  }
  return result;
}

void testBeaconRoundTripAndTamperRejection() {
  remus::test::FakeCrypto crypto;
  remus::link::FrameCodec codec(crypto);
  remus::link::Beacon beacon{0x0102030405060708ULL, 0x11223344U, 3U, 0x55667788U, 9U, 1U};
  std::array<std::uint8_t, remus::link::kMaximumFrameBytes> payload{};
  std::size_t payloadSize = 0;
  EXPECT(
      remus::link::MessageCodec::encode(beacon, payload.data(), payload.size(), payloadSize).ok());
  EXPECT(payloadSize == remus::link::kBeaconPayloadBytes);

  remus::link::FrameHeader header{};
  header.type = remus::link::MessageType::Beacon;
  header.networkId = 0xA1B2C3D4U;
  header.senderId = beacon.computerId;
  header.senderBootId = beacon.pcBootId;
  header.messageSeq = 7;
  header.originMonoUs = 123456;
  remus::link::EncodedFrame encoded{};
  EXPECT(codec.encode(header, payload.data(), payloadSize, kSecret.data(), kSecret.size(), encoded)
             .ok());
  EXPECT(encoded.size == 96);
  EXPECT(toHex(encoded.bytes.data(), encoded.size) ==
         "524d4631010100306000d4c3b2a1080706050403020144332211"
         "00000000000000000700000040e20100000000001c0008070605"
         "040302014433221103000000887766550900000001000000246443"
         "ce94a6a4ab9dcfad7f661504b3bab9b8bf");

  remus::link::DecodedFrame decoded{};
  EXPECT(codec.decode(encoded.bytes.data(), encoded.size, kSecret.data(), kSecret.size(), decoded)
             .ok());
  remus::link::Beacon recovered{};
  EXPECT(remus::link::MessageCodec::decode(decoded.payload.data(), decoded.payloadSize, recovered)
             .ok());
  EXPECT(recovered.computerId == beacon.computerId);
  EXPECT(recovered.configFingerprint == beacon.configFingerprint);

  encoded.bytes[50] ^= 0x01U;
  EXPECT(codec.decode(encoded.bytes.data(), encoded.size, kSecret.data(), kSecret.size(), decoded)
             .error == remus::Error::BadCrc);
}

void testTruncationsAndWrongKeyAreRejected() {
  remus::test::FakeCrypto crypto;
  remus::link::FrameCodec codec(crypto);
  const remus::link::Bind bind{11, 22, 33, 44, 55, 1};
  std::array<std::uint8_t, remus::link::kMaximumPayloadBytes> payload{};
  std::size_t payloadSize = 0;
  EXPECT(remus::link::MessageCodec::encode(bind, payload.data(), payload.size(), payloadSize).ok());
  remus::link::FrameHeader header{};
  header.type = remus::link::MessageType::Bind;
  header.networkId = 77;
  header.senderId = 88;
  remus::link::EncodedFrame frame{};
  EXPECT(codec.encode(header, payload.data(), payloadSize, kSecret.data(), kSecret.size(), frame)
             .ok());
  remus::link::DecodedFrame decoded{};
  for (std::size_t size = 0; size < frame.size; ++size) {
    EXPECT(!codec.decode(frame.bytes.data(), size, kSecret.data(), kSecret.size(), decoded).ok());
  }
  auto wrongKey = kSecret;
  wrongKey[0] ^= 0x80U;
  EXPECT(codec.decode(frame.bytes.data(), frame.size, wrongKey.data(), wrongKey.size(), decoded)
             .error == remus::Error::AuthFailed);
}

void testCanonicalPairDerivation() {
  remus::test::FakeCrypto crypto;
  remus::link::DerivedKeys forward{};
  remus::link::DerivedKeys reverse{};
  EXPECT(remus::link::derivePairKeys(crypto, kSecret.data(), kSecret.size(), 10, 20, forward).ok());
  EXPECT(remus::link::derivePairKeys(crypto, kSecret.data(), kSecret.size(), 20, 10, reverse).ok());
  EXPECT(forward.pairAuthKey == reverse.pairAuthKey);
  EXPECT(forward.pairLmk == reverse.pairLmk);
}

void testConnectionConfigurationValidation() {
  remus::RadioConfig radio{6, 8};
  remus::CapacityConfig capacity{8, 32, 32};
  EXPECT(remus::validateConnectionConfig(radio, capacity).ok());
  radio.encryptedPeerCapacity = 7;
  EXPECT(remus::validateConnectionConfig(radio, capacity).error == remus::Error::PeerFull);
  radio.encryptedPeerCapacity = 8;
  radio.channel = 0;
  EXPECT(remus::validateConnectionConfig(radio, capacity).error == remus::Error::InvalidArgument);
}
}  // namespace

int main() {
  testBeaconRoundTripAndTamperRejection();
  testTruncationsAndWrongKeyAreRejected();
  testCanonicalPairDerivation();
  testConnectionConfigurationValidation();
  return failures == 0 ? 0 : 1;
}
