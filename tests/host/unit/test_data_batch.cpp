#include <iostream>
#include <string>

#include "FakeCrypto.hpp"
#include "remus/link/FrameCodec.hpp"
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

std::string toHex(const std::uint8_t* bytes, std::size_t size) {
  constexpr char digits[] = "0123456789abcdef";
  std::string result(size * 2, '0');
  for (std::size_t index = 0; index < size; ++index) {
    result[index * 2] = digits[bytes[index] >> 4];
    result[index * 2 + 1] = digits[bytes[index] & 0x0F];
  }
  return result;
}

void testTenSampleDataBatchIsExactly244Bytes() {
  remus::link::DataBatch batch{};
  batch.firstSampleIndex = 100;
  batch.batchSeq = 7;
  batch.sampleCount = 10;
  batch.nominalPeriodUs = 5000;
  for (std::uint16_t index = 0; index < batch.sampleCount; ++index) {
    batch.samples[index] = {static_cast<std::uint16_t>(index * 500),
                            static_cast<std::int16_t>(index), 2, 3, 4, 5, 6, 0};
  }
  std::uint8_t payload[remus::link::kMaximumPayloadBytes]{};
  std::size_t payloadSize = 0;
  EXPECT(remus::link::MessageCodec::encode(batch, payload, sizeof(payload), payloadSize).ok());
  EXPECT(payloadSize == 176);
  EXPECT(toHex(payload, payloadSize) ==
         "6400000000000000070000000a00881300000000020003000400050006000000"
         "f4010100020003000400050006000000e8030200020003000400050006000000"
         "dc050300020003000400050006000000d0070400020003000400050006000000"
         "c4090500020003000400050006000000b80b0600020003000400050006000000"
         "ac0d0700020003000400050006000000a00f0800020003000400050006000000"
         "94110900020003000400050006000000");

  remus::test::FakeCrypto crypto;
  remus::link::FrameCodec codec(crypto);
  remus::link::EncodedFrame encoded{};
  std::uint8_t key[32]{};
  remus::link::FrameHeader header{remus::link::MessageType::DataBatch, 0, 1, 2, 3, 9, 7,
                                  100000};
  EXPECT(codec.encode(header, payload, payloadSize, key, sizeof(key), encoded).ok());
  EXPECT(encoded.size == 244);

  remus::link::DataBatch decoded{};
  EXPECT(remus::link::MessageCodec::decode(payload, payloadSize, decoded).ok());
  EXPECT(decoded.sampleCount == 10);
  EXPECT(decoded.samples[9].deltaTicks10us == 4500);
  EXPECT(decoded.samples[9].ax == 9);
  EXPECT(remus::link::MessageCodec::decode(payload, payloadSize - 1, decoded).error ==
         remus::Error::InvalidArgument);
}

void testSourceManifestHasAll34NormativeBytes() {
  const remus::link::SourceManifest manifest{1, 2, 0x70, 8, 500, 42, 200,
                                              0, 0, 0, 0, 0, 4096, 655};
  std::uint8_t payload[remus::link::kSourceManifestPayloadBytes]{};
  std::size_t payloadSize = 0;
  EXPECT(remus::link::MessageCodec::encode(manifest, payload, sizeof(payload), payloadSize).ok());
  EXPECT(payloadSize == 34);
  EXPECT(toHex(payload, payloadSize) ==
         "01000000000000000200000070000800f4012a00c800000000000000000000108f02");
  remus::link::SourceManifest decoded{};
  EXPECT(remus::link::MessageCodec::decode(payload, payloadSize, decoded).ok());
  EXPECT(decoded.imuModel == 0x70);
  EXPECT(decoded.accelFsG == 8);
  EXPECT(decoded.gyroFsDps == 500);
  EXPECT(decoded.lsbPerG == 4096);
  EXPECT(decoded.lsbPerDpsX10 == 655);
}
}  // namespace

int main() {
  testTenSampleDataBatchIsExactly244Bytes();
  testSourceManifestHasAll34NormativeBytes();
  return failures == 0 ? 0 : 1;
}
