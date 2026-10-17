idf_component_register(SRCS "ouse_files.c" INCLUDE_DIRS "include"
    REQUIRES ouse_tasks ouse_devices ouse_net fatfs sdmmc esp_timer freertos heap)
