idf_component_register(SRCS "ouse_wave.c" "ouse_media.c" INCLUDE_DIRS "include"
    REQUIRES ouse_core ouse_tasks ouse_devices ouse_net ouse_files esp_timer freertos)
