#include "esp_ota_ops.h"
#include "esp_http_server.h"
#include "esp_wifi.h"
#include "delayed_restart.h"

static esp_err_t ota_handler(httpd_req_t *req) {
    esp_ota_handle_t ota_handle = 0;
    const esp_partition_t *ota_partition = NULL;
    
    // Get next OTA partition
    ota_partition = esp_ota_get_next_update_partition(NULL);
    if (ota_partition == NULL) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No OTA partition found");
        return ESP_FAIL;
    }
    
    // Begin OTA
    esp_err_t err = esp_ota_begin(ota_partition, OTA_SIZE_UNKNOWN, &ota_handle);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA begin failed");
        return ESP_FAIL;
    }
    
    // Receive and write firmware data
    char buffer[1024];
    int received;
    while ((received = httpd_req_recv(req, buffer, sizeof(buffer))) > 0) {
        err = esp_ota_write(ota_handle, buffer, received);
        if (err != ESP_OK) {
            esp_ota_abort(ota_handle);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA write failed");
            return ESP_FAIL;
        }
    }
    
    // Finalize OTA
    err = esp_ota_end(ota_handle);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA end failed");
        return ESP_FAIL;
    }
    
    // Set new boot partition
    err = esp_ota_set_boot_partition(ota_partition);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Set boot partition failed");
        return ESP_FAIL;
    }
    
    httpd_resp_sendstr(req, "OTA successful, rebooting...");
    
    // Reboot after short delay
    delayed_restart();
    
    return ESP_OK;
}


void start_ota_server() {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t server = NULL;
    
    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t ota_uri = {
            .uri = "/ota",
            .method = HTTP_POST,
            .handler = ota_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &ota_uri);
    }
}