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



/* Wifi connection parameters (wifi_station.c) */
// #define EXAMPLE_ESP_WIFI_SSID      "" // CONFIG_ESP_WIFI_SSID
// #define EXAMPLE_ESP_WIFI_PASS      "" //CONFIG_ESP_WIFI_PASSWORD
// #define EXAMPLE_ESP_MAXIMUM_RETRY  3 // CONFIG_ESP_MAXIMUM_RETRY



void app_main(void)
{
    print_chip_information();

    wifi_station_connect();

    delayed_restart();
}
