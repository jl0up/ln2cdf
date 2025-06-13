#include "display.h"
#include "keypad.h"
#include "esp_log.h"
#include "esp_heap_caps.h"    // For DMA-capable memory allocation
#include "esp_memory_utils.h" // For esp_ptr_dma_capable()
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "DISPLAY";

// SPI device handle
static spi_device_handle_t spi_device;

// LVGL display buffer
static lv_display_t *display;
static void *buf1; // Will be allocated in DMA memory
static void *buf2; // Will be allocated in DMA memory

// LVGL UI objects
static lv_obj_t *screen_main;
static lv_obj_t *label_datetime;
static lv_obj_t *label_level_0;
static lv_obj_t *label_level_1;
static lv_obj_t *label_temperature;
static lv_obj_t *label_humidity;
static lv_obj_t *label_last_upload;
static lv_obj_t *label_last_boot;
static lv_obj_t *label_password;
static lv_obj_t *label_status;
static lv_obj_t *label_dots;

// Static function prototypes
static void lcd_send_cmd(uint8_t cmd);
static void lcd_send_data(const uint8_t *data, int len);
static void lcd_init(void);
static void lcd_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map);
static void lcd_dma_buffer_allocate(size_t buffer_size);
static void lcd_lvgl_task(void *arg);
static void init_spi_display(void);
static void init_lvgl(void);


void display_init(void)
{
    // Initialize SPI and display hardware
    init_spi_display();

    // Initialize LVGL
    init_lvgl();

    ESP_LOGI(TAG, "Display initialized successfully");
}

void display_start_task(void)
{
    xTaskCreate(lcd_lvgl_task, "lvgl_task", 4096, NULL, 6, NULL);
    ESP_LOGI(TAG, "Display task started");
}

