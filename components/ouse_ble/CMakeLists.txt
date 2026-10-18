idf_component_register(SRCS "ouse_ble.c" INCLUDE_DIRS "include"
    REQUIRES esp_common ouse_tasks PRIV_REQUIRES bt espressif__esp_hosted nvs_flash esp_timer freertos heap)
