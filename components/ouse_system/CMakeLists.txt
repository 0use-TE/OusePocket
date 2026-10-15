idf_component_register(SRCS "ouse_system.c" INCLUDE_DIRS "include"
    REQUIRES esp_common esp_system heap esp_timer spi_flash nvs_flash freertos)