void display_create_ui(void)
{
    // Create main screen
    screen_main = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen_main, lv_color_black(), 0);
    lv_screen_load(screen_main);

    // Label for date/time
    label_datetime = lv_label_create(screen_main);
    lv_label_set_text(label_datetime, "");
    lv_obj_set_style_text_color(label_datetime, lv_palette_main(LV_PALETTE_GREY), 0);
    lv_obj_set_style_text_font(label_datetime, &lv_font_montserrat_16, 0);
    lv_obj_align(label_datetime, LV_ALIGN_TOP_MID, 0, 5);

    // Labels for levels
    label_level_0 = lv_label_create(screen_main);
    lv_label_set_text(label_level_0, "Tank 0");
    lv_obj_set_style_text_color(label_level_0, lv_color_white(), 0);
    lv_obj_set_style_text_font(label_level_0, &lv_font_montserrat_46, 0);
    lv_obj_align(label_level_0, LV_ALIGN_TOP_MID, 0, 25);

    label_level_1 = lv_label_create(screen_main);
    lv_label_set_text(label_level_1, "Tank 1");
    lv_obj_set_style_text_color(label_level_1, lv_color_white(), 0);
    lv_obj_set_style_text_font(label_level_1, &lv_font_montserrat_46, 0);
    lv_obj_align(label_level_1, LV_ALIGN_TOP_MID, 0, 65);

    // label for temperature
    label_temperature = lv_label_create(screen_main);
    lv_label_set_text(label_temperature, "Temp °C");
    lv_obj_set_style_text_color(label_temperature, lv_palette_main(LV_PALETTE_CYAN), 0);
    lv_obj_set_style_text_font(label_temperature, &lv_font_montserrat_36, 0);
    lv_obj_align(label_temperature, LV_ALIGN_TOP_MID, 0, 110);

    // label for humidity
    label_humidity = lv_label_create(screen_main);
    lv_label_set_text(label_humidity, "Hum%");
    lv_obj_set_style_text_color(label_humidity, lv_palette_main(LV_PALETTE_TEAL), 0);
    lv_obj_set_style_text_font(label_humidity, &lv_font_montserrat_36, 0);
    lv_obj_align(label_humidity, LV_ALIGN_TOP_MID, 0, 140);

    // Password instruction label
    label_password = lv_label_create(screen_main);
    lv_label_set_text(label_password, "Enter password:");
    lv_obj_set_style_text_color(label_password, lv_color_white(), 0);
    lv_obj_set_style_text_font(label_password, &lv_font_montserrat_20, 0);
    lv_obj_align(label_password, LV_ALIGN_CENTER, 0, 45);

    // Password dots display
    label_dots = lv_label_create(screen_main);
    lv_label_set_text(label_dots, "____");
    lv_obj_set_style_text_color(label_dots, lv_palette_main(LV_PALETTE_YELLOW), 0);
    lv_obj_set_style_text_font(label_dots, &lv_font_montserrat_24, 0);
    lv_obj_align(label_dots, LV_ALIGN_CENTER, 0, 70);

    // Status label
    label_status = lv_label_create(screen_main);
    lv_label_set_text(label_status, keypad_get_default_identifier());
    lv_obj_set_style_text_color(label_status, lv_palette_main(LV_PALETTE_ORANGE), 0);
    lv_obj_set_style_text_font(label_status, &lv_font_montserrat_24, 0);
    lv_obj_align(label_status, LV_ALIGN_CENTER, 0, 100);

    // Instructions
    lv_obj_t *label_inst = lv_label_create(screen_main);
    lv_label_set_text(label_inst, "#: log out, *: cancel");
    lv_obj_set_style_text_color(label_inst, lv_palette_main(LV_PALETTE_GREY), 0);
    lv_obj_set_style_text_font(label_inst, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(label_inst, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(label_inst, LV_ALIGN_BOTTOM_MID, 0, -30);

    // Last upload info
    label_last_upload = lv_label_create(screen_main);
    lv_label_set_text(label_last_upload, "Last upload: unknown");
    lv_obj_set_style_text_color(label_last_upload, lv_palette_main(LV_PALETTE_GREY), 0);
    lv_obj_set_style_text_font(label_last_upload, &lv_font_montserrat_14, 0);
    lv_obj_align(label_last_upload, LV_ALIGN_BOTTOM_MID, 0, -15);

    // Last boot info
    label_last_boot = lv_label_create(screen_main);
    lv_label_set_text(label_last_boot, "Last boot: unknown");
    lv_obj_set_style_text_color(label_last_boot, lv_palette_main(LV_PALETTE_GREY), 0);
    lv_obj_set_style_text_font(label_last_boot, &lv_font_montserrat_14, 0);
    lv_obj_align(label_last_boot, LV_ALIGN_BOTTOM_MID, 0, 0);

    ESP_LOGI(TAG, "UI created successfully");
}

void display_update_password_dots(int password_length)
{
    char dots[MAX_PASSWORD_LENGTH + 1] = {0};

    for (int i = 0; i < MAX_PASSWORD_LENGTH; i++)
    {
        if (i < password_length)
        {
            dots[i] = '*';
        }
        else
        {
            dots[i] = '_';
        }
    }

    lv_label_set_text(label_dots, dots);
}

void display_show_status(const char *message, lv_color_t color)
{
    lv_obj_set_style_text_color(label_status, color, 0);
    lv_label_set_text(label_status, message);
}

void display_show_datetime(const char* datetime)
{
    lv_label_set_text(label_datetime, datetime);
}

void display_show_last_upload(const char* str)
{
    lv_label_set_text(label_last_upload, str);
}

void display_show_last_boot(const char* str)
{
    lv_label_set_text(label_last_boot, str);
}

void display_show_temperature(float temperature)
{
    char msg[64];
    snprintf(msg, sizeof(msg), "%.1f°C", temperature);
    lv_label_set_text(label_temperature, msg);
}

void display_show_humidity(float humidity)
{
    char msg[64];
    snprintf(msg, sizeof(msg), "%.1f%%", humidity);
    lv_label_set_text(label_humidity, msg);
}

void display_show_levels(float level_0, float level_1)
{
    char msg[64];
    lv_color_t color;

    if (level_0 < 0.) { color = lv_palette_main(LV_PALETTE_RED); }
    else
    if (level_0 < 20.) { color = lv_palette_main(LV_PALETTE_ORANGE); }
    else
    if (level_0 < 80.) { color = lv_palette_main(LV_PALETTE_YELLOW); }
    else
    if (level_0 <= 100.) { color = lv_palette_main(LV_PALETTE_GREEN); }
    else { color = lv_palette_main(LV_PALETTE_BLUE); }
    lv_obj_set_style_text_color(label_level_0, color, 0);
    snprintf(msg, sizeof(msg), "%5.1f%%", level_0);
    lv_label_set_text(label_level_0, msg);

    if (level_1 < 0.) { color = lv_palette_main(LV_PALETTE_RED); }
    else
    if (level_1 < 20.) { color = lv_palette_main(LV_PALETTE_ORANGE); }
    else
    if (level_1 < 80.) { color = lv_palette_main(LV_PALETTE_YELLOW); }
    else
    if (level_1 <= 100.) { color = lv_palette_main(LV_PALETTE_GREEN); }
    else { color = lv_palette_main(LV_PALETTE_BLUE); }
    lv_obj_set_style_text_color(label_level_1, color, 0);
    snprintf(msg, sizeof(msg), "%5.1f%%", level_1);
    lv_label_set_text(label_level_1, msg);
}

void display_show_login_result(const char *identifier, bool success)
{

    if (strcmp(identifier, keypad_get_default_identifier()) == 0)
    {
        lv_obj_set_style_text_color(label_status, lv_palette_main(LV_PALETTE_BLUE), 0); // Blue
        lv_label_set_text(label_status, "LOGGING OUT");
        strcpy(last_identifier, identifier);
    }
    else 
    {
        if (strcmp(identifier, INVALID_IDENTIFIER) == 0)
        {
            lv_obj_set_style_text_color(label_status, lv_palette_main(LV_PALETTE_RED), 0); // Red
            lv_label_set_text(label_status, "BAD PASSWD");
        }
        else
        {
            lv_obj_set_style_text_color(label_status, lv_palette_main(LV_PALETTE_GREEN), 0); // Green
            lv_label_set_text(label_status, "WELCOME");
            strcpy(last_identifier, identifier);
        }
    }

    // Reset display after 1 seconds
    vTaskDelay(pdMS_TO_TICKS(1000));
    lv_obj_set_style_text_color(label_status, lv_palette_main(LV_PALETTE_ORANGE), 0);
    lv_label_set_text(label_status, last_identifier);
    display_update_password_dots(0);
}






static void lcd_send_cmd(uint8_t cmd)
{
    esp_err_t ret;
    spi_transaction_t t;
    memset(&t, 0, sizeof(t));
    t.length = 8;
    t.flags = SPI_TRANS_USE_TXDATA;
    t.tx_data[0] = cmd;

    gpio_set_level(LCD_DC_GPIO, 0); // Command mode
    ret = spi_device_polling_transmit(spi_device, &t);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "lcd_send_cmd(): SPI transmit failed: %s", esp_err_to_name(ret));
        return; // Exit on error
    }
}

