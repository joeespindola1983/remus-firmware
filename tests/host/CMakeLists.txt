cmake_minimum_required(VERSION 3.16)
project(remus_host_tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

enable_testing()

set(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")

add_library(remus_core STATIC
  "${REPO_ROOT}/components/remus_core/src/Config.cpp"
  "${REPO_ROOT}/components/remus_core/src/Ids.cpp"
  "${REPO_ROOT}/components/remus_core/src/PacketPool.cpp"
)
target_include_directories(remus_core PUBLIC
  "${REPO_ROOT}/components/remus_core/include"
)
target_compile_options(remus_core PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_library(remus_hal INTERFACE)
target_include_directories(remus_hal INTERFACE
  "${REPO_ROOT}/components/remus_hal/include"
)
target_link_libraries(remus_hal INTERFACE remus_core)

add_library(remus_diagnostics STATIC
  "${REPO_ROOT}/components/remus_diagnostics/src/EventLog.cpp"
  "${REPO_ROOT}/components/remus_diagnostics/src/HealthCounters.cpp"
  "${REPO_ROOT}/components/remus_diagnostics/src/RotatingTextLog.cpp"
)
target_include_directories(remus_diagnostics PUBLIC
  "${REPO_ROOT}/components/remus_diagnostics/include"
)
target_link_libraries(remus_diagnostics PUBLIC remus_core)
target_compile_options(remus_diagnostics PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_library(remus_link STATIC
  "${REPO_ROOT}/components/remus_link/src/BladeLinkStateMachine.cpp"
  "${REPO_ROOT}/components/remus_link/src/ComputerPeerStateMachine.cpp"
  "${REPO_ROOT}/components/remus_link/src/FrameCodec.cpp"
  "${REPO_ROOT}/components/remus_link/src/KeyDerivation.cpp"
  "${REPO_ROOT}/components/remus_link/src/MessageCodec.cpp"
  "${REPO_ROOT}/components/remus_link/src/RadioSupervisor.cpp"
  "${REPO_ROOT}/components/remus_link/src/SourceRegistry.cpp"
  "${REPO_ROOT}/components/remus_link/src/TxScheduler.cpp"
)
target_include_directories(remus_link PUBLIC
  "${REPO_ROOT}/components/remus_link/include"
)
target_link_libraries(remus_link PUBLIC remus_hal)
target_compile_options(remus_link PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_executable(test_core unit/test_core.cpp)
target_link_libraries(test_core PRIVATE remus_core)
target_compile_options(test_core PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME core COMMAND test_core)

add_executable(test_fakes unit/test_fakes.cpp)
target_include_directories(test_fakes PRIVATE fakes)
target_link_libraries(test_fakes PRIVATE remus_hal)
target_compile_options(test_fakes PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME fakes COMMAND test_fakes)

add_executable(test_runtimes
  integration/test_runtimes.cpp
  "${REPO_ROOT}/firmware/blade/src/BladeRuntime.cpp"
  "${REPO_ROOT}/firmware/computer/src/ComputerRuntime.cpp"
)
target_include_directories(test_runtimes PRIVATE
  "${REPO_ROOT}/firmware/blade/src"
  "${REPO_ROOT}/firmware/computer/src"
)
target_link_libraries(test_runtimes PRIVATE remus_diagnostics remus_hal)
target_compile_options(test_runtimes PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME runtimes COMMAND test_runtimes)

add_executable(test_rotating_text_log unit/test_rotating_text_log.cpp)
target_link_libraries(test_rotating_text_log PRIVATE remus_diagnostics)
target_compile_options(test_rotating_text_log PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME rotating_text_log COMMAND test_rotating_text_log)

add_executable(test_remus_rf_v1 unit/test_remus_rf_v1.cpp)
target_include_directories(test_remus_rf_v1 PRIVATE fakes)
target_link_libraries(test_remus_rf_v1 PRIVATE remus_link remus_hal)
target_compile_options(test_remus_rf_v1 PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME remus_rf_v1 COMMAND test_remus_rf_v1)

add_executable(test_link_state unit/test_link_state.cpp)
target_link_libraries(test_link_state PRIVATE remus_link remus_hal)
target_compile_options(test_link_state PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME link_state COMMAND test_link_state)

add_library(remus_acquisition STATIC
  "${REPO_ROOT}/components/remus_acquisition/src/AcquisitionPipeline.cpp"
  "${REPO_ROOT}/components/remus_acquisition/src/BatchBuilder.cpp"
  "${REPO_ROOT}/components/remus_acquisition/src/Sampler.cpp"
  "${REPO_ROOT}/components/remus_acquisition/src/SampleClock.cpp"
)
target_include_directories(remus_acquisition PUBLIC
  "${REPO_ROOT}/components/remus_acquisition/include"
)
target_link_libraries(remus_acquisition PUBLIC remus_hal)
target_compile_options(remus_acquisition PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_executable(test_acquisition unit/test_acquisition.cpp)
target_include_directories(test_acquisition PRIVATE
  fakes
  "${REPO_ROOT}/components/remus_platform_esp32/include"
)
target_link_libraries(test_acquisition PRIVATE remus_acquisition remus_hal)
target_compile_options(test_acquisition PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME acquisition COMMAND test_acquisition)

add_executable(test_data_batch unit/test_data_batch.cpp)
target_include_directories(test_data_batch PRIVATE fakes)
target_link_libraries(test_data_batch PRIVATE remus_link remus_hal)
target_compile_options(test_data_batch PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME data_batch COMMAND test_data_batch)
