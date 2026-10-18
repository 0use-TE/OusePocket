idf_component_register(SRCS "ouse_camera.c" "ouse_avi.c" INCLUDE_DIRS "include"
    REQUIRES ouse_tasks ouse_files ouse_devices esp_driver_jpeg esp_timer esp_hw_support heap freertos)