static void lcd_send_data(const uint8_t *data, int len)
{
    esp_err_t ret;
    if (len == 0 || data == NULL)
        return; // Added null pointer check

    // Set DC pin once before all transfers
    gpio_set_level(LCD_DC_GPIO, 1); // Data mode

    int remaining = len;
    int offset = 0;

    while (remaining > 0)
    {
        spi_transaction_t t;
        memset(&t, 0, sizeof(t));

        // Calculate chunk size for this iteration
        int chunk_size = (remaining > MAX_SPI_TRANSFER_SIZE) ? MAX_SPI_TRANSFER_SIZE : remaining;
        t.length = chunk_size * 8;

        if (chunk_size <= 4)
        {
            // Small transfers can use tx_data (no DMA)
            t.flags = SPI_TRANS_USE_TXDATA;
            memcpy(t.tx_data, data + offset, chunk_size);
        }
        else
        {
            // Large transfers use DMA buffer
            t.tx_buffer = data + offset;
        }

        ret = spi_device_polling_transmit(spi_device, &t);
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "lcd_send_data(): SPI transmit failed: %s", esp_err_to_name(ret));
            return; // Exit on error
        }

        offset += chunk_size;
        remaining -= chunk_size;
        if (remaining > 0) {
            ESP_LOGD(TAG, "Not all data sent. Remaining bytes: %d", remaining);
            return; // Exit on error
        }
    }
}

