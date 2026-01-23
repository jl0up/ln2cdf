idf_component_register(SRCS "webserver.c" "temp_humidity.c" "sntp_client.c" "google_sheet.c" "adc_oneshot.c" "wifi_station.c" "delayed_restart.c" "chip_info.c" "display.c" "keypad.c" "ota_update.c" "main.c"
                    PRIV_REQUIRES spi_flash
                    INCLUDE_DIRS ""
                    REQUIRES esp_wifi nvs_flash esp_adc mbedtls esp_event esp_http_client driver freertos lvgl esp_system app_update esp_http_server esp_https_ota aht20 i2c_bus)
