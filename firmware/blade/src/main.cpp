#include "BladeRuntime.hpp"

#include "esp_log.h"

namespace {
constexpr char kLogTag[] = "remus_blade";
}

extern "C" void app_main(void) {
  static remus::blade::BladeRuntime runtime;
  const remus::Status status = runtime.start();
  if (!status.ok()) {
    ESP_LOGE(kLogTag, "runtime start failed: error=%u detail=%lu",
             static_cast<unsigned>(status.error), static_cast<unsigned long>(status.detail));
    return;
  }
  ESP_LOGI(kLogTag, "Remus Blade M0 ready");
}
