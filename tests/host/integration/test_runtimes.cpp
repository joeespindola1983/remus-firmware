#include <iostream>

#include "BladeRuntime.hpp"
#include "ComputerRuntime.hpp"

int main() {
  remus::blade::BladeRuntime blade;
  remus::computer::ComputerRuntime computer;

  if (!blade.start().ok() || !computer.start().ok()) {
    std::cerr << "runtime start failed\n";
    return 1;
  }
  if (blade.healthSnapshot().runtimeStarts != 1 || computer.healthSnapshot().runtimeStarts != 1) {
    std::cerr << "runtime start was not counted exactly once\n";
    return 1;
  }
  if (!blade.start().ok() || !computer.start().ok()) {
    std::cerr << "idempotent runtime start failed\n";
    return 1;
  }
  if (blade.healthSnapshot().runtimeStarts != 1 || computer.healthSnapshot().runtimeStarts != 1) {
    std::cerr << "idempotent start changed counters\n";
    return 1;
  }

  std::cout << "runtime integration tests passed\n";
  return 0;
}
