#pragma once

#include <cstddef>
#include <cstdint>

#include "driver/gpio.h"
#include "driver/uart.h"
#include "remus/hal/IGps.hpp"

namespace remus::platform::esp32 {

struct UartGpsConfig {
  uart_port_t port{UART_NUM_1};
  gpio_num_t receivePin{GPIO_NUM_0};
  gpio_num_t transmitPin{GPIO_NUM_1};
  std::size_t receiveBufferBytes{2048};
};

class Esp32UartGps final : public hal::IGps {
 public:
  explicit Esp32UartGps(const UartGpsConfig& config) noexcept : config_(config) {}
  ~Esp32UartGps() override;

  Status start(std::uint32_t baudRate) noexcept override;
  std::size_t readAvailable(std::uint8_t* destination, std::size_t capacity) noexcept override;
  Status write(const std::uint8_t* source, std::size_t size,
               std::size_t& written) noexcept override;

 private:
  UartGpsConfig config_{};
  bool started_{false};
};

}  // namespace remus::platform::esp32
