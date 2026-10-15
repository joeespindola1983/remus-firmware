#include <cstdio>

#include "ComputerAcquisitionService.hpp"
#include "ComputerRadioService.hpp"
#include "ComputerRuntime.hpp"
#include "GpsService.hpp"
#include "SdDiagnosticLogger.hpp"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "remus/diagnostics/OperationalSerial.hpp"

namespace {
constexpr char kLogTag[] = "remus_computer";
constexpr TickType_t kHealthLogInterval = pdMS_TO_TICKS(5000);

std::uint32_t fnv1a(const char* value) noexcept {
  std::uint32_t hash = 2166136261U;
  if (value == nullptr) return hash;
  while (*value != '\0') {
    hash ^= static_cast<std::uint8_t>(*value++);
    hash *= 16777619U;
  }
  return hash;
}
}  // namespace

extern "C" void app_main(void) {
  remus::RecordingId recordingId =
      (static_cast<remus::RecordingId>(esp_random()) << 32) | esp_random();
  if (recordingId == 0) recordingId = 1;
  static remus::computer::ComputerRuntime runtime;
  static remus::computer::SdDiagnosticLogger sdLogger;
  static remus::computer::ComputerRadioService radioService(&sdLogger, recordingId);
  static remus::computer::GpsService gpsService(sdLogger);
  // Independent Computer role profile. The owner-confirmed remus-sensor wiring
  // uses the same GPIO numbers as the Blade, on a physically separate I2C bus.
  static constexpr remus::platform::esp32::Mpu6050Config computerImu{
      GPIO_NUM_6, GPIO_NUM_5, GPIO_NUM_NC, 0x68, 400000};
  static remus::computer::ComputerAcquisitionService acquisitionService(computerImu);
  const esp_app_desc_t* app = esp_app_get_description();
  ESP_LOGI(kLogTag,
           "FIRMWARE: role=computer project=%s version=%s idf=%s build_date=%s build_time=%s",
           app->project_name, app->version, app->idf_ver, app->date, app->time);
  const remus::Status sdStatus = sdLogger.start();
  if (!sdStatus.ok()) {
    ESP_LOGE(kLogTag, "SD diagnostic log unavailable: error=%u detail=%lu",
             static_cast<unsigned>(sdStatus.error), static_cast<unsigned long>(sdStatus.detail));
  } else {
    static char bootDetails[320]{};
    std::snprintf(bootDetails, sizeof(bootDetails),
                  "role=computer reset_reason=%u project=%s version=%s idf=%s "
                  "build_date=%s build_time=%s free_heap=%lu minimum_free_heap=%lu",
                  static_cast<unsigned>(esp_reset_reason()), app->project_name, app->version,
                  app->idf_ver, app->date, app->time,
                  static_cast<unsigned long>(esp_get_free_heap_size()),
                  static_cast<unsigned long>(esp_get_minimum_free_heap_size()));
    sdLogger.enqueue(remus::computer::DiagnosticLogLevel::Info, "firmware_boot", bootDetails);
    ESP_LOGI(kLogTag,
             "SD diagnostic log ready: /sdcard/remus_pc.log + remus_pc.log.1 "
             "(%u MiB maximum)",
             static_cast<unsigned>(
                 remus::diagnostics::ComputerDiagnosticLogProfile::retentionBytes() /
                 (1024U * 1024U)));
  }

  const remus::Status status = runtime.start();
  if (!status.ok()) {
    ESP_LOGE(kLogTag, "runtime start failed: error=%u detail=%lu",
             static_cast<unsigned>(status.error), static_cast<unsigned long>(status.detail));
    static char failureDetails[96]{};
    std::snprintf(failureDetails, sizeof(failureDetails), "error=%u detail=%lu",
                  static_cast<unsigned>(status.error), static_cast<unsigned long>(status.detail));
    if (sdLogger.isReady()) {
      sdLogger.enqueue(remus::computer::DiagnosticLogLevel::Error, "runtime_start_failed",
                       failureDetails);
    }
    return;
  }
  ESP_LOGI(kLogTag, "Remus Computer core runtime ready (zero-Blade capable)");
  if (sdLogger.isReady()) {
    sdLogger.enqueue(remus::computer::DiagnosticLogLevel::Info, "runtime_ready", "role=computer");
  }

  // Wi-Fi/ESP-NOW initialization can occupy the ESP32-C3's single core for
  // tens of milliseconds. Stabilize it before opening RMS1 and starting the
  // 200 Hz sampler so startup work cannot create an avoidable sample gap.
  const remus::Status radioStatus = radioService.start();
  if (!radioStatus.ok()) {
    ESP_LOGE(kLogTag, "radio unavailable: error=%u detail=%lu; provision remus NVS credentials",
             static_cast<unsigned>(radioStatus.error),
             static_cast<unsigned long>(radioStatus.detail));
    if (sdLogger.isReady())
      sdLogger.enqueue(remus::computer::DiagnosticLogLevel::Error, "radio_start_failed",
                       "check_nvs_credentials=true");
  } else {
    ESP_LOGI(kLogTag, "Remus Computer M2 association service ready");
  }

  if (sdLogger.isReady() && radioStatus.ok()) {
    const remus::Status rmsStatus = sdLogger.startRecording(
        recordingId, radioService.computerId(), radioService.bootId(), radioService.networkId(),
        fnv1a(app->version), fnv1a("remus-computer-prototype1"));
    if (!rmsStatus.ok()) {
      ESP_LOGE(kLogTag, "RMS1 recording unavailable: error=%u detail=%lu",
               static_cast<unsigned>(rmsStatus.error),
               static_cast<unsigned long>(rmsStatus.detail));
    } else {
      radioService.setCaptureAllowed(true);
      acquisitionService.setStorageContext(&sdLogger, radioService.computerId(),
                                           radioService.bootId());
      ESP_LOGI(kLogTag, "RMS1 recording: started");
    }
  }

  const remus::Status acquisitionStatus =
      sdLogger.isRecording() ? acquisitionService.start(recordingId)
                             : remus::Status::failure(remus::Error::StorageUnavailable);
  if (!acquisitionStatus.ok()) {
    ESP_LOGE(kLogTag, "local IMU unavailable: error=%u detail=%lu; system remains operational",
             static_cast<unsigned>(acquisitionStatus.error),
             static_cast<unsigned long>(acquisitionStatus.detail));
    if (sdLogger.isReady()) {
      static char failureDetails[112]{};
      std::snprintf(failureDetails, sizeof(failureDetails),
                    "error=%u detail=%lu sda=6 scl=5 address=0x68",
                    static_cast<unsigned>(acquisitionStatus.error),
                    static_cast<unsigned long>(acquisitionStatus.detail));
      sdLogger.enqueue(remus::computer::DiagnosticLogLevel::Error,
                       "local_imu_start_failed", failureDetails);
    }
  } else if (sdLogger.isReady()) {
    static char readyDetails[144]{};
    std::snprintf(readyDetails, sizeof(readyDetails),
                  "rate_hz=200 sda=6 scl=5 address=0x%02x identity=0x%02x mode=software_timer",
                  static_cast<unsigned>(acquisitionService.detectedAddress()),
                  static_cast<unsigned>(acquisitionService.detectedIdentity()));
    sdLogger.enqueue(remus::computer::DiagnosticLogLevel::Info, "local_imu_ready",
                     readyDetails);
  }

  const remus::Status gpsStatus =
      gpsService.start(radioService.computerId(), radioService.bootId());
  if (!gpsStatus.ok()) {
    ESP_LOGW(kLogTag, "GPS: UNAVAILABLE error=%u detail=%lu; IMU capture remains operational",
             static_cast<unsigned>(gpsStatus.error),
             static_cast<unsigned long>(gpsStatus.detail));
    if (sdLogger.isReady()) {
      static char failureDetails[96]{};
      std::snprintf(failureDetails, sizeof(failureDetails), "error=%u detail=%lu",
                    static_cast<unsigned>(gpsStatus.error),
                    static_cast<unsigned long>(gpsStatus.detail));
      sdLogger.enqueue(remus::computer::DiagnosticLogLevel::Warning,
                       "gps_start_failed", failureDetails);
    }
  }

  remus::diagnostics::OperationalProblemCounters previousProblems{};
  for (;;) {
    const remus::diagnostics::HealthSnapshot health = runtime.healthSnapshot();
    const remus::acquisition::SamplingMetrics imu = acquisitionService.metrics();
    const remus::computer::SdDiagnosticLoggerStats sdLogStats = sdLogger.stats();
    const remus::computer::ComputerRecoveryStats recovery = radioService.recoveryStats();
    const remus::computer::GpsServiceStats gps = gpsService.stats();
    const remus::diagnostics::OperationalProblemCounters currentProblems{
        health.queueOverflows,
        health.poolExhaustions,
        health.invalidHandleReleases,
        sdLogStats.dropped,
        sdLogStats.writeFailures,
        imu.readErrors,
        imu.missedPeriods,
        sdLogStats.rmsDropped,
        sdLogStats.rmsWriteFailures,
        recovery.duplicateBatches,
        recovery.outsideWindowBatches,
        recovery.unrecoverableGaps,
        gps.checksumFailures,
        gps.decoderOverflows,
        sdLogStats.gpsDropped,
        0,
        0,
        0};
    static char operationalProblems[512]{};
    if (remus::diagnostics::formatNewOperationalProblems(
            previousProblems, currentProblems, operationalProblems,
            sizeof(operationalProblems))) {
      ESP_LOGW(kLogTag, "%s", operationalProblems);
      if (sdLogger.isReady())
        sdLogger.enqueue(remus::computer::DiagnosticLogLevel::Warning,
                         "operational_problem", operationalProblems);
    }
    previousProblems = currentProblems;
    vTaskDelay(kHealthLogInterval);
  }
}
