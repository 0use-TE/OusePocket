idf_component_register(SRCS "main.c" "counter_app.c" INCLUDE_DIRS "."
    REQUIRES ouse_core ouse_system waveshare_lcd35 esp_system heap)
