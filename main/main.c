/*
 * ln2cdf project
 *
 * by jl0up
 * 
 * https://github.com/jl0up/ln2cdf
 * 
 */

#include <stdlib.h>
#include "chip_info.h"
#include "delayed_restart.h"
#include "wifi_station.h"
#include "adc_oneshot.h"
#include "sntp_client.h"
#include "google_sheet.h"
#include "display.h"
#include "keypad.h"
// #include "onewire_bus.h"
// #include "ds18b20.h"
#include "ota_update.h"
#include "temp_humidity.h"
#include "esp_task_wdt.h"


#define WAIT_TIME_MS 0.7*1000
#define UPLOAD_THRESHOLD_PERCENT_0 1.5 // minimum percentage change on tank 0 to trigger upload unless identifier changed
#define UPLOAD_THRESHOLD_PERCENT_1 0.5 // minimum percentage change an tank 1 to trigger upload unless identifier changed
#define MAX_UPLOAD_INTERVAL_SECONDS 1*60*60 // maximum interval between uploads in seconds
#define MIN_UPLOAD_INTERVAL_SECONDS    2*60 // minimum interval between uploads in seconds
#define DELAY_BEFORE_LOGOUT_SECONDS 2*60*60 // seconds to wait before logging out
#define R_EFF 46.5 // 47 Ohm, installed, measured 46.5 Ohm
#define I_EMPTY 3.82 // theoretical empty current: 4 mA
#define I_FULL 20.16 // theoretical full current: 20 mA
// #define ERR_BUF_SIZE 256

const static char *TAG = "ln2cdf: main.c";


int compare(const void *a, const void *b) {
    if(*(int*)a > *(int*)b)
        return 1;
    else
        return -1;
}

float average_voltage(int* mem, int n){
    int avg = 0;
    int k = n/5;
    for (int i = k ; i < (n - k) ; i++ ) {
        // ESP_LOGD( "average_voltage()", "%d | %d | %d", n, k, i);
        avg += mem[i];
    }
    return (float)avg / (float)(n - k - k);
}

int average_adc_raw(int* mem, int n){
    int avg = 0;
    int k = n/5;
    for (int i = k ; i < (n - k) ; i++ ) {
        // ESP_LOGD( "average_adc_raw()", "%d | %d | %d", n, k, i);
        avg += mem[i];
    }
    return avg / (n - k - k);
}

// Global variable to track password length for display updates
static int current_password_length = 0;

// Global variable to track last successfully uploaded identifier
static char last_identifier_uploaded[MAX_IDENTIFIER_LENGTH + 1] = DEFAULT_IDENTIFIER;

// Global variable to track last login time
time_t datetime_last_login;

// Integrated callback function that handles all keypad events
void keypad_callback(const char* password, const char* message, bool success)
{
    ESP_LOGI(TAG, "Callback triggered - password: '%s', message: '%s', success: %d", 
             password, message, success);
    
    // Handle different callback scenarios based on the message content
    if (strcmp(message, "Entering password...") == 0) {
        // User is entering a password digit
        current_password_length++;
        display_update_password_dots(current_password_length);
        // display_show_status("Entering password...", DISPLAY_COLOR_CYAN);
        
    } else if (strcmp(message, "Cancelled - Ready") == 0) {
        // User pressed * to cancel
        current_password_length = 0;
        display_update_password_dots(0);
        // display_show_status("Cancelled - Ready", DISPLAY_COLOR_ORANGE);
        
    } else if (strcmp(message, "TIMEOUT") == 0) {
        // Password entry timed out
        current_password_length = 0;
        display_update_password_dots(0);
        // display_show_status("TIMEOUT - Try again", DISPLAY_COLOR_RED);
        
    } else {
        // Password was completed - show login result
        current_password_length = 0;
        display_show_login_result(message, success);
        if (strcmp(message, DEFAULT_IDENTIFIER) != 0) {
            datetime_last_login = time(NULL);
        }
    }
}


