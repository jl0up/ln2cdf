// webserver.h - Header file for ESP32 web server with OTA
#ifndef WEBSERVER_H
#define WEBSERVER_H

#include "esp_http_server.h"

/**
 * @brief Start the web server
 * @return Handle to the server, or NULL on failure
 */
httpd_handle_t start_webserver(void);

/**
 * @brief Stop the web server
 * @param server Handle returned by start_webserver()
 */
void stop_webserver(httpd_handle_t server);

/**
 * @brief Update variables displayed on the web page
 * @param a First float value
 * @param b Second float value  
 * @param c Third float value
 * @param d Fourth float value
 * @param e String value (will be copied, max 255 chars)
 */
void webpage_update(float a, float b, float c, float d, const char *e);

#endif // WEBSERVER_H