#include "google_sheet.h"
#include "wifi_station.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "google_sheet.c";

// Retry configuration
#define GOOGLE_SCRIPT_MAX_RETRIES 2          // Try up to 3 times total (initial + 2 retries)
#define GOOGLE_SCRIPT_RETRY_DELAY_MS 1000    // Wait 1 second between retries

 /* HTTP Event Handler */
 static esp_err_t http_event_handler(esp_http_client_event_t *evt) {
    switch(evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGE(TAG, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
            break;
        case HTTP_EVENT_ON_DATA:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
            if (!esp_http_client_is_chunked_response(evt->client)) {
                // Copy response data if user_data is provided
                if (evt->user_data && evt->data_len > 0) {
                    // Make sure to not overflow the buffer
                    size_t copy_len = evt->data_len;
                    if (copy_len > 511) { // Ensure we leave space for null terminator
                        copy_len = 511;
                    }
                    memcpy(evt->user_data, evt->data, copy_len);
                    ((char*)evt->user_data)[copy_len] = 0; // Null terminate
                }
            }
            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGD(TAG, "HTTP_EVENT_DISCONNECTED");
            break;
        case HTTP_EVENT_REDIRECT:
            ESP_LOGI(TAG, "HTTP_EVENT_REDIRECT");
            break;
    }
    return ESP_OK;
}

 /* Send data to Google Script with automatic retry */
 esp_err_t send_to_google_script(float voltage0, float voltage1, int raw_value0, int raw_value1, char* user, float temperature, float humidity) {
    char url[1024];
    
    // Get current time for timestamp
    time_t now;
    time(&now);
    
    // Construct URL with query parameters
    // Note: URL encoding would be better but we're keeping it simple
    snprintf(url, sizeof(url), 
        "https://script.google.com/macros/s/%s/exec?timestamp=%ju&voltage0=%.4f&voltage1=%.4f&raw_value0=%d&raw_value1=%d&user=%s&temperature=%.1f&humidity=%.1f",
        GOOGLE_SCRIPT_ID, now, voltage0, voltage1, raw_value0, raw_value1, user, temperature, humidity);
    
    ESP_LOGI(TAG, "Sending data to Google Script");
    
    // Configure the HTTP client (will be reused for retries)
    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .event_handler = http_event_handler,
        .crt_bundle_attach = esp_crt_bundle_attach,  // Use ESP's certificate bundle for TLS
        .timeout_ms = 10000,                         // 10 second timeout
        .buffer_size_tx = 1024,
    };
    
    // Retry loop
    int retry_count = 0;
    while (retry_count <= GOOGLE_SCRIPT_MAX_RETRIES) {
        // Response buffer (reset for each attempt)
        char response_buffer[1024] = {0};
        config.user_data = response_buffer;
        
        // Initialize the HTTP client
        esp_http_client_handle_t client = esp_http_client_init(&config);
        if (client == NULL) {
            ESP_LOGW(TAG, "HTTP client init failed (attempt %d/%d)", 
                    retry_count + 1, GOOGLE_SCRIPT_MAX_RETRIES + 1);
            goto retry;
        }
        
        // Perform the HTTP request
        esp_err_t err = esp_http_client_perform(client);
        
        // Check for success
        if (err == ESP_OK) {
            int status_code = esp_http_client_get_status_code(client);
            ESP_LOGD(TAG, "HTTP GET Status = %d, Response = %s", status_code, response_buffer);
            
            esp_http_client_cleanup(client);
            
            if (status_code == 200) {
                ESP_LOGI(TAG, "Data successfully sent to Google Sheets (attempt %d)", 
                        retry_count + 1);
                return ESP_OK;
            } else {
                ESP_LOGW(TAG, "Google Script returned error status: %d (attempt %d/%d)",
                        status_code, retry_count + 1, GOOGLE_SCRIPT_MAX_RETRIES + 1);
            }
        } else {
            ESP_LOGW(TAG, "HTTP GET request failed (attempt %d/%d): %s",
                    retry_count + 1, GOOGLE_SCRIPT_MAX_RETRIES + 1, esp_err_to_name(err));
            esp_http_client_cleanup(client);
        }
        
        // Prepare for retry
    retry:
        if (retry_count < GOOGLE_SCRIPT_MAX_RETRIES) {
            ESP_LOGI(TAG, "Retrying in %d ms...", GOOGLE_SCRIPT_RETRY_DELAY_MS);
            vTaskDelay(pdMS_TO_TICKS(GOOGLE_SCRIPT_RETRY_DELAY_MS));
        }
        
        retry_count++;
    }
    
    // All retries exhausted
    ESP_LOGE(TAG, "Failed to send data to Google Sheets after %d attempts", 
            GOOGLE_SCRIPT_MAX_RETRIES + 1);
    return ESP_FAIL;
}
