idf_component_register(
  SRCS
    "BladeAcquisitionService.cpp"
    "BladeRuntime.cpp"
    "BladeRadioService.cpp"
    "main.cpp"
  INCLUDE_DIRS "."
  REQUIRES remus_core remus_hal remus_diagnostics remus_acquisition remus_link remus_platform_esp32 nvs_flash
)

target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_17)
