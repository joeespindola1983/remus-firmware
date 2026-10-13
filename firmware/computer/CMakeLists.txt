cmake_minimum_required(VERSION 3.16)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(SDKCONFIG_DEFAULTS "sdkconfig.defaults")
list(APPEND EXTRA_COMPONENT_DIRS
  "${CMAKE_CURRENT_LIST_DIR}/src"
  "${CMAKE_CURRENT_LIST_DIR}/../../components/remus_core"
  "${CMAKE_CURRENT_LIST_DIR}/../../components/remus_hal"
  "${CMAKE_CURRENT_LIST_DIR}/../../components/remus_diagnostics"
)
list(REMOVE_DUPLICATES EXTRA_COMPONENT_DIRS)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(remus_computer)
