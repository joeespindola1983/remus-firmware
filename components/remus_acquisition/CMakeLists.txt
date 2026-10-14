idf_component_register(
  SRCS
    "src/AcquisitionPipeline.cpp"
    "src/BatchBuilder.cpp"
    "src/Sampler.cpp"
    "src/SampleClock.cpp"
  INCLUDE_DIRS "include"
  REQUIRES remus_core remus_hal
)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
