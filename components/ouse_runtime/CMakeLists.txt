idf_component_register(SRCS "ouse_budget.c" "ouse_runtime.c" INCLUDE_DIRS "include"
    REQUIRES ouse_tasks ouse_app_manager ouse_ble ouse_net ouse_alarm ouse_media ouse_files ouse_usb ouse_camera ouse_recorder ouse_core ouse_system ouse_storage esp_timer log espressif__quickjs-ng)

if(NOT CMAKE_SCRIPT_MODE_FILE)
    # GCC 15 RISC-V defines uint32_t as unsigned long, while QuickJS 0.14's
    # string iterator passes uint32_t idx to string_getc through (int *)&idx.
    # With -O2 strict aliasing this can leave the iterator index unchanged.
    # -Wno-strict-aliasing only silences warnings. Preserve -O2 and the pinned
    # managed source, but disable that assumption for the QuickJS target only.
    idf_component_get_property(_ouse_quickjs_lib espressif__quickjs-ng COMPONENT_LIB)
    target_compile_options(${_ouse_quickjs_lib} PRIVATE -fno-strict-aliasing)
endif()
