#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "soc/soc_caps.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

#define N_AVG 27  //number of ADC samples to average MAX ~27 because of memory limits
#define N_ADC_UNITS 1
#define N_ADC_CHANNELS 2

/*---------------------------------------------------------------
        ADC General Macros
---------------------------------------------------------------*/
//ADC1 Channels
#if CONFIG_IDF_TARGET_ESP32
#define EXAMPLE_ADC1_CHAN0          ADC_CHANNEL_4
#define EXAMPLE_ADC1_CHAN1          ADC_CHANNEL_5
#else
#define EXAMPLE_ADC1_CHAN0          ADC_CHANNEL_4
#define EXAMPLE_ADC1_CHAN1          ADC_CHANNEL_5
#endif

#if (SOC_ADC_PERIPH_NUM >= 2) && !CONFIG_IDF_TARGET_ESP32C3
/**
 * On ESP32C3, ADC2 is no longer supported, due to its HW limitation.
 * Search for errata on espressif website for more details.
 */
#define EXAMPLE_USE_ADC2            0
#endif

#if EXAMPLE_USE_ADC2
//ADC2 Channels
#if CONFIG_IDF_TARGET_ESP32
#define EXAMPLE_ADC2_CHAN0          ADC_CHANNEL_0
#else
#define EXAMPLE_ADC2_CHAN0          ADC_CHANNEL_0
#endif
#endif  //#if EXAMPLE_USE_ADC2

#define EXAMPLE_ADC_ATTEN           ADC_ATTEN_DB_0
#define EXAMPLE_ADC_BITWIDTH        ADC_BITWIDTH_DEFAULT



typedef struct {
    adc_oneshot_unit_handle_t adc1_handle;
    adc_oneshot_unit_handle_t adc2_handle;
    adc_oneshot_unit_init_cfg_t init_config1;
    adc_oneshot_unit_init_cfg_t init_config2;
    adc_oneshot_chan_cfg_t config;
    adc_cali_handle_t adc1_cali_chan0_handle;
    adc_cali_handle_t adc1_cali_chan1_handle;
    adc_cali_handle_t adc2_cali_handle;
    bool do_calibration1_chan0;
    bool do_calibration1_chan1;
    bool do_calibration2;
    int adc_raw[N_ADC_UNITS][N_ADC_CHANNELS];
    int voltage[N_ADC_UNITS][N_ADC_CHANNELS];
    int adc_raw_mem[N_ADC_UNITS][N_ADC_CHANNELS][N_AVG];
    int voltage_mem[N_ADC_UNITS][N_ADC_CHANNELS][N_AVG];
    float adc_raw_avg[N_ADC_UNITS][N_ADC_CHANNELS];
    float voltage_avg[N_ADC_UNITS][N_ADC_CHANNELS];
} adc_t;

void adc_oneshot_init(adc_t *adc);

void adc_oneshot_get(adc_t *adc);

void adc_oneshot_teardown(adc_t *adc);