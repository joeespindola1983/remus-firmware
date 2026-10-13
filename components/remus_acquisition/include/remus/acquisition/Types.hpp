#pragma once

#include <array>
#include <cstdint>

#include "remus/core/Types.hpp"
#include "remus/hal/IImu.hpp"

namespace remus::acquisition {

constexpr std::size_t kSamplesPerBatch = 10;

struct ImuSample {
  SampleIndex index{0};
  NativeTimestampUs sampledAtUs{0};
  hal::ImuRaw raw{};
  std::uint16_t flags{0};
};

struct SampleBatch {
  RecordingId recordingId{0};
  SampleIndex firstSampleIndex{0};
  std::uint32_t batchSeq{0};
  NativeTimestampUs baseTimeUs{0};
  std::uint16_t count{0};
  std::uint16_t nominalPeriodUs{0};
  std::array<ImuSample, kSamplesPerBatch> samples{};
};

}  // namespace remus::acquisition
