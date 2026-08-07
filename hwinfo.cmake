# Vendored build of https://github.com/lfreist/hwinfo
#
# Options are set as normal variables: hwinfo declares them with option(), which
# under CMP0077 (NEW since its own cmake_minimum_required(3.22)) honours an
# existing variable instead of creating a cache entry. Doing it this way keeps
# BUILD_TESTING and friends untouched for the rest of score.
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/hwinfo/CMakeLists.txt")
  message(WARNING "score-addon-sysinfo: 3rdparty/hwinfo is missing, run: git submodule update --init --recursive")
  return()
endif()

set(HWINFO_STATIC ON)
set(HWINFO_SHARED OFF)

set(HWINFO_OS ON)
set(HWINFO_MAINBOARD ON)
set(HWINFO_CPU ON)
set(HWINFO_DISK ON)
set(HWINFO_RAM ON)
set(HWINFO_GPU ON)
set(HWINFO_BATTERY ON)
set(HWINFO_NETWORK ON)

# Would pull in an OpenCL SDK for a couple of extra GPU fields
set(HWINFO_GPU_OPENCL OFF)

set(BUILD_EXAMPLES OFF)
set(BUILD_TESTING OFF)

# Keeps the generated pci.ids header out of the top of score's build folder
set(HWINFO_CMAKE_BINARY_DIR "${CMAKE_BINARY_DIR}/hwinfo-build")

block()
  # hwinfo builds with -Wall -Wextra -Wpedantic, and /W4 /WX on MSVC
  if(MSVC)
    add_compile_options("/w")
  else()
    add_compile_options("-w")
  endif()

  add_subdirectory(3rdparty/hwinfo "${CMAKE_BINARY_DIR}/hwinfo-build" SYSTEM)
endblock()
