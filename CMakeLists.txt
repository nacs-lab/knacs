project(KNaCs)

## Setup
cmake_minimum_required(VERSION 3.0...4.2)
set(CMAKE_MODULE_PATH "${CMAKE_MODULE_PATH}"
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(GNUInstallDirs)

if(NOT DEFINED KDIR)
  set(KDIR "/lib/modules/${KERNEL_VER}/build")
endif()

if(NOT DEFINED EXTRAMODULES_DIR)
  set(EXTRAMODULES_DIR "/lib/modules/${KERNEL_VER}/extramodules")
endif()

string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" system_processor)
if("${system_processor}" STREQUAL "x86_64" OR
    "${system_processor}" STREQUAL "x86-64" OR
    "${system_processor}" STREQUAL "amd64" OR
    "${system_processor}" STREQUAL "x86" OR
    "${system_processor}" STREQUAL "i386" OR
    "${system_processor}" STREQUAL "i486" OR
    "${system_processor}" STREQUAL "i586" OR
    "${system_processor}" STREQUAL "i686")
  set(KERNEL_ARCH "x86")
elseif("${system_processor}" STREQUAL "arm" OR
    "${system_processor}" STREQUAL "armv7l" OR
    "${system_processor}" STREQUAL "armhf")
  set(KERNEL_ARCH "arm")
elseif("${system_processor}" STREQUAL "aarch64" OR
    "${system_processor}" STREQUAL "arm64")
  set(KERNEL_ARCH "arm64")
else()
  message(WARNING "Unrecognized architecture ${system_processor}")
  set(KERNEL_ARCH "")
endif()
if(NOT "${KERNEL_ARCH}" STREQUAL "")
  set(KERNEL_ARCH_ARG "ARCH=${KERNEL_ARCH}")
else()
  set(KERNEL_ARCH_ARG "")
endif()

add_subdirectory(driver)
