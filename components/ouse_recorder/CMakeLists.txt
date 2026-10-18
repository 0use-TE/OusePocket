idf_component_register(SRCS "ouse_recorder.c" INCLUDE_DIRS "include"
    REQUIRES ouse_tasks ouse_devices ouse_files esp_timer freertos heap)
