#include "remus/acquisition/SampleClock.hpp"

namespace remus::acquisition {

Status SampleClock::reset(std::uint16_t rateHz, NativeTimestampUs originUs) noexcept {
  if (rateHz == 0 || (1000000U % rateHz) != 0) {
    return Status::failure(Error::InvalidArgument);
  }
  periodUs_ = 1000000U / rateHz;
  nextDueUs_ = originUs + periodUs_;
  nextIndex_ = 0;
  return Status::success();
}

DueSample SampleClock::consumeDue(NativeTimestampUs nowUs) noexcept {
  if (periodUs_ == 0 || nowUs < nextDueUs_) return {};
  const auto missed = (nowUs - nextDueUs_) / periodUs_;
  const DueSample result{true, nextIndex_ + missed, missed, nextDueUs_ + missed * periodUs_};
  nextIndex_ += missed + 1;
  nextDueUs_ += (missed + 1) * periodUs_;
  return result;
}

}  // namespace remus::acquisition
