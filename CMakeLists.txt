cmake_minimum_required(VERSION 3.24)
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/components/ouse_core"
    "${CMAKE_CURRENT_LIST_DIR}/boards/waveshare_lcd35")
# Keep component discovery independent from the old Brookesia example.
set(COMPONENTS main)
set(CMAKE_NINJA_FORCE_RESPONSE_FILE 1 CACHE INTERNAL "")
if(CMAKE_HOST_WIN32)
    set(CONFIGDEP_ENABLE 0 CACHE BOOL "Avoid Windows AUX configdep path" FORCE)
endif()
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(ouse_core_desktop)
