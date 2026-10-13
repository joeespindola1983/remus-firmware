idf_component_register(
  SRCS
    "BladeRuntime.cpp"
    "main.cpp"
  INCLUDE_DIRS "."
  REQUIRES remus_core remus_hal remus_diagnostics remus_link remus_platform_esp32
)

target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_17)
