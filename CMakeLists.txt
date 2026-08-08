cmake_minimum_required(VERSION 3.13 FATAL_ERROR)

if(EMSCRIPTEN)
  return()
endif()

if(NOT TARGET score_lib_base)
  include(ScoreExternalAddon)
endif()

project(score_addon_sysinfo LANGUAGES CXX)
score_common_setup()

include(hwinfo.cmake)
if(NOT TARGET score_addon_sysinfo_hwinfo)
  return()
endif()

add_library(score_addon_sysinfo
  "${CMAKE_CURRENT_SOURCE_DIR}/SysInfo/Device.hpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/SysInfo/Platform.hpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/SysInfo/Protocol.hpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/SysInfo/ProtocolFactory.hpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/SysInfo/ProtocolSettingsWidget.hpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/SysInfo/SpecificSettings.hpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/SysInfo/Tree.hpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/score_addon_sysinfo.hpp"

  "${CMAKE_CURRENT_SOURCE_DIR}/SysInfo/Device.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/SysInfo/Platform.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/SysInfo/Protocol.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/SysInfo/ProtocolFactory.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/SysInfo/ProtocolSettingsWidget.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/SysInfo/SpecificSettingsSerialization.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/SysInfo/Tree.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/score_addon_sysinfo.cpp"
)

target_include_directories(score_addon_sysinfo PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

# On Windows <windows.h> reaches most of these files - through
# ossia/detail/dylib_loader.hpp, boost::asio, and hwinfo/cpu.h which includes
# wmi_wrapper.h. Merged into one translation unit its macros leak forward:
# uuid_t becomes UUID and breaks score::uuid_t in InterfaceList.hpp.
set_target_properties(score_addon_sysinfo PROPERTIES UNITY_BUILD OFF)

target_link_libraries(score_addon_sysinfo
  PUBLIC
    score_plugin_engine
  PRIVATE
    score_addon_sysinfo_hwinfo
)

if(WIN32)
  # GetIfTable2, for the network throughput counters
  target_link_libraries(score_addon_sysinfo PRIVATE iphlpapi)
endif()

setup_score_plugin(score_addon_sysinfo)
