#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"

#include "secret.h" // for  #define GOOGLE_SCRIPT_ID "***secret***" // ID from your deployed Apps Script


 /* Send data to Google Script */
 esp_err_t send_to_google_script(float voltage0, float voltage1, int raw_value0, int raw_value1, char* user);