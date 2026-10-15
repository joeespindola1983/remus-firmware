idf_component_register(
  SRCS "src/NmeaDecoder.cpp" "src/UbxNavigationDecoder.cpp"
  INCLUDE_DIRS "include"
  REQUIRES remus_core
)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
