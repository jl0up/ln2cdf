#include "sntp_client.h"
#include "esp_log.h"

static const char *TAG = "sntp_client.c";

/* Initialize SNTP for time sync */
void initialize_sntp(void) {
    ESP_LOGI(TAG, "Initializing SNTP");
    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_init();
    setenv("TZ", "Europe/Paris", 1);
    tzset();
}
