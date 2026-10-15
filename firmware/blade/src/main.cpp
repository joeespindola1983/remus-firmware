#include "BladeAcquisitionService.hpp"
#include "BladeRadioService.hpp"
#include "BladeRuntime.hpp"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "remus/diagnostics/OperationalSerial.hpp"

namespace {
constexpr char kLogTag[] = "remus_blade";
constexpr TickType_t kHealthLogInterval = pdMS_TO_TICKS(5000);
}  // namespace

extern "C" void app_main(void) {
  static remus::blade::BladeRuntime runtime;
  static remus::blade::BladeRadioService radioService;
  // Independent Blade role profile preserving the owner-confirmed remus-sensor
  // SDA/SCL wiring. DRDY was not wired in that profile and remains disabled.
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

  remus::diagnostics::OperationalProblemCounters previousProblems{};
  for (;;) {
    const remus::diagnostics::HealthSnapshot health = runtime.healthSnapshot();
    const remus::acquisition::SamplingMetrics imu = acquisitionService.metrics();
    const remus::blade::BladeReplayStats replay = radioService.replayStats();
    const remus::diagnostics::OperationalProblemCounters currentProblems{
        health.queueOverflows,
        health.poolExhaustions,
        health.invalidHandleReleases,
        0,
        0,
        imu.readErrors,
        imu.missedPeriods,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        replay.overflows,
        replay.replayUnavailable,
        replay.gapNotices};
    static char operationalProblems[384]{};
    if (remus::diagnostics::formatNewOperationalProblems(
            previousProblems, currentProblems, operationalProblems,
            sizeof(operationalProblems)))
      ESP_LOGW(kLogTag, "%s", operationalProblems);
    previousProblems = currentProblems;
    vTaskDelay(kHealthLogInterval);
  }
}
