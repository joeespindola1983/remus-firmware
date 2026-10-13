idf_component_register(
  SRCS
    "src/Config.cpp"
    "src/Ids.cpp"
    "src/PacketPool.cpp"
  INCLUDE_DIRS "include"
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
