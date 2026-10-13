#include "ComputerRuntime.hpp"

#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {
constexpr char kLogTag[] = "remus_computer";
constexpr TickType_t kHealthLogInterval = pdMS_TO_TICKS(5000);
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

  for (;;) {
    const remus::diagnostics::HealthSnapshot health = runtime.healthSnapshot();
    ESP_LOGI(kLogTag,
             "health uptime_ms=%llu runtime_starts=%lu queue_overflows=%lu "
             "pool_exhaustions=%lu invalid_handle_releases=%lu free_heap=%lu "
             "minimum_free_heap=%lu",
             static_cast<unsigned long long>(esp_timer_get_time() / 1000),
             static_cast<unsigned long>(health.runtimeStarts),
             static_cast<unsigned long>(health.queueOverflows),
             static_cast<unsigned long>(health.poolExhaustions),
             static_cast<unsigned long>(health.invalidHandleReleases),
             static_cast<unsigned long>(esp_get_free_heap_size()),
             static_cast<unsigned long>(esp_get_minimum_free_heap_size()));
    vTaskDelay(kHealthLogInterval);
  }
}
