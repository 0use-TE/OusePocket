idf_component_register(SRCS "ouse_health.c" "ouse_metrics.c" "ouse_clock.c" "ouse_system.c" INCLUDE_DIRS "include"
    REQUIRES ouse_tasks esp_common esp_system heap esp_timer spi_flash nvs_flash freertos)
