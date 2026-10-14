idf_component_register(SRCS "main.cpp" "waveshare_display.cpp" INCLUDE_DIRS ".")
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_23)
target_link_libraries(${COMPONENT_LIB} PRIVATE "-u ouse_display_plugin_symbol")

# Use the same resource staging mechanism as the official System Super example.
get_property(app_stage GLOBAL PROPERTY BROOKESIA_RUNTIME_APP_STAGE_TARGETS)
get_property(system_stage GLOBAL PROPERTY BROOKESIA_SYSTEM_SUPER_STAGE_TARGETS)
littlefs_create_partition_image(littlefs_data ../littlefs FLASH_IN_PROJECT
    DEPENDS ${app_stage} ${system_stage})
