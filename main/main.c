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

#define WAIT_TIME_MS 60*1000

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
    return (float)avg / (float)(n - k - k) / 1000.;
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


void app_main(void)
{
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

        adc.adc_raw_mem[0][0][idx%N_AVG] = adc.adc_raw[0][0];
        printf("ADC1.0: %4d", adc.adc_raw[0][0]);
        if (adc.do_calibration1_chan0) {
            adc.voltage_mem[0][0][idx%N_AVG] = adc.voltage[0][0];
            printf(" (%4d mV)", adc.voltage[0][0]);
        }
        printf(" | ");
        
        adc.adc_raw_mem[0][1][idx%N_AVG] = adc.adc_raw[0][1];
        printf("ADC1.1: %4d", adc.adc_raw[0][1]);
        if (adc.do_calibration1_chan0) {
            adc.voltage_mem[0][1][idx%N_AVG] = adc.voltage[0][1];
            printf(" (%4d mV)", adc.voltage[0][1]);
        }
        printf("\n");
        
        if (idx%N_AVG == 0){
            printf("AVERAGING: idx=%d\tidxMODN_AVG=%d\tN_AVG/2=%d\n", idx, idx%N_AVG, N_AVG/2);

            // apply a median filter
            qsort(adc.adc_raw_mem[0][0], N_AVG, sizeof(int), compare);
            qsort(adc.adc_raw_mem[0][1], N_AVG, sizeof(int), compare);
            if (adc.do_calibration1_chan0) {
                qsort(adc.voltage_mem[0][0], N_AVG, sizeof(int), compare);
                qsort(adc.voltage_mem[0][1], N_AVG, sizeof(int), compare);
            }

            printf("ADC1.0: %4d", adc.adc_raw_mem[0][0][N_AVG/2]);
            printf(" [%4d]", average_adc_raw(adc.adc_raw_mem[0][0], N_AVG));
            if (adc.do_calibration1_chan0) {
                printf(" (%4d mV)", adc.voltage_mem[0][0][N_AVG/2]);
                printf(" [%5.1f mV]", 1000.*average_voltage(adc.voltage_mem[0][0], N_AVG));
            }
            printf(" | ");
    
            printf("ADC1.1: %4d", adc.adc_raw_mem[0][1][N_AVG/2]);
            printf(" [%4d]", average_adc_raw(adc.adc_raw_mem[0][1], N_AVG));
            if (adc.do_calibration1_chan0) {
                printf(" (%4d mV)", adc.voltage_mem[0][1][N_AVG/2]);
                printf(" [%5.1f mV]", 1000.*average_voltage(adc.voltage_mem[0][1], N_AVG));
            }
            printf("\n");
    

            // Send to Google Sheets via Google Apps Script
            if (send_to_google_script(
                        average_voltage(adc.voltage_mem[0][0], N_AVG),
                        average_voltage(adc.voltage_mem[0][1], N_AVG),
                        average_adc_raw(adc.adc_raw_mem[0][0], N_AVG),
                        average_adc_raw(adc.adc_raw_mem[0][1], N_AVG))
                    != ESP_OK) {
                printf("Failed to send data to Google Sheets");
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
