#include "BladeAcquisitionService.hpp"
#include "BladeRadioService.hpp"
#include "BladeRuntime.hpp"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {
constexpr char kLogTag[] = "remus_blade";
constexpr TickType_t kHealthLogInterval = pdMS_TO_TICKS(5000);
}  // namespace

extern "C" void app_main(void) {
  static remus::blade::BladeRuntime runtime;
  static remus::blade::BladeRadioService radioService;
  // Blade wiring is an independent role profile. DRDY remains disabled until
  // its physical GPIO is approved and wired for the H3 jitter gate.
  static constexpr remus::platform::esp32::Mpu6050Config bladeImu{
      GPIO_NUM_6, GPIO_NUM_5, GPIO_NUM_NC, 0x68, 400000};
  static remus::blade::BladeAcquisitionService acquisitionService(radioService, bladeImu);
  radioService.setAcquisitionService(&acquisitionService);
  const remus::Status status = runtime.start();
  if (!status.ok()) {
    ESP_LOGE(kLogTag, "runtime start failed: error=%u detail=%lu",
             static_cast<unsigned>(status.error), static_cast<unsigned long>(status.detail));
    return;
  }
  ESP_LOGI(kLogTag, "Remus Blade core runtime ready");
  const remus::Status radioStatus = radioService.start();
  if (!radioStatus.ok()) {
    ESP_LOGE(kLogTag, "radio unavailable: error=%u detail=%lu (provision NVS before M2 test)",
             static_cast<unsigned>(radioStatus.error),
             static_cast<unsigned long>(radioStatus.detail));
  }
  const remus::Status acquisitionStatus = acquisitionService.start();
  if (!acquisitionStatus.ok()) {
    ESP_LOGE(kLogTag, "IMU unavailable: error=%u detail=%lu; radio remains operational",
             static_cast<unsigned>(acquisitionStatus.error),
             static_cast<unsigned long>(acquisitionStatus.detail));
  }

  for (;;) {
    const remus::diagnostics::HealthSnapshot health = runtime.healthSnapshot();
    const remus::acquisition::SamplingMetrics imu = acquisitionService.metrics();
    ESP_LOGI(kLogTag,
             "health uptime_ms=%llu runtime_starts=%lu queue_overflows=%lu "
             "pool_exhaustions=%lu invalid_handle_releases=%lu free_heap=%lu "
             "minimum_free_heap=%lu imu_attempts=%llu imu_valid=%llu imu_errors=%llu "
             "imu_missed=%llu imu_max_jitter_us=%llu",
             static_cast<unsigned long long>(esp_timer_get_time() / 1000),
             static_cast<unsigned long>(health.runtimeStarts),
             static_cast<unsigned long>(health.queueOverflows),
             static_cast<unsigned long>(health.poolExhaustions),
             static_cast<unsigned long>(health.invalidHandleReleases),
             static_cast<unsigned long>(esp_get_free_heap_size()),
             static_cast<unsigned long>(esp_get_minimum_free_heap_size()),
             static_cast<unsigned long long>(imu.attempts),
             static_cast<unsigned long long>(imu.validSamples),
             static_cast<unsigned long long>(imu.readErrors),
             static_cast<unsigned long long>(imu.missedPeriods),
             static_cast<unsigned long long>(imu.maxAbsJitterUs));
    vTaskDelay(kHealthLogInterval);
  }
}