static void lcd_init(void)
{
    // Reset the display
    gpio_set_level(LCD_RST_GPIO, 0);
    vTaskDelay(pdMS_TO_TICKS(100));
    gpio_set_level(LCD_RST_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(150));

    // ST7789 initialization sequence
    lcd_send_cmd(0x01); // Software reset
    vTaskDelay(pdMS_TO_TICKS(150));

    lcd_send_cmd(0x11); // Sleep out
    vTaskDelay(pdMS_TO_TICKS(500));

    // Color mode - CRITICAL for proper font rendering
    lcd_send_cmd(0x3A);          // Pixel format
    uint8_t pixel_format = 0x55; // 16-bit color (RGB565)
    lcd_send_data(&pixel_format, 1);

    // Memory access control - affects color byte order
    lcd_send_cmd(0x36);
    uint8_t madctl = // BIT 0 IS ALWAYS 0
        BIT(1)       // bit D1 is always 1
        // | BIT(2)    // bit D2 is refresh right-to-left
        // | BIT(3)    // bit D3 is BGR instead of RGB
        // | BIT(4)    // bit D4 is refresh bottom-to-top
        // | BIT(5)    // bit D5 is row/col inverse mode
        // | BIT(6)    // bit D6 is right-to-left
        // | BIT(7)    // bit D7 is bottom-to-top
        ;
    ESP_LOGD(TAG, "ST7789 MADCTL: %d", madctl);

    lcd_send_data(&madctl, 1);

    /*********************************************************
     *  Voodoo ST7789 settings from docs and online examples *
     *********************************************************/

    // Porch settings for better stability
    lcd_send_cmd(0xB2); // Porch control
    uint8_t porch_data[] = {0x0C, 0x0C, 0x00, 0x33, 0x33};
    lcd_send_data(porch_data, 5);

    // Gate control
    lcd_send_cmd(0xB7);
    uint8_t gate_ctrl = 0x35;
    lcd_send_data(&gate_ctrl, 1);

    // VCOM setting
    lcd_send_cmd(0xBB);
    uint8_t vcom = 0x19;
    lcd_send_data(&vcom, 1);

    // LCM control
    lcd_send_cmd(0xC0);
    uint8_t lcm_ctrl = 0x2C;
    lcd_send_data(&lcm_ctrl, 1);

    // VDV and VRH enable
    lcd_send_cmd(0xC2);
    uint8_t vdv_vrh = 0x01;
    lcd_send_data(&vdv_vrh, 1);

    // VRH set
    lcd_send_cmd(0xC3);
    uint8_t vrh = 0x12;
    lcd_send_data(&vrh, 1);

    // VDV set
    lcd_send_cmd(0xC4);
    uint8_t vdv = 0x20;
    lcd_send_data(&vdv, 1);

    // // Frame rate control
    // lcd_send_cmd(0xC6);
    // uint8_t frame_rate = 0x0F;  // 60Hz
    // lcd_send_data(&frame_rate, 1);

    // Power control 1
    lcd_send_cmd(0xD0);
    uint8_t power_ctrl[] = {0xA4, 0xA1};
    lcd_send_data(power_ctrl, 2);

    // Positive voltage gamma
    lcd_send_cmd(0xE0);
    uint8_t gamma_pos[] = {0xD0, 0x04, 0x0D, 0x11, 0x13, 0x2B, 0x3F, 0x54,
                           0x4C, 0x18, 0x0D, 0x0B, 0x1F, 0x23};
    lcd_send_data(gamma_pos, 14);

    // Negative voltage gamma
    lcd_send_cmd(0xE1);
    uint8_t gamma_neg[] = {0xD0, 0x04, 0x0C, 0x11, 0x13, 0x2C, 0x3F, 0x44,
                           0x51, 0x2F, 0x1F, 0x1F, 0x20, 0x23};
    lcd_send_data(gamma_neg, 14);

    /* *******************************************************/

    // Set the active area to display size with offset
    lcd_send_cmd(0x2A); // Column address set
    uint8_t col_data[] = {
        (COLUMN_OFFSET) >> 8,
        (COLUMN_OFFSET) & 0xFF,
        (COLUMN_OFFSET + LCD_WIDTH - 1) >> 8,
        (COLUMN_OFFSET + LCD_WIDTH - 1) & 0xFF}; // Offset to (offset + width - 1)
    lcd_send_data(col_data, 4);

    lcd_send_cmd(0x2B); // Row address set
    uint8_t row_data[] = {
        (ROW_OFFSET) >> 8,
        (ROW_OFFSET) & 0xFF,
        (ROW_OFFSET + LCD_HEIGHT - 1) >> 8,
        (ROW_OFFSET + LCD_HEIGHT - 1) & 0xFF}; // 0 to (height - 1)
    lcd_send_data(row_data, 4);

    lcd_send_cmd(0x21); // Inversion on
    lcd_send_cmd(0x13); // Normal display on
    lcd_send_cmd(0x29); // Display on

    // Small delay before enabling backlight
    vTaskDelay(pdMS_TO_TICKS(100));
    gpio_set_level(LCD_BL_GPIO, 1);

    ESP_LOGI(TAG, "ST7789 initialized: Physical %dx%d, Offset (%d,%d)",
             LCD_WIDTH, LCD_HEIGHT, COLUMN_OFFSET, ROW_OFFSET);
}

