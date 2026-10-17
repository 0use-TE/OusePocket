idf_component_register(SRCS "ouse_http.c" "ouse_net.c" INCLUDE_DIRS "include"
    REQUIRES ouse_tasks esp-tls mbedtls http_parser esp_timer freertos lwip)
