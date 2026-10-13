#include "ComputerRuntime.hpp"

#include "esp_log.h"

namespace {
constexpr char kLogTag[] = "remus_computer";
}

extern "C" void app_main(void) {
  static remus::computer::ComputerRuntime runtime;
  const remus::Status status = runtime.start();
  if (!status.ok()) {
    ESP_LOGE(kLogTag, "runtime start failed: error=%u detail=%lu",
             static_cast<unsigned>(status.error), static_cast<unsigned long>(status.detail));
    return;
  }
  ESP_LOGI(kLogTag, "Remus Computer M0 ready (zero-Blade capable foundation)");
}
