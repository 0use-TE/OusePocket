set(ouse_installer_sources "ouse_uninstall_journal.c" "ouse_installer.c" "ouse_bpk.c" "ouse_protocol.c")
if(CONFIG_OUSE_DEVICE_DEBUG)
    list(APPEND ouse_installer_sources "ouse_debug_transport.c")
endif()
idf_component_register(SRCS ${ouse_installer_sources}
    INCLUDE_DIRS "include" "."
    REQUIRES ouse_core ouse_runtime ouse_storage ouse_alarm ouse_media esp_driver_uart esp_partition heap
        joltwallet__littlefs espressif__quickjs-ng esp_timer
    PRIV_REQUIRES espressif__esp_new_jpeg unity esp_app_format)
