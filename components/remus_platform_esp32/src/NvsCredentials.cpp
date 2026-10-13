#include "remus/platform/esp32/NvsCredentials.hpp"

#include "nvs.h"

namespace remus::platform::esp32 {
namespace {
Status mapError(esp_err_t result) noexcept {
  return Status::failure(result == ESP_ERR_NVS_NOT_FOUND ? Error::NotReady : Error::IoFailure,
                         static_cast<std::uint32_t>(result));
}
}  // namespace
Status NvsCredentials::readDeviceSerialNumber(DeviceSerialNumber& out) noexcept {
  nvs_handle_t handle{};
  esp_err_t result = nvs_open(nameSpace_, NVS_READONLY, &handle);
  if (result == ESP_OK) {
    result = nvs_get_u64(handle, "device_id", &out);
    nvs_close(handle);
  }
  if (result != ESP_OK) return mapError(result);
  return out != 0 ? Status::success() : Status::failure(Error::InvalidArgument);
}
Status NvsCredentials::readNetworkId(NetworkId& out) noexcept {
  nvs_handle_t handle{};
  esp_err_t result = nvs_open(nameSpace_, NVS_READONLY, &handle);
  if (result == ESP_OK) {
    result = nvs_get_u32(handle, "network_id", &out);
    nvs_close(handle);
  }
  if (result != ESP_OK) return mapError(result);
  return out != 0 ? Status::success() : Status::failure(Error::InvalidArgument);
}
Status NvsCredentials::readNetworkSecret(std::uint8_t* out, std::size_t capacity,
                                         std::size_t& written) noexcept {
  written = 0;
  if (out == nullptr || capacity < 32) return Status::failure(Error::BufferOverrun, 32);
  nvs_handle_t handle{};
  esp_err_t result = nvs_open(nameSpace_, NVS_READONLY, &handle);
  std::size_t size = capacity;
  if (result == ESP_OK) {
    result = nvs_get_blob(handle, "network_secret", out, &size);
    nvs_close(handle);
  }
  if (result != ESP_OK) return mapError(result);
  if (size != 32) return Status::failure(Error::InvalidArgument, static_cast<std::uint32_t>(size));
  written = size;
  return Status::success();
}
}  // namespace remus::platform::esp32
