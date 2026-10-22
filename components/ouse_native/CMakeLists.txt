idf_component_register(SRCS "ouse_native.c" "ouse_native_validate.c" "ouse_native_memory.c" "ouse_native_app.c" "ouse_native_game.c" "ouse_quake2_app.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "../ouse_core"
    REQUIRES ouse_core
    PRIV_REQUIRES espressif__elf_loader ouse_storage ouse_files esp_timer esp_mm heap)
target_link_libraries(${COMPONENT_LIB} INTERFACE
    "-Wl,--wrap=esp_elf_malloc" "-Wl,--wrap=esp_elf_free")
if(NOT CMAKE_SCRIPT_MODE_FILE)
    idf_build_get_property(native_build_dir BUILD_DIR)
    set(native_demo "${CMAKE_CURRENT_BINARY_DIR}/ouse_native_demo.elf")
    add_custom_command(OUTPUT "${native_demo}"
        COMMAND ${PYTHON} "${PROJECT_DIR}/tools/native.py" build
            "${PROJECT_DIR}/native-apps/hello/main.c" "${native_demo}"
            --firmware-build "${native_build_dir}"
        DEPENDS "${PROJECT_DIR}/tools/native.py" "${PROJECT_DIR}/native-apps/hello/main.c"
            "${CMAKE_CURRENT_LIST_DIR}/include/ouse_native_api.h"
            "${native_build_dir}/toolchain/cflags" "${native_build_dir}/project_description.json"
        VERBATIM)
    add_custom_target(ouse_native_demo DEPENDS "${native_demo}")
    add_dependencies(${COMPONENT_LIB} ouse_native_demo)
    target_add_binary_data(${COMPONENT_LIB} "${native_demo}" BINARY RENAME_TO ouse_native_demo DEPENDS ouse_native_demo)
endif()
