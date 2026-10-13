#include <array>
#include <cstdint>
#include <iostream>

#include "FakeClock.hpp"
#include "FakeCrypto.hpp"

namespace {

int failures = 0;

void expect(bool condition, const char* expression, int line) {
  if (!condition) {
    std::cerr << "FAIL line " << line << ": " << expression << '\n';
    ++failures;
  }
}

#define EXPECT(expression) expect((expression), #expression, __LINE__)

}  // namespace

int main() {
  remus::test::FakeClock clock;
  EXPECT(clock.now() == 0);
  clock.advanceUs(5000);
  EXPECT(clock.monotonicUs() == 5000);

  remus::test::FakeCrypto crypto;
  const std::array<std::uint8_t, 3> key{1, 2, 3};
  const std::array<std::uint8_t, 4> data{4, 5, 6, 7};
  std::array<std::uint8_t, 32> first{};
  std::array<std::uint8_t, 32> second{};
  EXPECT(crypto.hmacSha256(key.data(), key.size(), data.data(), data.size(), first.data()).ok());
  EXPECT(crypto.hmacSha256(key.data(), key.size(), data.data(), data.size(), second.data()).ok());
  EXPECT(crypto.constantTimeEqual(first.data(), second.data(), first.size()));
  second[31] ^= 1;
  EXPECT(!crypto.constantTimeEqual(first.data(), second.data(), first.size()));
  EXPECT(crypto.hmacSha256(nullptr, 1, data.data(), data.size(), first.data()).error ==
         remus::Error::InvalidArgument);

  if (failures == 0) {
    std::cout << "fake tests passed\n";
  }
  return failures == 0 ? 0 : 1;
}
