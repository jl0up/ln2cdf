#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"

 /* Google Script Configuration */
 #define GOOGLE_SCRIPT_ID "AKfycbzGnmqHohoQdRFfWisrl50etJgSVShO9_mcDPWrczsaDG443c9AHiN7vB9pb2URqEZK" // ID from your deployed Apps Script
  
 /* Send data to Google Script */
 esp_err_t send_to_google_script(float voltage0, float voltage1, int raw_value0, int raw_value1);