static void lcd_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    int32_t x1 = area->x1;
    int32_t x2 = area->x2;
    int32_t y1 = area->y1;
    int32_t y2 = area->y2;

    // Validate coordinates
    if (x1 < 0 || y1 < 0 || x2 >= LCD_WIDTH || y2 >= LCD_HEIGHT)
    {
        ESP_LOGE(TAG, "Invalid coordinates: (%ld,%ld) to (%ld,%ld)", x1, y1, x2, y2);
        lv_display_flush_ready(disp);
        return;
    }

    uint32_t pixel_count = (x2 - x1 + 1) * (y2 - y1 + 1);
    uint32_t size = pixel_count * lv_color_format_get_size(lv_display_get_color_format(disp));

    // DEBUG: Check if we're getting the expected buffer
    ESP_LOGD(TAG, "Flush area (%ld,%ld)-(%ld,%ld), %ld pixels, px_map=%p",
             x1, y1, x2, y2, pixel_count, px_map);

    // Apply ST7789 offsets
    int32_t st7789_x1 = x1 + COLUMN_OFFSET;
    int32_t st7789_x2 = x2 + COLUMN_OFFSET;
    int32_t st7789_y1 = y1 + ROW_OFFSET;
    int32_t st7789_y2 = y2 + ROW_OFFSET;

    // Set column address
    lcd_send_cmd(0x2A);
    uint8_t col_data[] = {
        st7789_x1 >> 8, st7789_x1 & 0xFF,
        st7789_x2 >> 8, st7789_x2 & 0xFF};
    lcd_send_data(col_data, 4);

    // Set row address
    lcd_send_cmd(0x2B);
    uint8_t row_data[] = {
        st7789_y1 >> 8, st7789_y1 & 0xFF,
        st7789_y2 >> 8, st7789_y2 & 0xFF};
    lcd_send_data(row_data, 4);

    // Write to RAM
    lcd_send_cmd(0x2C);
    lv_draw_sw_rgb565_swap(px_map, size);
    lcd_send_data(px_map, size);

    lv_display_flush_ready(disp);
}

