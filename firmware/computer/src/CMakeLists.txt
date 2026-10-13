idf_component_register(
  SRCS
    "ComputerRuntime.cpp"
    "SdDiagnosticLogger.cpp"
    "main.cpp"
  INCLUDE_DIRS "."
  REQUIRES
    remus_core
    remus_hal
    remus_diagnostics
    remus_link
    remus_platform_esp32
    fatfs
    sdmmc
    esp_driver_spi
    esp_driver_sdspi
)

target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_17)
