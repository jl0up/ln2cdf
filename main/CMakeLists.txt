idf_component_register(SRCS "sntp_client.c" "google_sheet.c" "adc_oneshot.c" "wifi_station.c" "delayed_restart.c" "chip_info.c" "display.c" "keypad.c" "main.c"
                    PRIV_REQUIRES spi_flash
                    INCLUDE_DIRS ""
                    REQUIRES esp_wifi nvs_flash esp_adc mbedtls esp_event esp_http_client driver freertos lvgl esp_system soc hal esp_mm onewire_bus)
