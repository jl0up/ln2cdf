#include "keypad.h"
#include "esp_log.h"
#include <string.h>
// #include "esp_task_wdt.h"

static const char *TAG = "KEYPAD";

// Semaphore for keypad thread safety (exposed to other modules via keypad.h)
SemaphoreHandle_t keypad_mutex = NULL;


// Keypad matrix
static const char keypad_map[ROWS][COLS] = {
    {'1', '2', '3'},
    {'4', '5', '6'},
    {'7', '8', '9'},
    {'*', '0', '#'}
};

// GPIO arrays for easier iteration
static const gpio_num_t row_gpios[ROWS] = {ROW_1_GPIO, ROW_2_GPIO, ROW_3_GPIO, ROW_4_GPIO};
static const gpio_num_t col_gpios[COLS] = {COL_1_GPIO, COL_2_GPIO, COL_3_GPIO};

// Debouncing variables
static char last_key = 0;
static uint32_t last_key_time = 0;
static const uint32_t DEBOUNCE_TIME_MS = 50;

// Password lookup table
static const password_entry_t password_table[] = {
    {DEFAULT_PASSWORD, DEFAULT_IDENTIFIER},
    {"1064", "Admin"},
    {"1110", "SB"},
    {"2022", "PQ"},
    {"3303", "CPB"},
    {"0444", "CSE"},
    {"5050", "LAM"},
    {"0606", "UAR1"},
    {"7007", "UAR2"},    
    {"8800", "A&B"},    
    {"0990", "LKB"},    
    {"1111", "Guest"}
};
// Length of the password table
static const int password_table_size = sizeof(password_table) / sizeof(password_entry_t);

// Password state
static char current_password[MAX_PASSWORD_LENGTH + 1] = "";
static int password_index = 0;
static TimerHandle_t password_timer;

// Queue for key events
static QueueHandle_t key_queue;

// Callback for password processing
static password_callback_t password_callback_func;// = NULL;

// Static function prototypes
static void configure_keypad_gpio(void);
static char scan_keypad(void);
static void keypad_task(void *arg);
static void key_handler_task(void *arg);
static void handle_password_digit(char digit);
static void handle_shortcut_key(void);
static void process_password(const char* password);
static void password_timeout_callback(TimerHandle_t xTimer);

// Global variable to track current user
char last_identifier[MAX_IDENTIFIER_LENGTH + 1] = DEFAULT_IDENTIFIER;

// Creates queue, timer, and configures GPIO pins for keypad
void keypad_init(void)
{

    // Create RECURSIVE mutex for keypad - recursive because key_handler_task holds it
    // while calling keypad_callback which calls display_show_login_result which also takes the mutex
    keypad_mutex = xSemaphoreCreateRecursiveMutex();
    if (keypad_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create keypad recursive mutex");
        return;
    }

    // Create queue for key events
    key_queue = xQueueCreate(10, sizeof(key_event_t));
    if (key_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create key queue");
    } else {
        // Create password timeout timer
        password_timer = xTimerCreate("password_timer",
                                    pdMS_TO_TICKS(PASSWORD_TIMEOUT_MS),
                                    pdFALSE,  // One-shot timer
                                    NULL,
                                    password_timeout_callback);
        
        if (password_timer == NULL) {
            ESP_LOGE(TAG, "Failed to create password timer");
        } else {
            // Configure GPIO pins
            configure_keypad_gpio();
            ESP_LOGI(TAG, "Keypad initialized successfully");
        }
    }
}

void keypad_start_tasks(void)
{
    // Increased stack sizes due to nested callback chain that includes LVGL operations
    // key_handler_task holds keypad_mutex while calling keypad_callback which calls display_show_login_result
    // which performs LVGL operations - LVGL is known to use significant stack
    // Even 8KB was causing stack overflow with LVGL. Now using 12KB + 16KB.
    xTaskCreate(keypad_task, "keypad_task", 8192, NULL, 5, NULL);
    xTaskCreate(key_handler_task, "key_handler_task", 16384, NULL, 4, NULL);
    ESP_LOGI(TAG, "Keypad tasks started with increased stack sizes (8KB + 16KB)");
}

const char* keypad_lookup_identifier(const char* password)
{
    for (int i = 0; i < password_table_size; i++) {
        if (strcmp(password, password_table[i].password) == 0) {
            return password_table[i].identifier;
        }
    }
    return INVALID_IDENTIFIER;
}

const char* keypad_get_default_identifier(void)
{
    return password_table[0].identifier;
}

void keypad_set_password_callback(password_callback_t callback)
{
    password_callback_func = callback;
}

static void configure_keypad_gpio(void)
{
    gpio_config_t io_conf = {};
    
    // Configure row pins as output with pull-up
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = 0;
    for (int i = 0; i < ROWS; i++) {
        io_conf.pin_bit_mask |= (1ULL << row_gpios[i]);
    }
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&io_conf);
    
    // Set all rows HIGH initially
    for (int i = 0; i < ROWS; i++) {
        gpio_set_level(row_gpios[i], 1);
    }
    
    // Configure column pins as input with pull-up
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = 0;
    for (int i = 0; i < COLS; i++) {
        io_conf.pin_bit_mask |= (1ULL << col_gpios[i]);
    }
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&io_conf);
}

