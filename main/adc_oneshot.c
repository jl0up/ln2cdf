#include "adc_oneshot.h"

const static char *TAG = "adc_oneshot.c";

static bool example_adc_calibration_init(adc_unit_t unit, adc_channel_t channel, adc_atten_t atten, adc_cali_handle_t *out_handle);
static void example_adc_calibration_deinit(adc_cali_handle_t handle);


void adc_oneshot_init(adc_t* adc)
{
    //-------------ADC1 Init---------------//
    // adc->init_config1.unit_id = ADC_UNIT_1;
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&(adc->init_config1), &(adc->adc1_handle)));

    //-------------ADC1 Config---------------//
    adc->config.atten = EXAMPLE_ADC_ATTEN;
    adc->config.bitwidth = EXAMPLE_ADC_BITWIDTH;

    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc->adc1_handle, EXAMPLE_ADC1_CHAN0, &(adc->config)));
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc->adc1_handle, EXAMPLE_ADC1_CHAN1, &(adc->config)));

    //-------------ADC1 Calibration Init---------------//
    adc->adc1_cali_chan0_handle = NULL;
    adc->adc1_cali_chan1_handle = NULL;
    adc->do_calibration1_chan0 = example_adc_calibration_init(ADC_UNIT_1, EXAMPLE_ADC1_CHAN0, EXAMPLE_ADC_ATTEN, &(adc->adc1_cali_chan0_handle));
    adc->do_calibration1_chan1 = example_adc_calibration_init(ADC_UNIT_1, EXAMPLE_ADC1_CHAN1, EXAMPLE_ADC_ATTEN, &(adc->adc1_cali_chan1_handle));
}

void adc_oneshot_get(adc_t* adc)
{
    esp_err_t err;

    err = adc_oneshot_read(adc->adc1_handle, EXAMPLE_ADC1_CHAN0, &(adc->adc_raw[0]));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "ADC1 chan0 read failed: %s (keeping last value)", esp_err_to_name(err));
    } else if (adc->do_calibration1_chan0) {
        err = adc_cali_raw_to_voltage(adc->adc1_cali_chan0_handle, adc->adc_raw[0], &(adc->voltage[0]));
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "ADC1 chan0 cali failed: %s (keeping last value)", esp_err_to_name(err));
        }
    }

    err = adc_oneshot_read(adc->adc1_handle, EXAMPLE_ADC1_CHAN1, &(adc->adc_raw[1]));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "ADC1 chan1 read failed: %s (keeping last value)", esp_err_to_name(err));
    } else if (adc->do_calibration1_chan1) {
        err = adc_cali_raw_to_voltage(adc->adc1_cali_chan1_handle, adc->adc_raw[1], &(adc->voltage[1]));
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "ADC1 chan1 cali failed: %s (keeping last value)", esp_err_to_name(err));
        }
    }
}



void adc_oneshot_teardown(adc_t* adc)
{
    //Tear Down
    ESP_ERROR_CHECK(adc_oneshot_del_unit(adc->adc1_handle));
    if (adc->do_calibration1_chan0) {
        example_adc_calibration_deinit((adc->adc1_cali_chan0_handle));
    }
    if (adc->do_calibration1_chan1) {
        example_adc_calibration_deinit(adc->adc1_cali_chan1_handle);
    }

}

/*---------------------------------------------------------------
        ADC Calibration
---------------------------------------------------------------*/
static bool example_adc_calibration_init(adc_unit_t unit, adc_channel_t channel, adc_atten_t atten, adc_cali_handle_t *out_handle)
{
    adc_cali_handle_t handle = NULL;
    esp_err_t ret = ESP_FAIL;
    bool calibrated = false;

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    if (!calibrated) {
        ESP_LOGI(TAG, "calibration scheme version is %s", "Curve Fitting");
        adc_cali_curve_fitting_config_t cali_config = {
            .unit_id = unit,
            .chan = channel,
            .atten = atten,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        ret = adc_cali_create_scheme_curve_fitting(&cali_config, &handle);
        if (ret == ESP_OK) {
            calibrated = true;
        }
    }
#endif

#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    if (!calibrated) {
        ESP_LOGI(TAG, "calibration scheme version is %s", "Line Fitting");
        adc_cali_line_fitting_config_t cali_config = {
            .unit_id = unit,
            .atten = atten,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        ret = adc_cali_create_scheme_line_fitting(&cali_config, &handle);
        if (ret == ESP_OK) {
            calibrated = true;
        }
    }
#endif

    *out_handle = handle;
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Calibration Success");
    } else if (ret == ESP_ERR_NOT_SUPPORTED || !calibrated) {
        ESP_LOGW(TAG, "eFuse not burnt, skip software calibration");
    } else {
        ESP_LOGE(TAG, "Invalid arg or no memory");
    }

    return calibrated;
}

static void example_adc_calibration_deinit(adc_cali_handle_t handle)
{
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    ESP_LOGI(TAG, "deregister %s calibration scheme", "Curve Fitting");
    ESP_ERROR_CHECK(adc_cali_delete_scheme_curve_fitting(handle));

#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    ESP_LOGI(TAG, "deregister %s calibration scheme", "Line Fitting");
    ESP_ERROR_CHECK(adc_cali_delete_scheme_line_fitting(handle));
#endif
}
