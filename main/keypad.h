#ifndef KEYPAD_H
#define KEYPAD_H

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/timers.h"
#include "driver/gpio.h"

// Keypad configuration
#define ROWS 4
#define COLS 3
#define MAX_PASSWORD_LENGTH 4
#define MAX_IDENTIFIER_LENGTH 32
#define PASSWORD_TIMEOUT_MS 1000

// GPIO pin definitions for ESP32-C6 - Keypad
#define ROW_1_GPIO    GPIO_NUM_15
#define ROW_2_GPIO    GPIO_NUM_23
#define ROW_3_GPIO    GPIO_NUM_22
#define ROW_4_GPIO    GPIO_NUM_21
#define COL_1_GPIO    GPIO_NUM_20
#define COL_2_GPIO    GPIO_NUM_19
#define COL_3_GPIO    GPIO_NUM_18

// Key event structure
typedef struct {
    char key;
    bool pressed;  // true for press, false for release
} key_event_t;

// Password lookup table structure
typedef struct {
    char password[MAX_PASSWORD_LENGTH + 1];  // 4 digits + null terminator
    char identifier[MAX_IDENTIFIER_LENGTH + 1];
} password_entry_t;

// Particular identifiers in the lookup table
#define INVALID_IDENTIFIER "INVALID" // identifier returned when password not found
#define DEFAULT_PASSWORD "0000" // password of defaut identifier
#define DEFAULT_IDENTIFIER "None" // default identifier for logging out ("0000" or "#")

// Global variable to track current user (initialized in keypad.c)
extern char last_identifier[MAX_IDENTIFIER_LENGTH + 1];

// Public functions prototypes
void keypad_init(void);
void keypad_start_tasks(void);
const char* keypad_lookup_identifier(const char* password);
const char* keypad_get_default_identifier(void);
// Callback function type for password processing
typedef void (*password_callback_t)(const char* password, const char* identifier, bool is_valid);
void keypad_set_password_callback(password_callback_t callback);

#endif // KEYPAD_H