idf_component_register(SRCS "main.cpp" "waveshare_display.cpp" "waveshare_audio.cpp"
    INCLUDE_DIRS "." REQUIRES nvs_flash pocket_platform)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_23)

# Override only the scheduler's millisecond idle wait; no edits to .reference.
include("${CMAKE_CURRENT_LIST_DIR}/../../../components/pocket_platform/cmake/scheduler_poll_wait.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/../../../components/pocket_platform/cmake/pthread_memory_policy.cmake")
target_link_libraries(${COMPONENT_LIB} PRIVATE
    "-u ouse_display_plugin_symbol" "-u ouse_audio_plugin_symbol")

# Use the same resource staging mechanism as the official System Super example.
include("${CMAKE_CURRENT_LIST_DIR}/../../../components/brookesia_system_core/cmake/runtime_app_stage.cmake")
brookesia_system_core_get_esp_runtime_app_stage_root(pocket_stage_root)
# Files is deliberately no longer preinstalled. Keep its development source.
# Remove only its old generated staging copy on reconfigure (not user storage).
get_filename_component(pocket_removed_files "${pocket_stage_root}/net.0use.files" ABSOLUTE)
if(NOT pocket_removed_files STREQUAL "${PROJECT_DIR}/littlefs/apps/net.0use.files")
    message(FATAL_ERROR "Refusing to remove an unexpected Files staging path: ${pocket_removed_files}")
endif()
file(REMOVE_RECURSE "${pocket_removed_files}")
get_property(app_stage GLOBAL PROPERTY BROOKESIA_RUNTIME_APP_STAGE_TARGETS)
get_property(system_stage GLOBAL PROPERTY BROOKESIA_SYSTEM_SUPER_STAGE_TARGETS)
littlefs_create_partition_image(littlefs_data ../littlefs FLASH_IN_PROJECT
    DEPENDS ${app_stage} ${system_stage})
