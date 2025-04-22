/*
 * ln2cdf project
 *
 * by jl0up
 * 
 * https://github.com/jl0up/ln2cdf
 * 
 */

#include "chip_info.h"
#include "delayed_restart.h"
#include "wifi_station.h"
#include "adc_oneshot.h"


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
    while (1) {
        adc_oneshot_get(&adc);

        printf("ADC1.0: %4d", adc.adc_raw[0][0]);
        if (adc.do_calibration1_chan0) {
            printf(" (%4d mV)", adc.voltage[0][0]);
        }
        printf(" | ");

        printf("ADC1.1: %4d", adc.adc_raw[0][1]);
        if (adc.do_calibration1_chan0) {
            printf(" (%4d mV)", adc.voltage[0][1]);
        }
        printf("\n");

        vTaskDelay(pdMS_TO_TICKS(1000));
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
