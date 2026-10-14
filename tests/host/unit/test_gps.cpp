#include <array>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>

#include "remus/gps/NmeaDecoder.hpp"
#include "remus/gps/UbxNavigationDecoder.hpp"
#include "remus/storage/Rms1Codec.hpp"

namespace {
int failures = 0;
#define EXPECT(x) do { if (!(x)) { ++failures; std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); } } while (0)

std::size_t makeUbx(std::uint8_t messageClass, std::uint8_t messageId,
                    const std::uint8_t* payload, std::uint16_t payloadSize,
                    std::uint8_t* out) {
  out[0] = 0xB5;
  out[1] = 0x62;
  out[2] = messageClass;
  out[3] = messageId;
  out[4] = static_cast<std::uint8_t>(payloadSize);
  out[5] = static_cast<std::uint8_t>(payloadSize >> 8U);
  if (payloadSize != 0) std::memcpy(out + 6, payload, payloadSize);
  std::uint8_t a = 0;
  std::uint8_t b = 0;
  for (std::size_t index = 2; index < 6U + payloadSize; ++index) {
    a = static_cast<std::uint8_t>(a + out[index]);
    b = static_cast<std::uint8_t>(b + a);
  }
  out[6U + payloadSize] = a;
  out[7U + payloadSize] = b;
  return 8U + payloadSize;
}

void put32(std::uint8_t* out, std::uint32_t value) {
  for (unsigned index = 0; index < 4; ++index)
    out[index] = static_cast<std::uint8_t>(value >> (8U * index));
}

void feed(remus::gps::UbxNavigationDecoder& decoder, std::uint8_t cls,
          std::uint8_t id, const std::uint8_t* payload, std::uint16_t size,
          std::uint64_t receivedAtUs) {
  std::array<std::uint8_t, 80> frame{};
  const auto bytes = makeUbx(cls, id, payload, size, frame.data());
  for (std::size_t index = 0; index < bytes; ++index)
    decoder.feed(frame[index], receivedAtUs);
}

void testCoherentUbxEpoch() {
  remus::gps::UbxNavigationDecoder decoder;
  std::uint8_t position[28]{};
  std::uint8_t velocity[36]{};
  std::uint8_t status[16]{};
  put32(position, 424200);
  put32(position + 4, static_cast<std::uint32_t>(-466333000));
  put32(position + 8, static_cast<std::uint32_t>(-235501000));
  put32(position + 20, 2300);
  put32(velocity, 424200);
  put32(velocity + 20, 345);
  put32(velocity + 24, 1234500);
  put32(velocity + 28, 40);
  put32(velocity + 32, 2500);
  put32(status, 424200);
  status[4] = 3;
  status[5] = 1;
  feed(decoder, 0x01, 0x02, position, sizeof(position), 9000000);
  feed(decoder, 0x01, 0x12, velocity, sizeof(velocity), 9000100);
  remus::gps::NavigationEpoch epoch{};
  EXPECT(!decoder.take(epoch));
  feed(decoder, 0x01, 0x03, status, sizeof(status), 9000200);
  EXPECT(decoder.take(epoch));
  EXPECT(epoch.gpsTimeOfWeekMs == 424200);
  EXPECT(epoch.latitudeE7 == -235501000);
  EXPECT(epoch.longitudeE7 == -466333000);
  EXPECT(epoch.groundSpeedMillimetersPerSecond == 3450);
  EXPECT(epoch.courseCentiDegrees == 1235);
  EXPECT(epoch.horizontalAccuracyMillimeters == 2300);
  EXPECT(epoch.fixType == 3 && epoch.receiverFixValid);
  EXPECT(epoch.receivedAtPcMonotonicUs == 9000200);
}

void testUbxRejectsBadChecksumAndMismatchedEpochs() {
  remus::gps::UbxNavigationDecoder decoder;
  std::uint8_t payload[16]{};
  put32(payload, 1000);
  payload[4] = 3;
  payload[5] = 1;
  std::array<std::uint8_t, 32> frame{};
  const auto bytes = makeUbx(0x01, 0x03, payload, sizeof(payload), frame.data());
  frame[bytes - 1] ^= 0xFF;
  for (std::size_t index = 0; index < bytes; ++index) decoder.feed(frame[index], 1);
  EXPECT(decoder.stats().checksumFailures == 1);
  remus::gps::NavigationEpoch epoch{};
  EXPECT(!decoder.take(epoch));

  std::uint8_t position[28]{};
  std::uint8_t velocity[36]{};
  std::uint8_t status[16]{};
  put32(position, 1000);
  put32(velocity, 1001);
  put32(status, 1000);
  feed(decoder, 0x01, 0x02, position, sizeof(position), 2);
  feed(decoder, 0x01, 0x12, velocity, sizeof(velocity), 3);
  feed(decoder, 0x01, 0x03, status, sizeof(status), 4);
  EXPECT(!decoder.take(epoch));
}

void testNmeaQualityAndUtc() {
  remus::gps::NmeaDecoder decoder;
  const char* gga = "$GPGGA,123519.00,4807.038,N,01131.000,E,1,08,0.90,545.4,M,46.9,M,,*59\r\n";
  const char* rmc = "$GPRMC,123520.00,A,4807.038,N,01131.000,E,6.50,84.40,230394,,,A*6F\r\n";
  for (const char* p = gga; *p; ++p) decoder.feed(static_cast<std::uint8_t>(*p), 1000000);
  for (const char* p = rmc; *p; ++p) decoder.feed(static_cast<std::uint8_t>(*p), 2000000);
  const auto snapshot = decoder.snapshot();
  EXPECT(snapshot.fixQuality == 1);
  EXPECT(snapshot.satellites == 8);
  EXPECT(snapshot.hdopX100 == 90);
  EXPECT(snapshot.utcValid);
  EXPECT(snapshot.positionValid);
  EXPECT(snapshot.speedValid);
  EXPECT(snapshot.courseValid);
  EXPECT(snapshot.latitudeE7 == 481173000);
  EXPECT(snapshot.longitudeE7 == 115166667);
}

void testGpsFixPayloadRoundTrip() {
  const remus::storage::GpsFixPayload expected{
      1234567, -235501000, -466333000, 3450, 1235, 8, 3, 90, 0x003F,
      764426120000LL, 25};
  std::uint8_t encoded[remus::storage::kGpsFixPayloadBytes]{};
  EXPECT(remus::storage::encodeGpsFixPayload(expected, encoded, sizeof(encoded)).ok());
  remus::storage::GpsFixPayload decoded{};
  EXPECT(remus::storage::decodeGpsFixPayload(encoded, sizeof(encoded), decoded).ok());
  EXPECT(decoded.pcMonotonicUs == expected.pcMonotonicUs);
  EXPECT(decoded.latitudeE7 == expected.latitudeE7);
  EXPECT(decoded.longitudeE7 == expected.longitudeE7);
  EXPECT(decoded.speedMillimetersPerSecond == expected.speedMillimetersPerSecond);
  EXPECT(decoded.courseCentiDegrees == expected.courseCentiDegrees);
  EXPECT(decoded.utcMilliseconds == expected.utcMilliseconds);
}
}  // namespace

int main() {
  testCoherentUbxEpoch();
  testUbxRejectsBadChecksumAndMismatchedEpochs();
  testNmeaQualityAndUtc();
  testGpsFixPayloadRoundTrip();
  return failures == 0 ? 0 : 1;
}
