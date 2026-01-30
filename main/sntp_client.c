#include "sntp_client.h"
#include "esp_log.h"

static const char *TAG = "sntp_client.c";

/* Initialize SNTP for time sync */
void initialize_sntp(void) {
    ESP_LOGI(TAG, "Initializing SNTP");
    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_init();
    setenv("TZ", "CET-1CEST,M3.5.0/02:00,M10.5.0/03:00", 1); // "CET-1CEST,M3.5.0/02:00,M10.5.0/03:00" is equivalent to Europe/Paris
    tzset();
}
