idf_component_register(
  SRCS "src/EspNowDriver.cpp" "src/MbedTlsCrypto.cpp" "src/Mpu6050Driver.cpp" "src/NvsCredentials.cpp"
  INCLUDE_DIRS "include"
  REQUIRES remus_core remus_hal esp_driver_gpio esp_driver_i2c esp_event esp_netif esp_timer esp_wifi mbedtls nvs_flash
)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
