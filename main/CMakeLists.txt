idf_component_register(SRCS "main.c" "ouse_reliability.c"
    INCLUDE_DIRS "."
    REQUIRES ouse_native ouse_journal ouse_core ouse_ble ouse_system ouse_runtime ouse_installer ouse_net ouse_media ouse_alarm ouse_storage ouse_devices ouse_usb ouse_camera ouse_recorder waveshare_lcd35 esp_system espcoredump heap esp_netif)
if(NOT CMAKE_SCRIPT_MODE_FILE)
    include("${CMAKE_CURRENT_LIST_DIR}/../boards/waveshare_lcd35/cmake/hosted_deferred_init.cmake")
    include("${CMAKE_CURRENT_LIST_DIR}/../boards/waveshare_lcd35/cmake/hosted_memory_safety.cmake")
    include("${CMAKE_CURRENT_LIST_DIR}/../components/ouse_net/cmake/websocket_security.cmake")
    include("${CMAKE_CURRENT_LIST_DIR}/../boards/waveshare_lcd35/cmake/p4_fault_diagnostics.cmake")
endif()
