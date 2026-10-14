idf_component_register(
  SRCS
    "src/EventLog.cpp"
    "src/HealthCounters.cpp"
    "src/OperationalSerial.cpp"
    "src/RotatingTextLog.cpp"
  INCLUDE_DIRS "include"
  REQUIRES remus_core
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
