#include <array>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>

#include "remus/gps/NmeaDecoder.hpp"
#include "remus/gps/Neo6mProfile.hpp"
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

void feedEpoch(remus::gps::UbxNavigationDecoder& decoder, std::uint32_t towMs,
               std::uint64_t receivedAtUs) {
  std::uint8_t position[28]{};
  std::uint8_t velocity[36]{};
  std::uint8_t status[16]{};
  put32(position, towMs);
  put32(position + 4, static_cast<std::uint32_t>(-466333000));
  put32(position + 8, static_cast<std::uint32_t>(-235501000));
  put32(velocity, towMs);
  put32(status, towMs);
  status[4] = 3;
  status[5] = 1;
  feed(decoder, 0x01, 0x02, position, sizeof(position), receivedAtUs);
  feed(decoder, 0x01, 0x12, velocity, sizeof(velocity), receivedAtUs + 1);
  feed(decoder, 0x01, 0x03, status, sizeof(status), receivedAtUs + 2);
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

void testAdaptiveProfilesDefaultToContinuousOneHertz() {
  using remus::gps::GpsOperatingMode;
  EXPECT(remus::gps::kDefaultGpsOperatingMode == GpsOperatingMode::Normal);
  const auto saving = remus::gps::profileFor(GpsOperatingMode::NormalPowerSaving);
  const auto normal = remus::gps::profileFor(GpsOperatingMode::Normal);
  const auto armed = remus::gps::profileFor(GpsOperatingMode::DynamicArmed);
  EXPECT(saving.measurementRateMs == 1000 && saving.powerSave);
  EXPECT(normal.measurementRateMs == 1000 && !normal.powerSave);
  EXPECT(armed.measurementRateMs == 200 && !armed.powerSave);
  EXPECT(armed.nmeaMessageRate == 5);
  EXPECT(armed.ubxNavigationMessageRate == 1);
  EXPECT(!remus::gps::hasQualifiedFixReport(true, true, 0,
                                            remus::gps::kUnavailableUnsigned8));
  EXPECT(remus::gps::hasQualifiedFixReport(true, true, 1, 6));
  EXPECT(!remus::gps::hasQualifiedFixReport(false, true, 1, 6));
}


void testAdaptiveModeSafeguards() {
  using remus::gps::GpsModeController;
  using remus::gps::GpsOperatingMode;
  GpsModeController controller;
  EXPECT(controller.requestedMode() == GpsOperatingMode::Normal);
  EXPECT(controller.activeMode() == GpsOperatingMode::Normal);

  controller.request(GpsOperatingMode::DynamicArmed, 1000);
  EXPECT(controller.configurationRequired());
  EXPECT(controller.configurationApplied(GpsOperatingMode::DynamicArmed, 1000));
  EXPECT(controller.activeMode() == GpsOperatingMode::DynamicArmed);
  controller.tick(1000 + GpsModeController::kDynamicMaximumDurationUs + 1);
  EXPECT(controller.requestedMode() == GpsOperatingMode::Normal);

  EXPECT(controller.configurationApplied(GpsOperatingMode::Normal, 2000));
  controller.request(GpsOperatingMode::DynamicArmed, 3000);
  EXPECT(controller.configurationApplied(GpsOperatingMode::DynamicArmed, 3000));
  controller.observeStorage(0, GpsModeController::kStoragePressureDepth, 0, 4000);
  EXPECT(controller.requestedMode() == GpsOperatingMode::Normal);
  EXPECT(controller.storagePressureDetected());

  GpsModeController stale;
  stale.request(GpsOperatingMode::NormalPowerSaving, 0);
  EXPECT(stale.configurationApplied(GpsOperatingMode::NormalPowerSaving, 0));
  stale.observeNavigationStale(5000);
  EXPECT(stale.requestedMode() == GpsOperatingMode::Normal);
  EXPECT(stale.navigationFallbackDetected());

  GpsModeController writeFailure;
  writeFailure.request(GpsOperatingMode::DynamicArmed, 0);
  EXPECT(writeFailure.configurationApplied(GpsOperatingMode::DynamicArmed, 0));
  writeFailure.observeStorage(0, 0, 0, 1);
  writeFailure.observeStorage(0, 0, 1, 2);
  EXPECT(writeFailure.requestedMode() == GpsOperatingMode::Normal);

  GpsModeController superseded;
  superseded.request(GpsOperatingMode::DynamicArmed, 0);
  superseded.request(GpsOperatingMode::NormalPowerSaving, 1);
  EXPECT(!superseded.configurationApplied(GpsOperatingMode::DynamicArmed, 2));
  EXPECT(superseded.activeMode() == GpsOperatingMode::Normal);

  GpsModeController failed;
  failed.request(GpsOperatingMode::DynamicArmed, 0);
  failed.configurationFailed();
  failed.configurationFailed();
  failed.configurationFailed();
  EXPECT(failed.requestedMode() == GpsOperatingMode::Normal);
  EXPECT(failed.configurationFault());
}

void testDecoderQueuesBurstWithoutSilentOverwrite() {
  remus::gps::UbxNavigationDecoder decoder;
  feedEpoch(decoder, 1000, 1000000);
  feedEpoch(decoder, 1200, 1200000);
  feedEpoch(decoder, 1400, 1400000);
  remus::gps::NavigationEpoch epoch{};
  EXPECT(decoder.take(epoch) && epoch.gpsTimeOfWeekMs == 1000);
  EXPECT(decoder.take(epoch) && epoch.gpsTimeOfWeekMs == 1200);
  EXPECT(decoder.take(epoch) && epoch.gpsTimeOfWeekMs == 1400);
  EXPECT(!decoder.take(epoch));
  EXPECT(decoder.stats().overwrittenEpochs == 0);
}

void testDecoderOverflowIsBoundedAndMeasured() {
  remus::gps::UbxNavigationDecoder decoder;
  for (std::uint32_t index = 0; index < 9; ++index)
    feedEpoch(decoder, 1000 + index * 200, 1000000 + index * 200000);
  EXPECT(decoder.stats().coherentEpochs == 9);
  EXPECT(decoder.stats().overwrittenEpochs == 1);
  remus::gps::NavigationEpoch epoch{};
  for (std::uint32_t index = 0; index < 8; ++index) {
    EXPECT(decoder.take(epoch));
    EXPECT(epoch.gpsTimeOfWeekMs == 1000 + index * 200);
  }
  EXPECT(!decoder.take(epoch));
}
}  // namespace

int main() {
  testCoherentUbxEpoch();
  testUbxRejectsBadChecksumAndMismatchedEpochs();
  testNmeaQualityAndUtc();
  testGpsFixPayloadRoundTrip();
  testAdaptiveProfilesDefaultToContinuousOneHertz();
  testAdaptiveModeSafeguards();
  testDecoderQueuesBurstWithoutSilentOverwrite();
  testDecoderOverflowIsBoundedAndMeasured();
  return failures == 0 ? 0 : 1;
}
