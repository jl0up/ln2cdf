#include "i2c_bus.h"
#include "aht20.h"

// I2C configuration for ESP32-C6
#define I2C_MASTER_SCL_IO           13      // GPIO22 for SCL
#define I2C_MASTER_SDA_IO           12      // GPIO21 for SDA
#define I2C_MASTER_NUM              0       // I2C port number
#define I2C_MASTER_FREQ_HZ          100000  // I2C master clock frequency

// AHT20 I2C address (CE pin low)
#define AHT20_I2C_ADDRESS           AHT20_ADDRRES_0

esp_err_t i2c_bus_init(void);

esp_err_t aht20_sensor_init(void);

void print_sensor_data(void);

void read_temperature_humidity(float* temperature, float* humidity);

void cleanup_resources(void);