idf_component_register(SRCS "ouse_native.c" "ouse_native_validate.c" "ouse_native_memory.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES espressif__elf_loader ouse_files esp_timer esp_mm heap)
target_link_libraries(${COMPONENT_LIB} INTERFACE
    "-Wl,--wrap=esp_elf_malloc" "-Wl,--wrap=esp_elf_free")
