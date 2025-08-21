#ifndef DISPLAY_H
#define DISPLAY_H

#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "lvgl.h"

// GPIO pin definitions for ESP32-C6 - Display ST7789
#define LCD_SCLK_GPIO       6    // SCLK (Serial Clock)
#define LCD_MOSI_GPIO       7    // MOSI (Master Out Slave In)
#define LCD_RST_GPIO        8   // Reset pin
#define LCD_DC_GPIO         9   // Data/Command pin
#define LCD_BL_GPIO         15   // Backlight pin
#define LCD_CS_GPIO         18   // CS (Chip Select)

// Display configuration
#define LCD_WIDTH           170
#define LCD_HEIGHT          320
#define LCD_SPI_CLOCK_HZ    (20 * 1000 * 1000)  // 20MHz

// ST7789 controller frame buffer size
#define ST7789_WIDTH        240
#define ST7789_HEIGHT       320

// Calculate offset to center the display
#define COLUMN_OFFSET       ((ST7789_WIDTH - LCD_WIDTH) / 2)  // (240-170)/2 = 35
#define ROW_OFFSET          0  // No row offset needed

// Buffer configuration
#define BUFFER_LINES        40
#define BUFFER_LENGTH       (LCD_WIDTH * BUFFER_LINES)

// Define maximum transfer size for ESP32-C6
#define MAX_SPI_TRANSFER_SIZE BUFFER_LENGTH*LV_COLOR_DEPTH/8

// Function prototypes
void display_init(void);
void display_start_task(void);
void display_create_ui(void);

// UI update functions
void display_update_password_dots(int password_length);
void display_show_status(const char* message, lv_color_t color);
void display_show_datetime(const char* datetime_str);
void display_show_last_upload(const char* str);
void display_show_last_boot(const char* str);
void display_show_temperature(float temperature);
void display_show_humidity(float humdity);
void display_show_ip(const char *ip);
void display_show_levels(float level_0, float level_1);
void display_show_login_result(const char* identifier, bool success);

#define DISPLAY_COLOR_WHITE lv_color_white()
#define DISPLAY_COLOR_GREEN lv_palette_main(LV_PALETTE_GREEN)
#define DISPLAY_COLOR_RED lv_palette_main(LV_PALETTE_RED)
#define DISPLAY_COLOR_BLUE lv_palette_main(LV_PALETTE_BLUE)
#define DISPLAY_COLOR_ORANGE lv_palette_main(LV_PALETTE_ORANGE)
#define DISPLAY_COLOR_CYAN lv_palette_main(LV_PALETTE_CYAN)
#define DISPLAY_COLOR_YELLOW lv_palette_main(LV_PALETTE_YELLOW)
#define DISPLAY_COLOR_BLACK lv_color_black()

#endif // DISPLAY_H