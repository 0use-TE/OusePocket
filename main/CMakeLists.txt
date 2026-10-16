idf_component_register(SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES ouse_core ouse_system ouse_runtime ouse_installer waveshare_lcd35 esp_system espcoredump heap)
if(NOT CMAKE_SCRIPT_MODE_FILE)
    include("${CMAKE_CURRENT_LIST_DIR}/../boards/waveshare_lcd35/cmake/hosted_deferred_init.cmake")
    include("${CMAKE_CURRENT_LIST_DIR}/../boards/waveshare_lcd35/cmake/p4_fault_diagnostics.cmake")
endif()
