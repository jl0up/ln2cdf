#include "google_sheet.h"
#include "wifi_station.h"

 static const char *TAG = "google_sheet.c";

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

 /* Send data to Google Script */
 esp_err_t send_to_google_script(float voltage0, float voltage1, int raw_value0, int raw_value1, char* user, float temperature, float humidity) {
    char url[512];
    
    // Get current time for timestamp
    char time_str[64] = "unknown_time";
    struct tm timeinfo;
    time_t now;
    time(&now);
    localtime_r(&now, &timeinfo);
    strftime(time_str, sizeof(time_str), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);
    
    // Construct URL with query parameters
    // Note: URL encoding would be better but we're keeping it simple
    snprintf(url, sizeof(url), 
        "https://script.google.com/macros/s/%s/exec?timestamp=%ju&voltage0=%.4f&voltage1=%.4f&raw_value0=%d&raw_value1=%d&user=%s&temperature=%.1f&humidity=%.1f",
        GOOGLE_SCRIPT_ID, now, voltage0, voltage1, raw_value0, raw_value1, user, temperature, humidity);
    
    ESP_LOGI(TAG, "Sending data to Google Script: %s", url);
    
    // Response buffer
    char response_buffer[1024] = {0};
    
    // Configure the HTTP client
    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .event_handler = http_event_handler,
        .user_data = response_buffer,
        .crt_bundle_attach = esp_crt_bundle_attach,  // Use ESP's certificate bundle for TLS
        .timeout_ms = 10000,                         // 10 second timeout
        .buffer_size_tx = 1024,
    };
    
    // Initialize the HTTP client
    esp_http_client_handle_t client = esp_http_client_init(&config);
    
    // Perform the HTTP request
    esp_err_t err = esp_http_client_perform(client);
    
    // Check for success
    if (err == ESP_OK) {
        int status_code = esp_http_client_get_status_code(client);
        ESP_LOGD(TAG, "HTTP GET Status = %d, Response = %s", status_code, response_buffer);
        
        if (status_code == 200) {
            ESP_LOGD(TAG, "Data successfully sent to Google Sheets");
        } else {
            ESP_LOGE(TAG, "Google Script returned error status: %d", status_code);
        }
    } else {
        ESP_LOGE(TAG, "HTTP GET request failed: %s", esp_err_to_name(err));
    }
    
    // Clean up the HTTP client
    esp_http_client_cleanup(client);
    
    return (err == ESP_OK) ? ESP_OK : ESP_FAIL;
}
