idf_component_register(SRCS "ouse_storage.c" INCLUDE_DIRS "include"
    REQUIRES ouse_tasks esp_common freertos esp_partition joltwallet__littlefs heap log)
