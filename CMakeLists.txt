project(KNaCs)

## Setup
cmake_minimum_required(VERSION 3.0)
set(CMAKE_MODULE_PATH "${CMAKE_MODULE_PATH}"
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(GNUInstallDirs)

if(NOT DEFINED KDIR)
  set(KDIR "/lib/modules/${KERNEL_VER}/build")
endif()

if(NOT DEFINED EXTRAMODULES_DIR)
  set(EXTRAMODULES_DIR "/lib/modules/${KERNEL_VER}/extramodules")
endif()

add_subdirectory(driver)
