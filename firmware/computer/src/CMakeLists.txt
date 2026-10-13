idf_component_register(
  SRCS
    "ComputerRuntime.cpp"
    "main.cpp"
  INCLUDE_DIRS "."
  REQUIRES remus_core remus_hal remus_diagnostics
)

target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_17)
