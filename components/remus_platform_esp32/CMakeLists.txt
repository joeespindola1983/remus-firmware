idf_component_register(
  SRCS "src/EspNowDriver.cpp" "src/MbedTlsCrypto.cpp" "src/NvsCredentials.cpp"
  INCLUDE_DIRS "include"
  REQUIRES remus_core remus_hal esp_event esp_netif esp_timer esp_wifi mbedtls nvs_flash
)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
