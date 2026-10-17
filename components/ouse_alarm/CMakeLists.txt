idf_component_register(SRCS "ouse_alarm.c" "ouse_alarm_schedule.c" INCLUDE_DIRS "include"
    REQUIRES ouse_tasks ouse_core ouse_devices ouse_storage esp_timer esp_rom freertos)
