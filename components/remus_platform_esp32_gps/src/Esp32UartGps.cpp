#include "remus/platform/esp32/Esp32UartGps.hpp"

#include <algorithm>

#include "esp_err.h"

namespace remus::platform::esp32 {

Esp32UartGps::~Esp32UartGps() {
  if (started_) uart_driver_delete(config_.port);
}

Status Esp32UartGps::start(std::uint32_t baudRate) noexcept {
  if (started_) return Status::success();
  if (baudRate == 0 || config_.receiveBufferBytes < 256)
    return Status::failure(Error::InvalidArgument);
  uart_config_t uartConfig{};
  uartConfig.baud_rate = static_cast<int>(baudRate);
  uartConfig.data_bits = UART_DATA_8_BITS;
  uartConfig.parity = UART_PARITY_DISABLE;
  uartConfig.stop_bits = UART_STOP_BITS_1;
  uartConfig.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
  uartConfig.rx_flow_ctrl_thresh = 0;
  uartConfig.source_clk = UART_SCLK_DEFAULT;
  esp_err_t result = uart_param_config(config_.port, &uartConfig);
  if (result == ESP_OK)
    result = uart_set_pin(config_.port, config_.transmitPin, config_.receivePin,
                          UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
  if (result == ESP_OK)
    result = uart_driver_install(config_.port, static_cast<int>(config_.receiveBufferBytes),
                                 0, 0, nullptr, 0);
  if (result != ESP_OK)
    return Status::failure(Error::NotReady, static_cast<std::uint32_t>(result));
  started_ = true;
  return Status::success();
}

std::size_t Esp32UartGps::readAvailable(std::uint8_t* destination,
                                        std::size_t capacity) noexcept {
  if (!started_ || destination == nullptr || capacity == 0) return 0;
  std::size_t buffered = 0;
  if (uart_get_buffered_data_len(config_.port, &buffered) != ESP_OK || buffered == 0) return 0;
  const std::size_t requested = std::min(buffered, capacity);
  const int bytes = uart_read_bytes(config_.port, destination,
                                    static_cast<std::uint32_t>(requested), 0);
  return bytes > 0 ? static_cast<std::size_t>(bytes) : 0;
}

Status Esp32UartGps::write(const std::uint8_t* source, std::size_t size,
                           std::size_t& written) noexcept {
  written = 0;
  if (!started_ || source == nullptr || size == 0) return Status::failure(Error::InvalidArgument);
  const int bytes = uart_write_bytes(config_.port, source, size);
  if (bytes < 0) return Status::failure(Error::IoFailure);
  written = static_cast<std::size_t>(bytes);
  return written == size ? Status::success() : Status::failure(Error::IoFailure);
}

}  // namespace remus::platform::esp32
