idf_component_register(SRCS "delayed_restart.c" "chip_info.c" "main.c"
                    PRIV_REQUIRES spi_flash
                    INCLUDE_DIRS "")
