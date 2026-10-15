idf_component_register(
  SRCS "src/Esp32UartGps.cpp"
  INCLUDE_DIRS "include"
  REQUIRES remus_core remus_hal esp_driver_gpio esp_driver_uart
)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
