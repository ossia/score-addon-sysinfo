# Vendored build of https://github.com/lfreist/hwinfo
#
# Its own CMakeLists is not used: it would build shared libraries by default,
# add install rules for archives and headers into score's install tree, and
# compile with -Werror on MSVC. The sources are compiled straight into an
# object library instead, so the add-on ends up self-contained in a single
# plug-in with nothing to deploy alongside it.
set(HWINFO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/hwinfo")

if(NOT EXISTS "${HWINFO_DIR}/CMakeLists.txt")
  message(WARNING "score-addon-sysinfo: 3rdparty/hwinfo is missing, run: git submodule update --init --recursive")
  return()
endif()

# The PCI vendor / device database, which the Linux GPU backend maps ids through
set(HWINFO_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/hwinfo-generated")
set(HWINFO_PCI_IDS_HEADER "${HWINFO_GENERATED_DIR}/pci.ids.h")

add_custom_command(
  OUTPUT "${HWINFO_PCI_IDS_HEADER}"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${HWINFO_GENERATED_DIR}"
  COMMAND ${CMAKE_COMMAND}
    -DINPUT=${HWINFO_DIR}/data/pci.ids
    -DOUTPUT=${HWINFO_PCI_IDS_HEADER}
    -DNAME=pci_ids
    -P "${HWINFO_DIR}/data/embed.cmake"
  DEPENDS "${HWINFO_DIR}/data/pci.ids" "${HWINFO_DIR}/data/embed.cmake"
  COMMENT "score-addon-sysinfo: generating pci_ids header"
  VERBATIM
)

# Every backend guards itself on HWINFO_UNIX / HWINFO_APPLE / HWINFO_WINDOWS,
# so the ones that do not apply compile to nothing.
add_library(score_addon_sysinfo_hwinfo OBJECT
  "${HWINFO_PCI_IDS_HEADER}"

  "${HWINFO_DIR}/src/battery.cpp"
  "${HWINFO_DIR}/src/cpu.cpp"
  "${HWINFO_DIR}/src/disk.cpp"
  "${HWINFO_DIR}/src/gpu.cpp"
  "${HWINFO_DIR}/src/mainboard.cpp"
  "${HWINFO_DIR}/src/network.cpp"
  "${HWINFO_DIR}/src/os.cpp"
  "${HWINFO_DIR}/src/ram.cpp"
  "${HWINFO_DIR}/src/PCIMapper.cpp"

  "${HWINFO_DIR}/src/apple/battery.cpp"
  "${HWINFO_DIR}/src/apple/cpu.cpp"
  "${HWINFO_DIR}/src/apple/disk.cpp"
  "${HWINFO_DIR}/src/apple/gpu.cpp"
  "${HWINFO_DIR}/src/apple/mainboard.cpp"
  "${HWINFO_DIR}/src/apple/network.cpp"
  "${HWINFO_DIR}/src/apple/os.cpp"
  "${HWINFO_DIR}/src/apple/ram.cpp"
  "${HWINFO_DIR}/src/apple/monitoring/cpu.cpp"
  "${HWINFO_DIR}/src/apple/monitoring/disk.cpp"
  "${HWINFO_DIR}/src/apple/monitoring/ram.cpp"

  "${HWINFO_DIR}/src/linux/battery.cpp"
  "${HWINFO_DIR}/src/linux/cpu.cpp"
  "${HWINFO_DIR}/src/linux/disk.cpp"
  "${HWINFO_DIR}/src/linux/gpu.cpp"
  "${HWINFO_DIR}/src/linux/mainboard.cpp"
  "${HWINFO_DIR}/src/linux/network.cpp"
  "${HWINFO_DIR}/src/linux/os.cpp"
  "${HWINFO_DIR}/src/linux/ram.cpp"
  "${HWINFO_DIR}/src/linux/monitoring/cpu.cpp"
  "${HWINFO_DIR}/src/linux/monitoring/disk.cpp"
  "${HWINFO_DIR}/src/linux/monitoring/ram.cpp"

  "${HWINFO_DIR}/src/windows/battery.cpp"
  "${HWINFO_DIR}/src/windows/cpu.cpp"
  "${HWINFO_DIR}/src/windows/disk.cpp"
  "${HWINFO_DIR}/src/windows/gpu.cpp"
  "${HWINFO_DIR}/src/windows/mainboard.cpp"
  "${HWINFO_DIR}/src/windows/network.cpp"
  "${HWINFO_DIR}/src/windows/os.cpp"
  "${HWINFO_DIR}/src/windows/ram.cpp"
  "${HWINFO_DIR}/src/windows/monitoring/cpu.cpp"
  "${HWINFO_DIR}/src/windows/monitoring/disk.cpp"
  "${HWINFO_DIR}/src/windows/monitoring/ram.cpp"
  "${HWINFO_DIR}/src/windows/utils/wmi_wrapper.cpp"
)

target_include_directories(score_addon_sysinfo_hwinfo
  SYSTEM PUBLIC
    "${HWINFO_DIR}/include"
    "${HWINFO_GENERATED_DIR}"
)

# HWINFO_API is a dllimport / dllexport attribute otherwise
target_compile_definitions(score_addon_sysinfo_hwinfo PUBLIC HWINFO_STATIC)

# HWINFO_STATIC only blanks HWINFO_API on Windows; everywhere else the macro is
# __attribute__((visibility("default"))), which overrides score's
# -fvisibility=hidden and would put ~110 hwinfo:: symbols in the plug-in's
# dynamic symbol table. platform.h is #pragma once, so including it up front and
# blanking the macro afterwards leaves nothing exported. Applied to consumers
# too, so that the class definitions carry the same attribute in every
# translation unit.
if(NOT WIN32)
  set(HWINFO_VISIBILITY_SHIM "${CMAKE_CURRENT_BINARY_DIR}/hwinfo_visibility.h")
  file(CONFIGURE OUTPUT "${HWINFO_VISIBILITY_SHIM}" CONTENT [[
#pragma once
#include <hwinfo/platform.h>
#undef HWINFO_API
#define HWINFO_API
]])
  target_compile_options(score_addon_sysinfo_hwinfo
    PUBLIC "SHELL:-include ${HWINFO_VISIBILITY_SHIM}")
endif()

set_target_properties(score_addon_sysinfo_hwinfo PROPERTIES
  POSITION_INDEPENDENT_CODE ON
  # hwinfo defines a file-local struct Jiffies in both src/linux/cpu.cpp and
  # src/linux/monitoring/cpu.cpp, each with its own get_jiffies(). Legal apart,
  # ambiguous once score's unity build puts them in one translation unit.
  UNITY_BUILD OFF

  # setup_score_plugin() only applies these to the plug-in target, and this
  # object library never goes through it
  C_VISIBILITY_PRESET hidden
  CXX_VISIBILITY_PRESET hidden
  VISIBILITY_INLINES_HIDDEN ON
)

if(MSVC)
  target_compile_options(score_addon_sysinfo_hwinfo PRIVATE "/w")
else()
  target_compile_options(score_addon_sysinfo_hwinfo PRIVATE "-w")
endif()

if(WIN32)
  target_link_libraries(score_addon_sysinfo_hwinfo
    PUBLIC ntdll powrprof dxgi setupapi wbemuuid ole32 oleaut32)
elseif(APPLE)
  target_link_libraries(score_addon_sysinfo_hwinfo
    PUBLIC "-framework IOKit" "-framework CoreFoundation")
endif()
