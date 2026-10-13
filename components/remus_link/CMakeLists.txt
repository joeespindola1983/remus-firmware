idf_component_register(
  SRCS "src/FrameCodec.cpp" "src/KeyDerivation.cpp" "src/MessageCodec.cpp"
  INCLUDE_DIRS "include"
  REQUIRES remus_core remus_hal
)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
