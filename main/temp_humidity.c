#include "temp_humidity.h"
#include "esp_err.h"
#include "esp_log.h"

#define TAG "TEMP_HUMIDITY"

i2c_bus_handle_t i2c_bus_handle;
aht20_dev_handle_t aht20_handle;

/**
 * @brief Initialize I2C bus
 */
esp_err_t i2c_bus_init(void)
{
    i2c_config_t i2c_conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_MASTER_FREQ_HZ,
    };
    
    i2c_bus_handle = i2c_bus_create(I2C_MASTER_NUM, &i2c_conf);
    if (i2c_bus_handle == NULL) {
        ESP_LOGE(TAG, "I2C bus creation failed");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "I2C bus initialized successfully");
    return ESP_OK;
}

/**
 * @brief Initialize AHT20 sensor
 */
esp_err_t aht20_sensor_init(void)
{
    aht20_i2c_config_t i2c_conf = {
        .bus_inst = i2c_bus_handle,
        .i2c_addr = AHT20_I2C_ADDRESS,
    };
    
    esp_err_t err = aht20_new_sensor(&i2c_conf, &aht20_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "AHT20 sensor creation failed: %s", esp_err_to_name(err));
        return err;
    }
    
    ESP_LOGI(TAG, "AHT20 sensor initialized successfully");
    return ESP_OK;
}

/**
 * @brief Read and display temperature and humidity
 */
void print_sensor_data(void)
{
    uint32_t temperature_raw = 0;
    float temperature = 0.0;
    uint32_t humidity_raw = 0;
    float humidity = 0.0;
    
    esp_err_t err = aht20_read_temperature_humidity(aht20_handle, 
                                                   &temperature_raw, &temperature,
                                                   &humidity_raw, &humidity);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Temperature: %.2f °C (raw: %lu), Humidity: %.2f %% (raw: %lu)", 
                 temperature, temperature_raw, humidity, humidity_raw);
    } else {
        ESP_LOGE(TAG, "Failed to read sensor data: %s", esp_err_to_name(err));
    }
}

void read_temperature_humidity(float* temperature, float* humidity){
    uint32_t temperature_raw = 0;
    uint32_t humidity_raw = 0;
    
    esp_err_t err = aht20_read_temperature_humidity(aht20_handle, 
                                                   &temperature_raw, temperature,
                                                   &humidity_raw, humidity);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Temperature: %.2f °C (raw: %lu), Humidity: %.2f %% (raw: %lu)", 
                 *temperature, temperature_raw, *humidity, humidity_raw);
    } else {
        ESP_LOGE(TAG, "Failed to read sensor data: %s", esp_err_to_name(err));
        *temperature = -99.99;
        *humidity = -99.99;
    }
}

/**
 * @brief Cleanup resources
 */
void cleanup_resources(void)
{
    if (aht20_handle != NULL) {
        aht20_del_sensor(aht20_handle);
        aht20_handle = NULL;
        ESP_LOGI(TAG, "AHT20 sensor handle deleted");
    }
    
    if (i2c_bus_handle != NULL) {
        i2c_bus_delete(&i2c_bus_handle);
        i2c_bus_handle = NULL;
        ESP_LOGI(TAG, "I2C bus handle deleted");
    }
}
