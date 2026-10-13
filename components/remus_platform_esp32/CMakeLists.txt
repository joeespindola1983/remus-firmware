idf_component_register(
  SRCS "src/MbedTlsCrypto.cpp" "src/NvsCredentials.cpp"
  INCLUDE_DIRS "include"
  REQUIRES remus_core remus_hal mbedtls nvs_flash
)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
