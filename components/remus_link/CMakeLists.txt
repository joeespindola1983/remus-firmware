idf_component_register(
  SRCS
    "src/AckTracker.cpp"
    "src/BladeLinkStateMachine.cpp"
    "src/ComputerPeerStateMachine.cpp"
    "src/FrameCodec.cpp"
    "src/KeyDerivation.cpp"
    "src/MessageCodec.cpp"
    "src/RadioSupervisor.cpp"
    "src/ReplayWindow.cpp"
    "src/SourceRegistry.cpp"
    "src/TxScheduler.cpp"
  INCLUDE_DIRS "include"
  REQUIRES remus_core remus_hal
)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
