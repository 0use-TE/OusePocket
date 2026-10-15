idf_component_register(SRCS "pocket_platform.cpp" "pocket_battery.cpp" "pocket_logs.cpp" "pocket_resources.cpp" INCLUDE_DIRS "include"
    EMBED_FILES "assets/pocket-test.wav"
    REQUIRES brookesia_service_manager nvs_flash esp_driver_i2c fatfs sdmmc esp_timer joltwallet__littlefs
    PRIV_REQUIRES heap esp_hw_support esp_system spi_flash esp_partition app_update esp_app_format bootloader_support)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_23)
target_link_libraries(${COMPONENT_LIB} PUBLIC "-u ouse_power_plugin_symbol")
