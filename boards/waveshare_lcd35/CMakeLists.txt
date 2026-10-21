idf_component_register(SRCS "ouse_board.c" "board_display.c" "board_lcd_frame.c" "board_lcd_frame_io.c" "board_lcd_scan_io.c" "board_lcd_sync.c" "display_flush_wake.c" "board_devices.c" "board_audio.c" "board_wifi.c" "board_sd.c" "board_sd_bus.c" "board_camera.c" "board_ble_probe.c" INCLUDE_DIRS "include"
    REQUIRES ouse_core ouse_devices ouse_media ouse_files ouse_usb ouse_camera ouse_recorder espressif__esp_video waveshare__esp32_p4_wifi6_touch_lcd_3_5 espressif__esp_lvgl_adapter
    PRIV_REQUIRES esp_driver_i2s esp_driver_i2c esp_driver_sdmmc esp_driver_sd_intf sdmmc espressif__esp_codec_dev esp_wifi esp_event esp_netif nvs_flash esp_timer espressif__esp_hosted lwip)
target_link_options(${COMPONENT_LIB} INTERFACE
    "-Wl,--wrap=lv_display_flush_ready"
    "-Wl,--wrap=esp_lv_adapter_register_display"
    "-Wl,--wrap=esp_lv_adapter_register_touch"
    "-Wl,--wrap=esp_lcd_new_panel_io_spi"
    "-Wl,--wrap=esp_lcd_panel_draw_bitmap"
    "-Wl,--wrap=esp_lcd_panel_io_tx_param"
    "-Wl,--wrap=esp_lcd_panel_io_rx_param"
    "-Wl,--wrap=esp_lcd_panel_io_tx_color"
    "-Wl,--wrap=esp_lcd_panel_io_register_event_callbacks"
    "-Wl,--wrap=spi_bus_initialize"
    "-Wl,--wrap=spi_bus_add_device"
    "-Wl,--wrap=spi_device_polling_transmit"
    "-Wl,--wrap=sdmmc_host_do_transaction" "-Wl,--wrap=sdmmc_write_sectors"
    "-Wl,--wrap=sdmmc_read_sectors" "-Wl,--wrap=sdmmc_card_init")
if(CONFIG_OUSE_DEVICE_DEBUG AND NOT CONFIG_ESP_HOSTED_ENABLE_BT_NIMBLE AND NOT CONFIG_ESP_HOSTED_ENABLE_BT_BLUEDROID)
    target_link_options(${COMPONENT_LIB} INTERFACE "-Wl,--undefined=ouse_board_debug_ble_probe")
endif()
