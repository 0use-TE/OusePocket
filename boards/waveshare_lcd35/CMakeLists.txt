idf_component_register(SRCS "ouse_board.c" "board_devices.c" "board_audio.c" "board_wifi.c" INCLUDE_DIRS "include"
    REQUIRES ouse_core ouse_devices ouse_media ouse_usb waveshare__esp32_p4_wifi6_touch_lcd_3_5 espressif__esp_lvgl_adapter
    PRIV_REQUIRES esp_driver_i2s esp_driver_i2c espressif__esp_codec_dev esp_wifi esp_event esp_netif nvs_flash esp_timer espressif__esp_hosted lwip)