static char scan_keypad(void)
{
    for (int row = 0; row < ROWS; row++) {
        // Set current row LOW, others HIGH
        for (int i = 0; i < ROWS; i++) {
            gpio_set_level(row_gpios[i], (i == row) ? 0 : 1);
        }
        
        // Small delay to let the signal stabilize
        vTaskDelay(pdMS_TO_TICKS(1));
        
        // Check each column
        for (int col = 0; col < COLS; col++) {
            if (gpio_get_level(col_gpios[col]) == 0) {
                // Key pressed at this position
                
                // Set all rows back to HIGH
                for (int i = 0; i < ROWS; i++) {
                    gpio_set_level(row_gpios[i], 1);
                }
                
                return keypad_map[row][col];
            }
        }
    }
    
    // Set all rows back to HIGH
    for (int i = 0; i < ROWS; i++) {
        gpio_set_level(row_gpios[i], 1);
    }
    
    return 0; // No key pressed
}

static void keypad_task(void *arg)
{
    // esp_task_wdt_add(NULL); // watch this task with Task Watchdog Timer

    char current_key;
    key_event_t key_event;
    uint32_t current_time;
    
    ESP_LOGI(TAG, "Keypad task started");
    
    while (1) {
        current_key = scan_keypad();
        current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
        
        // Debouncing logic
        if (current_key != last_key) {
            if (current_time - last_key_time > DEBOUNCE_TIME_MS) {
                if (current_key != 0) {
                    // New key pressed
                    key_event.key = current_key;
                    key_event.pressed = true;
                    xQueueSend(key_queue, &key_event, 0);
                    ESP_LOGD(TAG, "Key pressed: %c", current_key);
                }
                
                last_key = current_key;
                last_key_time = current_time;
            }
        } else {
            // Same key state, update time
            last_key_time = current_time;
        }
        
        // Scan every 10ms
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

static void key_handler_task(void *arg)
{
    // esp_task_wdt_add(NULL); // watch this task with Task Watchdog Timer
    
    key_event_t key_event;
    
    ESP_LOGI(TAG, "Key handler task started");
    
    while (1) {
        if (xQueueReceive(key_queue, &key_event, portMAX_DELAY)) {
            if (key_event.pressed) {
                ESP_LOGD(TAG, "Processing key press: %c", key_event.key);
                
                if (xSemaphoreTakeRecursive(keypad_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {

                    // Handle password input
                    if (key_event.key >= '0' && key_event.key <= '9') {
                        // Numeric key - add to password
                        handle_password_digit(key_event.key);
                    } else if (key_event.key == '#') {
                        // Shortcut key - process default password
                        handle_shortcut_key();
                    } else if (key_event.key == '*') {
                        // Star key - reset/cancel
                        ESP_LOGI(TAG, "Cancel key pressed - resetting password");
                        xTimerStop(password_timer, 0);
                        memset(current_password, 0, sizeof(current_password));
                        password_index = 0;
                        
                        if (password_callback_func) {
                            password_callback_func("", "Cancelled - Ready", false);
                        }
                    } else {
                        ESP_LOGW(TAG, "Invalid key for password input: %c", key_event.key);
                    }

                    xSemaphoreGiveRecursive(keypad_mutex);
                }
            }
        }
    }
}

// Function to handle password input
static void handle_password_digit(char digit)
{
    if (password_index < MAX_PASSWORD_LENGTH) {
        current_password[password_index++] = digit;
        
        if (password_callback_func) {
            password_callback_func("", "Entering password...", false);
        }
        
        ESP_LOGD(TAG, "Password progress: %d/4 digits entered", password_index);
        
        // Stop the timer
        xTimerStop(password_timer, 0);
        
        if (password_index == MAX_PASSWORD_LENGTH) {
            // Password complete
            current_password[MAX_PASSWORD_LENGTH] = '\0';
            process_password(current_password);
            
            // Reset password state
            memset(current_password, 0, sizeof(current_password));
            password_index = 0;
        } else {
            // Start/restart the timer for timeout
            xTimerReset(password_timer, 0);
        }
    }
}

// Function to handle shortcut key (#)
static void handle_shortcut_key(void)
{
    ESP_LOGI(TAG, "Shortcut key pressed - Logging out");
    
    // Stop the timer
    xTimerStop(password_timer, 0);
    
    // Process default password
    process_password(DEFAULT_PASSWORD);
    
    // Reset password state
    memset(current_password, 0, sizeof(current_password));
    password_index = 0;
}

// Function called when password is complete or timeout occurs
static void process_password(const char* password)
{
    const char* identifier = keypad_lookup_identifier(password);
    bool success = strcmp(identifier, INVALID_IDENTIFIER) != 0;
    
    ESP_LOGI(TAG, "Password entered: %s", password);
    ESP_LOGI(TAG, "Identifier: %s", identifier);
    
    if (password_callback_func) {
        password_callback_func(password, identifier, success);
    }
}

// Timer callback function for password timeout
static void password_timeout_callback(TimerHandle_t xTimer)
{
    ESP_LOGW(TAG, "Password timeout");
    
    if (xSemaphoreTakeRecursive(keypad_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        
        if (password_callback_func) {
            password_callback_func("", "TIMEOUT", false);
        }
    
        // Reset password state
        memset(current_password, 0, sizeof(current_password));
        password_index = 0;

        xSemaphoreGiveRecursive(keypad_mutex);
    }
}