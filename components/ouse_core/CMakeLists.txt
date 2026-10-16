file(GLOB ui_fonts "${CMAKE_CURRENT_LIST_DIR}/ouse_font_ui_*.c")
# theme/ouse_theme_palette.inc is generated from theme/colors.json via tools/gen-theme-colors.py
idf_component_register(SRCS "ouse_core.c" "ouse_apps.c" "ouse_shell.c" "ouse_theme.c" "ouse_ui.c" "ouse_widgets.c" "ouse_dialog.c" "ouse_screen.c" "ouse_debug.c" "ouse_device_ui.c" "ouse_ime.c" "ouse_notifications.c" "ouse_navigation.cpp" ${ui_fonts} INCLUDE_DIRS "include" "."
    REQUIRES lvgl__lvgl esp_common ouse_system ouse_devices log freertos esp_timer)