static void lcd_dma_buffer_allocate(size_t buffer_size)
{
    ESP_LOGD(TAG, "lv_color_format_get_size=%zu", buffer_size / BUFFER_LENGTH);

    // Free existing buffers if any
    if (buf1)
    {
        heap_caps_free(buf1);
        buf1 = NULL;
    }
    if (buf2)
    {
        heap_caps_free(buf2);
        buf2 = NULL;
    }

    // Allocation method - 32-byte aligned, DMA capable
    buf1 = heap_caps_aligned_alloc(32, buffer_size, MALLOC_CAP_DMA | MALLOC_CAP_32BIT);
    buf2 = heap_caps_aligned_alloc(32, buffer_size, MALLOC_CAP_DMA | MALLOC_CAP_32BIT);

    if (!buf1 || !buf2)
    {
        ESP_LOGE(TAG, "Failed to allocate DMA buffers");
        if (buf1)
            heap_caps_free(buf1);
        if (buf2)
            heap_caps_free(buf2);
    }
    else
    {
        ESP_LOGI(TAG, "Allocated DMA buffers: buf1=%p, buf2=%p, size=%zu",
                 buf1, buf2, buffer_size);
        // VERIFY DMA CAPABILITY
        ESP_LOGD(TAG, "buf1 DMA capable: %s", esp_ptr_dma_capable(buf1) ? "YES" : "NO");
        ESP_LOGD(TAG, "buf2 DMA capable: %s", esp_ptr_dma_capable(buf2) ? "YES" : "NO");
    }
}

static void lcd_lvgl_task(void *arg)
{
    ESP_LOGI(TAG, "LVGL task started");

    while (1)
    {
        lv_tick_inc(10);
        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

static void init_spi_display(void)
{
    esp_err_t ret;

    // Configure SPI bus with DMA
    spi_bus_config_t buscfg = {
        .miso_io_num = -1,
        .mosi_io_num = LCD_MOSI_GPIO,
        .sclk_io_num = LCD_SCLK_GPIO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = MAX_SPI_TRANSFER_SIZE,
        .flags = 0 // SPICOMMON_BUSFLAG_MASTER | SPICOMMON_BUSFLAG_GPIO_PINS
    };

    // Configure SPI device
    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = LCD_SPI_CLOCK_HZ,
        .mode = 3,
        .spics_io_num = LCD_CS_GPIO,
        .queue_size = 7,
        .flags = SPI_DEVICE_NO_DUMMY,
        .pre_cb = NULL,
        .post_cb = NULL,
    };

    // Initialize SPI bus with DMA
    ret = spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);
    ESP_ERROR_CHECK(ret);

    // Add device to SPI bus
    ret = spi_bus_add_device(SPI2_HOST, &devcfg, &spi_device);
    ESP_ERROR_CHECK(ret);

    // Configure GPIO pins
    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = (1ULL << LCD_DC_GPIO) | (1ULL << LCD_RST_GPIO) | (1ULL << LCD_BL_GPIO);
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_config(&io_conf);

    // Initialize display
    lcd_init();
}

static void init_lvgl(void)
{
    lv_init();

    // Create display
    display = lv_display_create(LCD_WIDTH, LCD_HEIGHT);

    // // Change display orientation (<---- doesn't work ?)
    // lv_display_set_rotation(display, LV_DISPLAY_ROTATION_180);

    // Associate flush callback
    lv_display_set_flush_cb(display, lcd_flush);

    // Set color format explicitly: ST7789 accepts 16 bit per pixel following RGB565 standard
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);

    // Set render mode
    lv_display_set_render_mode(display, LV_DISPLAY_RENDER_MODE_PARTIAL);

    // Allocate DMA buffers
    size_t buffer_size = BUFFER_LENGTH * lv_color_format_get_size(lv_display_get_color_format(display));
    lcd_dma_buffer_allocate(buffer_size);

    // Associate buffers in LVGL
    lv_display_set_buffers(display, buf1, buf2, BUFFER_LENGTH, LV_DISPLAY_RENDER_MODE_PARTIAL);

    ESP_LOGI(TAG, "LVGL initialized with SINGLE DMA buffer (testing mode)");
}
