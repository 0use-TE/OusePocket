set(ouse_installer_sources "ouse_uninstall_journal.c" "ouse_installer.c" "ouse_bpk.c" "ouse_protocol.c")
if(CONFIG_OUSE_DEVICE_DEBUG)
    list(APPEND ouse_installer_sources "ouse_debug_transport.c")
endif()
idf_component_register(SRCS ${ouse_installer_sources}
    INCLUDE_DIRS "include" "."
    REQUIRES ouse_core ouse_runtime ouse_storage ouse_alarm ouse_media ouse_files ouse_usb ouse_app_manager esp_driver_uart esp_partition heap
        joltwallet__littlefs espressif__quickjs-ng esp_timer
    PRIV_REQUIRES ouse_net mbedtls espressif__esp_new_jpeg unity esp_app_format)

if(NOT CMAKE_SCRIPT_MODE_FILE)
    file(GLOB_RECURSE store_sources CONFIGURE_DEPENDS "${PROJECT_DIR}/apps/store/*")
    set(store_bpk "${CMAKE_CURRENT_BINARY_DIR}/ouse_builtin_store.bpk")
    add_custom_command(OUTPUT "${store_bpk}"
        COMMAND ${PYTHON} "${PROJECT_DIR}/tools/bpk.py" pack "${PROJECT_DIR}/apps/store" "${store_bpk}"
        DEPENDS ${store_sources} "${PROJECT_DIR}/tools/bpk.py" VERBATIM)
    add_custom_target(ouse_builtin_store DEPENDS "${store_bpk}")
    add_dependencies(${COMPONENT_LIB} ouse_builtin_store)
    target_add_binary_data(${COMPONENT_LIB} "${store_bpk}" BINARY RENAME_TO ouse_builtin_store DEPENDS ouse_builtin_store)
endif()
