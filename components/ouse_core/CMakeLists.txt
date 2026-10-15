idf_component_register(SRCS "ouse_core.c" "ouse_apps.c" "ouse_shell.c" "ouse_theme.c" "ouse_navigation.cpp" "ouse_font_ui_16.c" INCLUDE_DIRS "include"
    REQUIRES lvgl__lvgl esp_common ouse_system log)
