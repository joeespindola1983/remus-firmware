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
  const remus::Status sdStatus = sdLogger.start();
  if (!sdStatus.ok()) {
    ESP_LOGE(kLogTag, "SD diagnostic log unavailable: error=%u detail=%lu",
             static_cast<unsigned>(sdStatus.error), static_cast<unsigned long>(sdStatus.detail));
  } else {
    const esp_app_desc_t* app = esp_app_get_description();
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
             "(1 MiB maximum)");
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
    const esp_app_desc_t* app = esp_app_get_description();
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
      sdLogger.isRecording()
          ? gpsService.start(radioService.computerId(), radioService.bootId())
          : remus::Status::failure(remus::Error::StorageUnavailable);
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
  std::uint32_t previousGpsDropped = 0;
  for (;;) {
    const remus::diagnostics::HealthSnapshot health = runtime.healthSnapshot();
    const remus::acquisition::SamplingMetrics imu = acquisitionService.metrics();
    const remus::computer::SdDiagnosticLoggerStats sdLogStats = sdLogger.stats();
    const remus::computer::ComputerRecoveryStats recovery = radioService.recoveryStats();
    const remus::computer::GpsServiceStats gps = gpsService.stats();
    static char healthDetails[768]{};
    std::snprintf(healthDetails, sizeof(healthDetails),
                  "uptime_ms=%llu runtime_starts=%lu queue_overflows=%lu pool_exhaustions=%lu "
                  "invalid_handle_releases=%lu free_heap=%lu minimum_free_heap=%lu "
                  "sd_log_queued=%lu sd_log_dropped=%lu sd_log_write_failures=%lu "
                  "sd_log_rotations=%lu imu_attempts=%llu imu_valid=%llu imu_errors=%llu "
                  "imu_missed=%llu imu_max_jitter_us=%llu local_batches=%llu "
                  "rms_queued=%lu rms_written=%lu rms_dropped=%lu rms_write_failures=%lu "
                  "rms_checkpoints=%lu rms_bytes=%llu sd_log_queue_hwm=%lu rms_queue_hwm=%lu "
                  "gps_bytes=%llu gps_records_queued=%lu gps_records_written=%lu "
                  "gps_records_dropped=%lu gps_coherent_epochs=%lu gps_checksum_failures=%lu "
                  "gps_fix_transitions=%lu gps_queue_hwm=%lu",
                  static_cast<unsigned long long>(esp_timer_get_time() / 1000),
                  static_cast<unsigned long>(health.runtimeStarts),
                  static_cast<unsigned long>(health.queueOverflows),
                  static_cast<unsigned long>(health.poolExhaustions),
                  static_cast<unsigned long>(health.invalidHandleReleases),
                  static_cast<unsigned long>(esp_get_free_heap_size()),
                  static_cast<unsigned long>(esp_get_minimum_free_heap_size()),
                  static_cast<unsigned long>(sdLogStats.queued),
                  static_cast<unsigned long>(sdLogStats.dropped),
                  static_cast<unsigned long>(sdLogStats.writeFailures),
                  static_cast<unsigned long>(sdLogStats.rotations),
                  static_cast<unsigned long long>(imu.attempts),
                  static_cast<unsigned long long>(imu.validSamples),
                  static_cast<unsigned long long>(imu.readErrors),
                  static_cast<unsigned long long>(imu.missedPeriods),
                  static_cast<unsigned long long>(imu.maxAbsJitterUs),
                  static_cast<unsigned long long>(acquisitionService.batchCount()),
                  static_cast<unsigned long>(sdLogStats.rmsQueued),
                  static_cast<unsigned long>(sdLogStats.rmsWritten),
                  static_cast<unsigned long>(sdLogStats.rmsDropped),
                  static_cast<unsigned long>(sdLogStats.rmsWriteFailures),
                  static_cast<unsigned long>(sdLogStats.rmsCheckpoints),
                  static_cast<unsigned long long>(sdLogStats.rmsBytes),
                  static_cast<unsigned long>(sdLogStats.diagnosticQueueHighWater),
                  static_cast<unsigned long>(sdLogStats.rmsQueueHighWater),
                  static_cast<unsigned long long>(gps.bytes),
                  static_cast<unsigned long>(gps.recordsQueued),
                  static_cast<unsigned long>(sdLogStats.gpsWritten),
                  static_cast<unsigned long>(gps.recordsDropped + sdLogStats.gpsDropped),
                  static_cast<unsigned long>(gps.coherentEpochs),
                  static_cast<unsigned long>(gps.checksumFailures),
                  static_cast<unsigned long>(gps.fixTransitions),
                  static_cast<unsigned long>(sdLogStats.gpsQueueHighWater));
    if (sdLogger.isReady() &&
        !sdLogger.enqueue(remus::computer::DiagnosticLogLevel::Info, "health", healthDetails)) {
      ESP_LOGW(kLogTag, "SD diagnostic log queue full; entry dropped");
    }
    static char recoveryDetails[256]{};
    std::snprintf(recoveryDetails, sizeof(recoveryDetails),
                  "ack_frames_queued=%lu checkpoint_acks_queued=%lu "
                  "replay_requests_queued=%lu duplicate_batches=%lu "
                  "outside_window_batches=%lu unrecoverable_gaps=%lu",
                  static_cast<unsigned long>(recovery.ackFramesQueued),
                  static_cast<unsigned long>(recovery.checkpointAcksQueued),
                  static_cast<unsigned long>(recovery.replayRequestsQueued),
                  static_cast<unsigned long>(recovery.duplicateBatches),
                  static_cast<unsigned long>(recovery.outsideWindowBatches),
                  static_cast<unsigned long>(recovery.unrecoverableGaps));
    if (sdLogger.isReady())
      sdLogger.enqueue(remus::computer::DiagnosticLogLevel::Info, "radio_recovery",
                       recoveryDetails);
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
        recovery.unrecoverableGaps};
    static char operationalProblems[384]{};
    if (remus::diagnostics::formatNewOperationalProblems(
            previousProblems, currentProblems, operationalProblems,
            sizeof(operationalProblems)))
      ESP_LOGW(kLogTag, "%s", operationalProblems);
    previousProblems = currentProblems;
    const std::uint32_t gpsDropped = gps.recordsDropped + sdLogStats.gpsDropped;
    if (gpsDropped > previousGpsDropped)
      ESP_LOGW(kLogTag, "GPS: record_drop total=%lu", static_cast<unsigned long>(gpsDropped));
    previousGpsDropped = gpsDropped;
    vTaskDelay(kHealthLogInterval);
  }
}
