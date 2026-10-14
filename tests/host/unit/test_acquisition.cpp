#include <iostream>

#include "FakeClock.hpp"
#include "FakeImu.hpp"
#include "remus/acquisition/AcquisitionPipeline.hpp"
#include "remus/acquisition/BatchBuilder.hpp"
#include "remus/acquisition/Sampler.hpp"
#include "remus/platform/esp32/Mpu6050Compatibility.hpp"

namespace {
int failures = 0;
void expect(bool value, const char* expression, int line) {
  if (!value) {
    std::cerr << "FAIL line " << line << ": " << expression << '\n';
    ++failures;
  }
}
#define EXPECT(value) expect((value), #value, __LINE__)

class CollectingSink final : public remus::acquisition::IAcquisitionSink {
 public:
  remus::Status onBatchReady(const remus::acquisition::SampleBatch& value) noexcept override {
    batch = value;
    ++calls;
    return status;
  }
  remus::acquisition::SampleBatch batch{};
  remus::Status status{};
  unsigned calls{0};
};

void testSamplerUsesRealReadTimeAndReportsMissedPeriods() {
  remus::test::FakeClock clock;
  remus::test::FakeImu imu;
  remus::acquisition::Sampler sampler;
  EXPECT(sampler.initialize({200}, imu, clock).ok());
  remus::acquisition::ImuSample sample{};
  EXPECT(!sampler.pollDue(4999, sample).sampleReady);
  clock.setUs(5100);
  const auto first = sampler.pollDue(5100, sample);
  EXPECT(first.sampleReady);
  EXPECT(sample.sampledAtUs == 5100);
  EXPECT(sample.index == 0);
  clock.setUs(16100);
  EXPECT(sampler.pollDue(16100, sample).sampleReady);
  EXPECT(sample.index == 2);
  EXPECT(sampler.metrics().missedPeriods == 1);
}

void testReadFailureConsumesIndexWithoutFabricatingSample() {
  remus::test::FakeClock clock;
  remus::test::FakeImu imu;
  remus::acquisition::Sampler sampler;
  EXPECT(sampler.initialize({200}, imu, clock).ok());
  remus::acquisition::ImuSample sample{};
  imu.readStatus = remus::Status::failure(remus::Error::IoFailure);
  clock.setUs(5000);
  EXPECT(!sampler.pollDue(5000, sample).sampleReady);
  imu.readStatus = remus::Status::success();
  clock.setUs(10000);
  EXPECT(sampler.pollDue(10000, sample).sampleReady);
  EXPECT(sample.index == 1);
  EXPECT(sampler.metrics().readErrors == 1);
}

void testBatchBuilderClosesTenSamplesAndNeverHidesGap() {
  remus::acquisition::BatchBuilder builder;
  builder.resetForSession(9, 5000);
  for (std::uint64_t index = 0; index < 10; ++index) {
    remus::acquisition::ImuSample sample{index, 100000 + index * 5000, {}, 0};
    EXPECT(builder.pushSample(sample) == remus::acquisition::PushResult::Accepted);
  }
  EXPECT(builder.isReady());
  remus::acquisition::SampleBatch batch{};
  EXPECT(builder.take(batch));
  EXPECT(batch.count == 10);
  EXPECT(batch.firstSampleIndex == 0);
  EXPECT(batch.baseTimeUs == 100000);
  EXPECT(batch.batchSeq == 0);

  EXPECT(builder.pushSample({20, 200000, {}, 0}) == remus::acquisition::PushResult::Accepted);
  EXPECT(builder.pushSample({22, 210000, {}, 0}) == remus::acquisition::PushResult::GapClosedBatch);
  EXPECT(builder.take(batch));
  EXPECT(batch.count == 1);
  EXPECT(batch.firstSampleIndex == 20);
  EXPECT(builder.pushSample({22, 210000, {}, 0}) == remus::acquisition::PushResult::Accepted);
}

void testSharedPipelinePublishesTenSamplesToRoleSpecificSink() {
  remus::test::FakeClock clock;
  remus::test::FakeImu imu;
  CollectingSink sink;
  remus::acquisition::AcquisitionPipeline pipeline;
  EXPECT(pipeline.initialize({200}, imu, clock, sink).ok());
  clock.setUs(1000000);
  EXPECT(pipeline.startSession(77).ok());
  EXPECT(!pipeline.poll(1004999).sampleReady);
  for (std::uint64_t index = 1; index <= 10; ++index) {
    clock.setUs(1000000 + index * 5000);
    EXPECT(pipeline.poll(clock.now()).status.ok());
  }
  EXPECT(sink.calls == 1);
  EXPECT(sink.batch.recordingId == 77);
  EXPECT(sink.batch.count == 10);
  EXPECT(sink.batch.firstSampleIndex == 0);
  EXPECT(pipeline.metrics().missedPeriods == 0);
}

void testMpuCompatibilityUsesFunctionalConfigurationNotIdentityAlone() {
  using remus::platform::esp32::hasMpu6050RegisterProfile;
  using remus::platform::esp32::isCanonicalMpu6050Identity;
  EXPECT(isCanonicalMpu6050Identity(0x68));
  EXPECT(isCanonicalMpu6050Identity(0x69));
  EXPECT(!isCanonicalMpu6050Identity(0x74));
  EXPECT(hasMpu6050RegisterProfile(0x00, 0x03, 0x04, 0x10, 0x08, 0x04));
  EXPECT(!hasMpu6050RegisterProfile(0x40, 0x03, 0x04, 0x10, 0x08, 0x04));
}
}  // namespace

int main() {
  testSamplerUsesRealReadTimeAndReportsMissedPeriods();
  testReadFailureConsumesIndexWithoutFabricatingSample();
  testBatchBuilderClosesTenSamplesAndNeverHidesGap();
  testSharedPipelinePublishesTenSamplesToRoleSpecificSink();
  testMpuCompatibilityUsesFunctionalConfigurationNotIdentityAlone();
  return failures == 0 ? 0 : 1;
}
