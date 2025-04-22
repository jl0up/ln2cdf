idf_component_register(SRCS "wifi_station.c" "delayed_restart.c" "chip_info.c" "main.c"
                    PRIV_REQUIRES spi_flash
                    INCLUDE_DIRS ""
                    REQUIRES esp_wifi nvs_flash)
