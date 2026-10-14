#include <cstdint>
#include <iostream>

#include "remus/core/FixedQueue.hpp"
#include "remus/core/Ids.hpp"
#include "remus/core/ImuProfile.hpp"
#include "remus/core/PacketPool.hpp"

namespace {

int failures = 0;

void expect(bool condition, const char* expression, int line) {
  if (!condition) {
    std::cerr << "FAIL line " << line << ": " << expression << '\n';
    ++failures;
  }
}

#define EXPECT(expression) expect((expression), #expression, __LINE__)

void testIdentifiersAndSequenceWrap() {
  EXPECT(!remus::isAssigned(0));
  EXPECT(remus::isAssigned(42));
  EXPECT(!remus::isAssignedRecording(0));
  EXPECT(remus::isAssignedRecording(9));
  EXPECT(remus::sequenceAfter(1, 0));
  EXPECT(remus::sequenceBefore(0, 1));
  EXPECT(remus::sequenceAfter(1, UINT32_MAX));
  EXPECT(remus::sequenceBefore(UINT32_MAX, 1));
}

void testQueueRejectsOverflowWithoutOverwrite() {
  remus::FixedQueue<int, 2> queue;
  EXPECT(queue.push(11));
  EXPECT(queue.push(22));
  EXPECT(queue.full());
  EXPECT(!queue.push(33));

  int value = 0;
  EXPECT(queue.pop(value));
  EXPECT(value == 11);
  EXPECT(queue.pop(value));
  EXPECT(value == 22);
  EXPECT(!queue.pop(value));
}

void testPoolDetectsExhaustionAndStaleHandles() {
  remus::PacketPool<std::uint32_t, 2> pool;
  remus::PacketPool<std::uint32_t, 2>::Handle first;
  remus::PacketPool<std::uint32_t, 2>::Handle second;
  remus::PacketPool<std::uint32_t, 2>::Handle third;

  EXPECT(pool.acquire(first).ok());
  EXPECT(pool.acquire(second).ok());
  EXPECT(pool.acquire(third).error == remus::Error::PoolExhausted);
  EXPECT(pool.get(first) != nullptr);
  *pool.get(first) = 77;
  EXPECT(*pool.get(first) == 77);
  EXPECT(pool.release(first).ok());
  EXPECT(pool.get(first) == nullptr);
  EXPECT(pool.release(first).error == remus::Error::InvalidHandle);

  EXPECT(pool.acquire(third).ok());
  EXPECT(third.index == first.index);
  EXPECT(third.generation != first.generation);
  EXPECT(pool.inUse() == 2);
}

void testRoleSpecificImuProfilesHaveTypedRangesAndSensitivities() {
  const auto computer = remus::core::computerImuProfile();
  EXPECT(computer.accelRange == remus::core::AccelRangeG::G8);
  EXPECT(computer.gyroRange == remus::core::GyroRangeDps::Dps1000);
  EXPECT(remus::core::accelLsbPerG(computer.accelRange) == 4096);
  EXPECT(remus::core::gyroLsbPerDpsX10(computer.gyroRange) == 328);
  EXPECT(remus::core::accelRegisterBits(computer.accelRange) == 0x10);
  EXPECT(remus::core::gyroRegisterBits(computer.gyroRange) == 0x10);
  EXPECT(remus::core::validateImuProfile(computer).ok());

  const auto blade = remus::core::bladeImuProfile();
  EXPECT(blade.accelRange == remus::core::AccelRangeG::G16);
  EXPECT(blade.gyroRange == remus::core::GyroRangeDps::Dps2000);
  EXPECT(remus::core::accelLsbPerG(blade.accelRange) == 2048);
  EXPECT(remus::core::gyroLsbPerDpsX10(blade.gyroRange) == 164);
  EXPECT(remus::core::accelRegisterBits(blade.accelRange) == 0x18);
  EXPECT(remus::core::gyroRegisterBits(blade.gyroRange) == 0x18);
  EXPECT(remus::core::validateImuProfile(blade).ok());
}

void testInvalidImuProfileIsRejected() {
  auto profile = remus::core::computerImuProfile();
  profile.dlpfCode = 7;
  EXPECT(profile.gyroDlpfBandwidthHz() == 0);
  EXPECT(remus::core::validateImuProfile(profile).error == remus::Error::InvalidArgument);
  profile = remus::core::computerImuProfile();
  profile.dlpfCode = 0;
  EXPECT(remus::core::validateImuProfile(profile).error == remus::Error::InvalidArgument);
  profile = remus::core::computerImuProfile();
  profile.sampleRateHz = 333;
  EXPECT(remus::core::validateImuProfile(profile).error == remus::Error::InvalidArgument);
}

}  // namespace

int main() {
  testIdentifiersAndSequenceWrap();
  testQueueRejectsOverflowWithoutOverwrite();
  testPoolDetectsExhaustionAndStaleHandles();
  testRoleSpecificImuProfilesHaveTypedRangesAndSensitivities();
  testInvalidImuProfileIsRejected();
  if (failures == 0) {
    std::cout << "core tests passed\n";
  }
  return failures == 0 ? 0 : 1;
}
