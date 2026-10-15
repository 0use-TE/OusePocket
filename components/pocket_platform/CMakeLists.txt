idf_component_register(SRCS "pocket_platform.cpp" "pocket_battery.cpp" "pocket_logs.cpp" INCLUDE_DIRS "include"
    REQUIRES brookesia_service_manager nvs_flash esp_driver_i2c fatfs sdmmc esp_timer joltwallet__littlefs)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_23)
target_link_libraries(${COMPONENT_LIB} PUBLIC "-u ouse_power_plugin_symbol")
