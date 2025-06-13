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

#define WAIT_TIME_MS 10*1000/N_AVG
#define UPLOAD_THRESHOLD_PERCENT 1.0
#define R_EFF 46.0 // Ohm
#define I_EMPTY 3.913
#define I_FULL 20.131
#define ERR_BUF_SIZE 512

const static char *TAG = "ln2cdf: main.c";

int compare(const void *a, const void *b) {
    // int *valA = (int*) a;
    // int *valB = (int*) b;
    // return *valA - *valB;
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
        avg += mem[k];
    }
    return (float)avg / (float)(n - k - k);
}

int average_adc_raw(int* mem, int n){
    int avg = 0;
    int k = n/5;
    for (int i = k ; i < (n - k) ; i++ ) {
        // ESP_LOGD( "average_adc_raw()", "%d | %d | %d", n, k, i);
        avg += mem[k];
    }
    return avg / (n - k - k);
}

// Global variable to track password length for display updates
static int current_password_length = 0;

// Integrated callback function that handles all keypad events
void my_callback(const char* password, const char* message, bool success)
{
    ESP_LOGI(TAG, "Callback triggered - password: '%s', message: '%s', success: %d", 
             password, message, success);
    
    // Handle different callback scenarios based on the message content
    if (strcmp(message, "Entering password...") == 0) {
        // User is entering a password digit
        current_password_length++;
        display_update_password_dots(current_password_length);
        display_show_status("Entering password...", DISPLAY_COLOR_CYAN);
        
    } else if (strcmp(message, "Cancelled - Ready") == 0) {
        // User pressed * to cancel
        current_password_length = 0;
        display_update_password_dots(0);
        display_show_status("Cancelled - Ready", DISPLAY_COLOR_ORANGE);
        
    } else if (strcmp(message, "TIMEOUT") == 0) {
        // Password entry timed out
        current_password_length = 0;
        display_update_password_dots(0);
        display_show_status("TIMEOUT - Try again", DISPLAY_COLOR_RED);
        
    } else {
        // Password was completed - show login result
        current_password_length = 0;
        display_show_login_result(message, success);
    }
}


void app_main(void)
{
    char err_buf[ERR_BUF_SIZE] = "";

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



    // Initialize SPI and display
    display_init();
    
    // Create UI
    display_create_ui();

    // Initialize keypad
    keypad_init();

    keypad_set_password_callback(my_callback);

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

    /***************************
     *** user identification ***
     ***************************/
    enum Codes {
        USER_NONE = -1,
        USER_0 = 1234,
        USER_1 = 5678,
    };
    // typedef struct {
    //     char name[128];
    //     enum Codes code;
    //     time_t last_logged_in;
    //     time_t last_logged_out;
    //     int number_of_auto_logout;
    //     int number_of_logins;
    //     time_t average_login_duration;
    //     float average_consumption;
    //     float total_consumption;
    // } User;

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
    adc_oneshot_unit_init_cfg_t init_config2 = {
        .unit_id = ADC_UNIT_2,
    };
    adc.init_config2 = init_config2;
    adc_oneshot_init(&adc);


    // Reading ADC in loop
    unsigned int idx = 0;
    while (1) {
        idx++;

        adc_oneshot_get(&adc);

        *err_buf = '\0';
        for(int j=0;j<1;j++){
            for(int i=0;i<2;i++){
                // add sample to circular memory
                snprintf(err_buf + strlen(err_buf), ERR_BUF_SIZE, "ADC%d.%d: ", j+1, i);
                snprintf(err_buf + strlen(err_buf), ERR_BUF_SIZE, "%4d", adc.adc_raw[j][i]);
                adc.adc_raw_mem[j][i][idx%N_AVG] = adc.adc_raw[j][i];
                snprintf(err_buf + strlen(err_buf), ERR_BUF_SIZE, " (%4d mV)", adc.voltage[j][i]);
                adc.voltage_mem[j][i][idx%N_AVG] = adc.voltage[j][i];
                snprintf(err_buf + strlen(err_buf), ERR_BUF_SIZE, " | ");
            }
        }
        ESP_LOGI(TAG, "%s", err_buf);

        if (idx%N_AVG == 0){
            ESP_LOGD(TAG, "AVERAGING: idx=%d\tidxMODN_AVG=%d\tN_AVG/2=%d\n", idx, idx%N_AVG, N_AVG/2);
            
            *err_buf = '\0';
            for(int j=0;j<1;j++){
                for(int i=0;i<2;i++){
                    // apply a median filter
                    qsort(adc.adc_raw_mem[j][i], N_AVG, sizeof(int), compare);
                    snprintf(err_buf + strlen(err_buf), ERR_BUF_SIZE, "ADC%d.%d: ", j+1, i);
                    snprintf(err_buf + strlen(err_buf), ERR_BUF_SIZE, "median=%4d", adc.adc_raw_mem[j][i][N_AVG/2]);
                    qsort(adc.voltage_mem[j][i], N_AVG, sizeof(int), compare);
                    snprintf(err_buf + strlen(err_buf), ERR_BUF_SIZE, " (%4d mV)", adc.voltage_mem[j][i][N_AVG/2]);
                    // calculate average excluding median-filtered outliers
                    adc.adc_raw_avg[j][i] = average_adc_raw(adc.adc_raw_mem[j][i], N_AVG);
                    snprintf(err_buf + strlen(err_buf), ERR_BUF_SIZE, ", avg=%6.1f", adc.adc_raw_avg[j][i]);
                    adc.voltage_avg[j][i] = average_voltage(adc.voltage_mem[j][i], N_AVG);
                    snprintf(err_buf + strlen(err_buf), ERR_BUF_SIZE, " (%6.1f mV)", adc.voltage_avg[j][i]);
                    snprintf(err_buf + strlen(err_buf), ERR_BUF_SIZE, " | ");
                }
            }
            ESP_LOGI(TAG, "%s", err_buf);

    
            level_0 = 100 * (adc.voltage_avg[0][0] / R_EFF - I_EMPTY) / ( I_FULL - I_EMPTY );
            level_1 = 100 * (adc.voltage_avg[0][1] / R_EFF - I_EMPTY) / ( I_FULL - I_EMPTY );

            if (    (level_0 > level_last_logged_0 + UPLOAD_THRESHOLD_PERCENT)
                ||  (level_0 < level_last_logged_0 - UPLOAD_THRESHOLD_PERCENT)
                ||  (level_1 > level_last_logged_1 + UPLOAD_THRESHOLD_PERCENT)
                ||  (level_1 < level_last_logged_1 - UPLOAD_THRESHOLD_PERCENT) ) {
                
                // Send to Google Sheets via Google Apps Script
                ESP_LOGI(TAG, "Level threshold reached: uploading to Google Sheet");
                level_last_logged_0 = level_0;
                level_last_logged_1 = level_1;                
                if (send_to_google_script(
                            adc.voltage_avg[0][0] / 1000.,
                            adc.voltage_avg[0][1] / 1000.,
                            adc.adc_raw_avg[0][0],
                            adc.adc_raw_avg[0][1])
                        != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to send data to Google Sheets");
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