void app_main(void)
{
    // char err_buf[ERR_BUF_SIZE] = "";

    /*****************
     *** Chip info ***
     *****************/
    print_chip_information();


    /********************
     *** Wifi connect ***
     ********************/
    wifi_station_connect();


    /*************************
     *** get time via SNTP ***
     *************************/
    initialize_sntp();
    time_t datetime_last_upload;
    time_t datetime_current;
    char datetime_str[64];
    time_t datetime_boot;
    char datetime_boot_str[64];

    /**************************************
     *** Enable OTA update by http POST ***
     **************************************/
    start_ota_server();


    // /**********************************
    //  *** 1-wire temperature sensors ***
    //  **********************************/

    // #define EXAMPLE_ONEWIRE_BUS_GPIO    6
    // #define EXAMPLE_ONEWIRE_MAX_DS18B20 3

    // // install 1-wire bus
    // onewire_bus_handle_t bus = NULL;
    // onewire_bus_config_t bus_config = {
    //     .bus_gpio_num = EXAMPLE_ONEWIRE_BUS_GPIO,
    // };
    // onewire_bus_rmt_config_t rmt_config = {
    //     .max_rx_bytes = 10, // 1byte ROM command + 8byte ROM number + 1byte device command
    // };
    // ESP_ERROR_CHECK(onewire_new_bus_rmt(&bus_config, &rmt_config, &bus));

    // int ds18b20_device_num = 0;
    // ds18b20_device_handle_t ds18b20s[EXAMPLE_ONEWIRE_MAX_DS18B20];
    // onewire_device_iter_handle_t iter = NULL;
    // onewire_device_t next_onewire_device;
    // esp_err_t search_result = ESP_OK;

    // // create 1-wire device iterator, which is used for device search
    // ESP_ERROR_CHECK(onewire_new_device_iter(bus, &iter));
    // ESP_LOGI(TAG, "Device iterator created, start searching...");
    // do {
    //     search_result = onewire_device_iter_get_next(iter, &next_onewire_device);
    //     if (search_result == ESP_OK) { // found a new device, let's check if we can upgrade it to a DS18B20
    //         ds18b20_config_t ds_cfg = {};
    //         // check if the device is a DS18B20, if so, return the ds18b20 handle
    //         if (ds18b20_new_device(&next_onewire_device, &ds_cfg, &ds18b20s[ds18b20_device_num]) == ESP_OK) {
    //             ESP_LOGI(TAG, "Found a DS18B20[%d], address: %016llX", ds18b20_device_num, next_onewire_device.address);
    //             ds18b20_device_num++;
    //         } else {
    //             ESP_LOGI(TAG, "Found an unknown device, address: %016llX", next_onewire_device.address);
    //         }
    //     }
    // } while (search_result != ESP_ERR_NOT_FOUND);
    // ESP_ERROR_CHECK(onewire_del_device_iter(iter));
    // ESP_LOGI(TAG, "Searching done, %d DS18B20 device(s) found", ds18b20_device_num);


    /****************************************************************
     *** Init I2C bus and AHT20 (temperature and humidity sensor) ***
     ****************************************************************/
    // Initialize I2C bus
    esp_err_t ret = i2c_bus_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C bus initialization failed");
        return;
    }
    
    // Initialize AHT20 sensor
    ret = aht20_sensor_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "AHT20 sensor initialization failed");
        cleanup_resources();
        return;
    }

    float temperature = -99.99;
    float humidity = -99.99;









    // Initialize SPI and display
    display_init();
    
    // Create UI
    display_create_ui();

    // Initialize keypad
    keypad_init();

    keypad_set_password_callback(keypad_callback);

    // Create tasks
    display_start_task();
    keypad_start_tasks();




    /*************************
     *** upload parameters ***
     *************************/
    float level_0 = -100.;
    float level_1 = -100.;
    float level_last_logged_0 = -100.;
    float level_last_logged_1 = -100.;


    /****************
     *** ADC read ***
     ****************/
    // initialise ADC (setup + memory)
    // ADC 0 is GPIO0, ADC1 is GPIO1 (on ESP32-C6)
    adc_t adc;

    adc_oneshot_unit_init_cfg_t init_config1 = {
        .unit_id = ADC_UNIT_1,
    };
    adc.init_config1 = init_config1;

    #if EXAMPLE_USE_ADC2
    adc_oneshot_unit_init_cfg_t init_config2 = {
        .unit_id = ADC_UNIT_2,
    };
    adc.init_config2 = init_config2;
    #endif  //#if EXAMPLE_USE_ADC2

    adc_oneshot_init(&adc);

    // Show last boot time
    datetime_boot = time(NULL);
    strftime(datetime_boot_str, sizeof(datetime_boot_str), "Booted %d %b %H:%M:%S", localtime(&datetime_boot) );
    display_show_last_boot(datetime_boot_str);
    datetime_last_upload = datetime_boot - MAX_UPLOAD_INTERVAL_SECONDS; // force upload on first cycle
    datetime_last_login = datetime_boot;  // initialize last login time


    // Initialize Task Watchdog Timer (after all init, before main loop)
    // 30 second timeout, panic (reboot) on timeout
    esp_task_wdt_config_t wdt_config = {
        .timeout_ms = 30000,
        .idle_core_mask = 0,  // Don't watch idle tasks
        .trigger_panic = true,
    };
    esp_task_wdt_init(&wdt_config);
    esp_task_wdt_add(NULL);  // Add current task (app_main task)

    // Reading ADC in loop
    unsigned int idx = 0;
    while (1) {
        idx++;

        // Feed the watchdog at the start of each loop iteration
        esp_task_wdt_reset();

        adc_oneshot_get(&adc);

        // *err_buf = '\0';
        for(int j=0;j<N_ADC_UNITS;j++){
            for(int i=0;i<N_ADC_CHANNELS;i++){
                // add sample to circular memory
                adc.adc_raw_mem[j][i][idx%N_AVG] = adc.adc_raw[j][i];
                adc.voltage_mem[j][i][idx%N_AVG] = adc.voltage[j][i];
                // removed detailed logs to prevent memory problems
                // snprintf(err_buf + strlen(err_buf), ERR_BUF_SIZE, "ADC%d.%d: ", j+1, i);
                // snprintf(err_buf + strlen(err_buf), ERR_BUF_SIZE, "%4d", adc.adc_raw[j][i]);
                // snprintf(err_buf + strlen(err_buf), ERR_BUF_SIZE, " (%4d mV)", adc.voltage[j][i]);
                // snprintf(err_buf + strlen(err_buf), ERR_BUF_SIZE, " | ");
            }
        }
        // ESP_LOGI(TAG, "%s", err_buf);

        if (idx%N_AVG == 0){
            idx = 0; // reset index to avoid overflow
            ESP_LOGD(TAG, "AVERAGING");
            
            // *err_buf = '\0';
            for(int j=0;j<N_ADC_UNITS;j++){
                for(int i=0;i<N_ADC_CHANNELS;i++){
                    // apply a median filter
                    qsort(adc.adc_raw_mem[j][i], N_AVG, sizeof(int), compare);
                    qsort(adc.voltage_mem[j][i], N_AVG, sizeof(int), compare);
                    // snprintf(err_buf + strlen(err_buf), ERR_BUF_SIZE, "ADC%d.%d: ", j+1, i);
                    // snprintf(err_buf + strlen(err_buf), ERR_BUF_SIZE, "median=%4d", adc.adc_raw_mem[j][i][N_AVG/2]);
                    // snprintf(err_buf + strlen(err_buf), ERR_BUF_SIZE, " (%4d mV)", adc.voltage_mem[j][i][N_AVG/2]);
                    // calculate average excluding median-filtered outliers
                    adc.adc_raw_avg[j][i] = average_adc_raw(adc.adc_raw_mem[j][i], N_AVG);
                    adc.voltage_avg[j][i] = average_voltage(adc.voltage_mem[j][i], N_AVG);
                    // snprintf(err_buf + strlen(err_buf), ERR_BUF_SIZE, ", avg=%6.1f", adc.adc_raw_avg[j][i]);
                    // snprintf(err_buf + strlen(err_buf), ERR_BUF_SIZE, " (%6.1f mV)", adc.voltage_avg[j][i]);
                    // snprintf(err_buf + strlen(err_buf), ERR_BUF_SIZE, " | ");
                }
            }
            // ESP_LOGI(TAG, "%s", err_buf);

    
            level_0 = 100 * (adc.voltage_avg[0][0] / R_EFF - I_EMPTY) / ( I_FULL - I_EMPTY );
            level_1 = 100 * (adc.voltage_avg[0][1] / R_EFF - I_EMPTY) / ( I_FULL - I_EMPTY );
            
            display_show_levels(level_0, level_1);

            display_show_ip(get_current_ip_string());

            datetime_current = time(NULL);
            strftime(datetime_str, sizeof(datetime_str), "%Y/%m/%d %H:%M:%S", localtime(&datetime_current) );
            display_show_datetime(datetime_str);

            // 
            if ( (strcmp(last_identifier, DEFAULT_IDENTIFIER) != 0) && (difftime(datetime_current, datetime_last_login) > DELAY_BEFORE_LOGOUT_SECONDS) ) {
                // strcpy(last_identifier, DEFAULT_IDENTIFIER);
                keypad_callback(DEFAULT_PASSWORD, DEFAULT_IDENTIFIER, false);
            }

            // Read temperature from DS18B20 sensors
            // for (int i = 0; i < ds18b20_device_num; i ++) {
            //     ESP_ERROR_CHECK(ds18b20_trigger_temperature_conversion(ds18b20s[i]));
            //     ESP_ERROR_CHECK(ds18b20_get_temperature(ds18b20s[i], &temperature));
            //     ESP_LOGI(TAG, "temperature read from DS18B20[%d]: %.2fC", i, temperature);
            // }
            // For simplicity, just read from the last DS18B20 sensor
            //display_show_temperature(temperature);

            // Read temperature from AHT20 sensor
            read_temperature_humidity(&temperature, &humidity);
            display_show_temperature(temperature);
            display_show_humidity(humidity);



            if (    ( (difftime(datetime_current, datetime_last_upload) > MIN_UPLOAD_INTERVAL_SECONDS) && 
                      ( (level_0 > level_last_logged_0 + UPLOAD_THRESHOLD_PERCENT_0) ||  
                        (level_0 < level_last_logged_0 - UPLOAD_THRESHOLD_PERCENT_0) ||  
                        (level_1 > level_last_logged_1 + UPLOAD_THRESHOLD_PERCENT_1) ||  
                        (level_1 < level_last_logged_1 - UPLOAD_THRESHOLD_PERCENT_1) ) )
                ||  (strcmp(last_identifier_uploaded, "") == 0)
                ||  (strcmp(last_identifier_uploaded, last_identifier) != 0)
                ||  (difftime(datetime_current, datetime_last_upload) > MAX_UPLOAD_INTERVAL_SECONDS)
                ) {
                
                // Send to Google Sheets via Google Apps Script
                ESP_LOGI(TAG, "Level threshold reached: uploading to Google Sheet");
                level_last_logged_0 = level_0;
                level_last_logged_1 = level_1;                
                // display_show_status("UPLOADING...", DISPLAY_COLOR_GREEN);
                if (send_to_google_script(
                            adc.voltage_avg[0][0] / 1000.,
                            adc.voltage_avg[0][1] / 1000.,
                            adc.adc_raw_avg[0][0],
                            adc.adc_raw_avg[0][1],
                            last_identifier,
                            temperature,
                            humidity
                        )
                        != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to send data to Google Sheets");
                    // display_show_status("UPLOAD FAILED", DISPLAY_COLOR_RED);
                    strncpy(last_identifier_uploaded, "", sizeof(last_identifier_uploaded));
                }
                else {
                    ESP_LOGI(TAG, "Data successfully sent to Google Sheets");
                    // char msg[128];
                    // strftime(datetime_str, sizeof(datetime_str), "%d %b %H:%M:%S", localtime(&datetime) );
                    strftime(datetime_str, sizeof(datetime_str), "Upload %d %b %H:%M:%S", localtime(&datetime_current) );
                    // snprintf(msg, sizeof(msg), "Uploaded: %s", datetime_str);
                    display_show_last_upload(datetime_str);
                    strncpy(last_identifier_uploaded, last_identifier, sizeof(last_identifier_uploaded));
                    datetime_last_upload = datetime_current;
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(WAIT_TIME_MS));
    }

    // release ADC
    adc_oneshot_teardown(&adc);
    /********************
     *** END-ADC read ***
     ********************/



    /***************
     *** Restart ***
     ***************/
    delayed_restart();
}
