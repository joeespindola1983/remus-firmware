idf_component_register(
  SRCS
    "ComputerAcquisitionService.cpp"
    "ComputerRuntime.cpp"
    "ComputerRadioService.cpp"
    "GpsService.cpp"
    "SdDiagnosticLogger.cpp"
    "main.cpp"
  INCLUDE_DIRS "."
  REQUIRES
    remus_core
    remus_hal
    remus_diagnostics
    remus_acquisition
    remus_link
    remus_gps
    remus_storage
    remus_platform_esp32
    remus_platform_esp32_gps
    fatfs
    sdmmc
    esp_driver_spi
    esp_driver_sdspi
)

target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_17)
