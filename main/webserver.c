// webserver.c - Complete ESP32 web server with OTA, diagnostics, and variable display
#include "webserver.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "esp_ota_ops.h"
#include "esp_app_format.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include "esp_timer.h"
#include <time.h>

#define HISTORY_SIZE 64

// If you have delayed_restart.h, include it; otherwise we define a simple version
// #include "delayed_restart.h"

static const char *TAG = "webserver";

// ============================================================================
// Variable Storage
// ============================================================================
static float var_a = 0.0f;
static float var_b = 0.0f;
static float var_c = 0.0f;
static float var_d = 0.0f;
static char var_e[256] = "not set";

static int64_t history_timestamps[HISTORY_SIZE];  // microseconds since boot
static float var_a_history[HISTORY_SIZE];
static float var_b_history[HISTORY_SIZE];
static float var_c_history[HISTORY_SIZE];
static float var_d_history[HISTORY_SIZE];
static int history_index = 0;
static int history_count = 0;
static int history_update_interval_s = 900;  // Default: 15 minutes

// ======= Simple in-memory log capture =======
// Keep a modest in-memory log buffer. Use atomic ops to avoid races.
#define LOG_LINES 64
#define LOG_LINE_LEN 192
static char log_lines[LOG_LINES][LOG_LINE_LEN];
static int log_index = 0;    // next write index (atomic incremented)
static int log_count_lines = 0; // number of lines stored (<= LOG_LINES)

static int (*orig_vprintf)(const char *fmt, va_list ap) = NULL;

static void add_log_line(const char *line) {
    if (!line) return;
    // Reserve an index atomically
    int idx = __atomic_fetch_add(&log_index, 1, __ATOMIC_RELAXED);
    idx = idx % LOG_LINES;
    // store truncated
    strncpy(log_lines[idx], line, LOG_LINE_LEN - 1);
    log_lines[idx][LOG_LINE_LEN - 1] = '\0';
    // Increase count up to buffer size
    int prev = __atomic_load_n(&log_count_lines, __ATOMIC_RELAXED);
    if (prev < LOG_LINES) {
        __atomic_fetch_add(&log_count_lines, 1, __ATOMIC_RELAXED);
    }
}

// Minimal HTML escape for &, <, > (preserve newlines)
static void html_escape(const char *in, char *out, size_t out_size) {
    size_t oi = 0;
    for (size_t i = 0; in[i] != '\0' && oi + 1 < out_size; i++) {
        char c = in[i];
        if (c == '&') {
            const char *s = "&amp;";
            for (size_t j = 0; s[j] != '\0' && oi + 1 < out_size; j++) out[oi++] = s[j];
        } else if (c == '<') {
            const char *s = "&lt;";
            for (size_t j = 0; s[j] != '\0' && oi + 1 < out_size; j++) out[oi++] = s[j];
        } else if (c == '>') {
            const char *s = "&gt;";
            for (size_t j = 0; s[j] != '\0' && oi + 1 < out_size; j++) out[oi++] = s[j];
        } else {
            out[oi++] = c;
        }
    }
    out[oi] = '\0';
}

// Build logs HTML (raw text) into out buffer (caller must free)
static void get_logs_html(char *out, size_t out_size) {
    if (!out || out_size == 0) return;
    out[0] = '\0';
    if (log_count_lines == 0) {
        strncpy(out, "No logs yet\n", out_size - 1);
        out[out_size - 1] = '\0';
        return;
    }

    int start = (log_index - log_count_lines + LOG_LINES) % LOG_LINES;
    size_t off = 0;
    char esc[LOG_LINE_LEN * 2];
    for (int i = 0; i < log_count_lines; i++) {
        const char *ln = log_lines[(start + i) % LOG_LINES];
        html_escape(ln, esc, sizeof(esc));
        size_t need = strlen(esc);
        if (off + need + 2 >= out_size) break;
        memcpy(out + off, esc, need);
        off += need;
        out[off++] = '\n';
        out[off] = '\0';
    }
}

static int capture_vprintf(const char *fmt, va_list ap) {
    // Allocate buffer on heap to avoid stack overflow (don't use stack for large buffers!)
    char *tmp = malloc(LOG_LINE_LEN);
    if (!tmp) {
        // Fallback: skip capture and call original vprintf
        if (orig_vprintf) {
            return orig_vprintf(fmt, ap);
        } else {
            return vprintf(fmt, ap);
        }
    }

    va_list ap_copy;
    va_copy(ap_copy, ap);
    vsnprintf(tmp, LOG_LINE_LEN, fmt, ap_copy);
    va_end(ap_copy);

    // Split multi-line output into separate lines
    char *p = tmp;
    char *linebuf = malloc(LOG_LINE_LEN);
    if (!linebuf) {
        free(tmp);
        if (orig_vprintf) {
            return orig_vprintf(fmt, ap);
        } else {
            return vprintf(fmt, ap);
        }
    }

    while (*p) {
        char *nl = strchr(p, '\n');
        if (nl) {
            size_t len = nl - p;
            if (len >= LOG_LINE_LEN) len = LOG_LINE_LEN - 1;
            memcpy(linebuf, p, len);
            linebuf[len] = '\0';
            add_log_line(linebuf);
            p = nl + 1;
        } else {
            add_log_line(p);
            break;
        }
    }

    free(linebuf);
    free(tmp);

    if (orig_vprintf) {
        return orig_vprintf(fmt, ap);
    } else {
        return vprintf(fmt, ap);
    }
}
// ======= end log capture =======

int get_history_update_interval_s(void) {
    return history_update_interval_s;
}

void set_history_update_interval_s(int interval_s) {
    if (interval_s > 0 && interval_s < 3600) {  // Allow 1s to 1 hour
        history_update_interval_s = interval_s;
    }
}


void webpage_update(float a, float b, float c, float d, const char *e) {
    var_a = a;
    var_b = b;
    var_c = c;
    var_d = d;
    if (e != NULL) {
        strncpy(var_e, e, sizeof(var_e) - 1);
        var_e[sizeof(var_e) - 1] = '\0';
    }
    history_timestamps[history_index] = esp_timer_get_time();
    var_a_history[history_index] = a;
    var_b_history[history_index] = b;
    var_c_history[history_index] = c;
    var_d_history[history_index] = d;
    history_index = (history_index + 1) % HISTORY_SIZE;
    if (history_count < HISTORY_SIZE) {
        history_count++;
    }
}

// ============================================================================
// Helper Functions
// ============================================================================
static void format_bytes(size_t bytes, char *buf, size_t buf_size) {
    if (bytes >= 1024 * 1024) {
        snprintf(buf, buf_size, "%.2f MB", bytes / (1024.0 * 1024.0));
    } else if (bytes >= 1024) {
        snprintf(buf, buf_size, "%.1f KB", bytes / 1024.0);
    } else {
        snprintf(buf, buf_size, "%d B", (int)bytes);
    }
}

static const char* reset_reason_to_str(esp_reset_reason_t reason) {
    switch (reason) {
        case ESP_RST_POWERON:    return "Power-on";
        case ESP_RST_EXT:        return "External pin";
        case ESP_RST_SW:         return "Software reset";
        case ESP_RST_PANIC:      return "Panic/Exception";
        case ESP_RST_INT_WDT:    return "Interrupt watchdog";
        case ESP_RST_TASK_WDT:   return "Task watchdog";
        case ESP_RST_WDT:        return "Other watchdog";
        case ESP_RST_DEEPSLEEP:  return "Deep sleep wake";
        case ESP_RST_BROWNOUT:   return "Brownout";
        case ESP_RST_SDIO:       return "SDIO";
        case ESP_RST_USB:        return "USB peripheral";
        default:                 return "Unknown";
    }
}

// Simple delayed restart if you don't have delayed_restart.h
static void restart_task(void *arg) {
    vTaskDelay(pdMS_TO_TICKS(1000));  // Wait 1 second for response to be sent
    esp_restart();
}

static void delayed_restart_local(void) {
    xTaskCreate(restart_task, "restart", 2048, NULL, 5, NULL);
}

static void generate_chart_svg(char *buf, size_t buf_size, float *history_a, float *history_b, float *history_c, float *history_d,
                                int64_t *history_ts, int index, int count, int width, int height, int64_t current_time_us) {
    if (count == 0) {
        snprintf(buf, buf_size, "<svg width='%d' height='%d'><text x='10' y='%d' "
                 "fill='#888'>No data yet</text></svg>", width, height, height/2);
        return;
    }
    
    if (buf_size < 2560) {
        snprintf(buf, buf_size, "<svg width='%d' height='%d'><text x='10' y='%d' "
                 "fill='#888'>Buffer too small</text></svg>", width, height, height/2);
        return;
    }
    
    // Find min/max for scaling
    float min_val = 0.0f;   // 0%
    float max_val = 100.0f;   // 100%
    
    float range = max_val - min_val;
    if (range <= 0.0001f) range = 1.0f;
    int margin = 40;  // Increased for axis labels
    int chart_w = width - 2 * margin;
    int chart_h = height - 2 * margin;
    
    // Calculate time span: oldest point age in seconds
    int64_t oldest_idx = (index - count + HISTORY_SIZE) % HISTORY_SIZE;
    int64_t oldest_timestamp = history_ts[oldest_idx];
    int64_t time_span_us = current_time_us - oldest_timestamp;
    double time_span_hours = time_span_us / (3600.0 * 1e6);
    if (time_span_hours < 0.001) time_span_hours = 0.001;  // Avoid division by zero
    
    // Build points string for polyline (allocate on heap to avoid large stack usage)
    const int points_size = 1024;
    char *points_a = malloc(points_size);
    char *points_b = malloc(points_size);
    char *points_c = malloc(points_size);
    char *points_d = malloc(points_size);
    if (!points_a || !points_b || !points_c || !points_d) {
        if (points_a) free(points_a);
        if (points_b) free(points_b);
        if (points_c) free(points_c);
        if (points_d) free(points_d);
        snprintf(buf, buf_size, "<svg width='%d' height='%d'><text x='10' y='%d' "
                 "fill='#888'>No memory for chart</text></svg>", width, height, height/2);
        return;
    }
    int offset_a = 0;
    int offset_b = 0;
    int offset_c = 0;
    int offset_d = 0;

    points_a[0] = '\0'; points_b[0] = '\0'; points_c[0] = '\0'; points_d[0] = '\0';

    for (int i = 0; i < count; i++) {
        // Read from oldest to newest
        int idx = (index - count + i + HISTORY_SIZE) % HISTORY_SIZE;
        int64_t age_us = current_time_us - history_ts[idx];
        double age_hours = age_us / (3600.0 * 1e6);
        // x position: right-to-left (newest on right), proportional to age
        int x = margin + chart_w - (int)((age_hours / time_span_hours) * chart_w);
        int y = 0;

        int remaining, ret;

        y = margin + chart_h - (int)(((history_a[idx] - min_val) / range) * chart_h);
        remaining = points_size - offset_a;
        ret = snprintf(points_a + offset_a, remaining, "%d,%d ", x, y);
        if (ret < 0) break;
        if (ret >= remaining) { offset_a = points_size - 1; break; }
        offset_a += ret;

        y = margin + chart_h - (int)(((history_b[idx] - min_val) / range) * chart_h);
        remaining = points_size - offset_b;
        ret = snprintf(points_b + offset_b, remaining, "%d,%d ", x, y);
        if (ret < 0) break;
        if (ret >= remaining) { offset_b = points_size - 1; break; }
        offset_b += ret;

        y = margin + chart_h - (int)(((history_c[idx] - min_val) / range) * chart_h);
        remaining = points_size - offset_c;
        ret = snprintf(points_c + offset_c, remaining, "%d,%d ", x, y);
        if (ret < 0) break;
        if (ret >= remaining) { offset_c = points_size - 1; break; }
        offset_c += ret;

        y = margin + chart_h - (int)(((history_d[idx] - min_val) / range) * chart_h);
        remaining = points_size - offset_d;
        ret = snprintf(points_d + offset_d, remaining, "%d,%d ", x, y);
        if (ret < 0) break;
        if (ret >= remaining) { offset_d = points_size - 1; break; }
        offset_d += ret;
    }
    
    // Build SVG with grid
    char *svg_grid = malloc(2048);
    if (!svg_grid) {
        free(points_a); free(points_b); free(points_c); free(points_d);
        snprintf(buf, buf_size, "<svg width='%d' height='%d'></svg>", width, height);
        return;
    }
    int grid_offset = 0;
    
    // Vertical grid lines (5 divisions = every 20%)
    for (int i = 0; i <= 5; i++) {
        int y_pos = margin + (i * chart_h) / 5;
        grid_offset += snprintf(svg_grid + grid_offset, 2048 - grid_offset,
            "<line x1='%d' y1='%d' x2='%d' y2='%d' stroke='#444' stroke-width='1'/>"
            "<text x='5' y='%d' fill='#888' font-size='9'>%.0f%%</text>",
            margin, y_pos, margin + chart_w, y_pos,
            y_pos + 3, max_val - (i * (max_val - min_val) / 5));
    }
    
    // Horizontal grid lines (every hour)
    int max_hours = (int)time_span_hours + 1;
    for (int h = 0; h <= max_hours; h++) {
        double age = h;
        int x_pos = margin + chart_w - (int)((age / time_span_hours) * chart_w);
        if (x_pos >= margin && x_pos <= margin + chart_w) {
            grid_offset += snprintf(svg_grid + grid_offset, 2048 - grid_offset,
                "<line x1='%d' y1='%d' x2='%d' y2='%d' stroke='#444' stroke-width='1'/>"
                "<text x='%d' y='%d' fill='#888' font-size='9'>-%dh</text>",
                x_pos, margin, x_pos, margin + chart_h,
                x_pos - 10, margin + chart_h + 15, h);
        }
    }
    
    snprintf(buf, buf_size,
        "<svg width='%d' height='%d' style='background:#232323;border-radius:8px;'>"
        "%s"
        "<polyline points='%s' fill='none' stroke='#5778a4' stroke-width='2'/>"
        "<polyline points='%s' fill='none' stroke='#e49444' stroke-width='2'/>"
        "<polyline points='%s' fill='none' stroke='#d1615d' stroke-width='2'/>"
        "<polyline points='%s' fill='none' stroke='#6a9f58' stroke-width='2'/>"
        "</svg>",
        width, height, svg_grid, points_a, points_b, points_c, points_d);

    free(points_a); free(points_b); free(points_c); free(points_d); free(svg_grid);
}

// ============================================================================
// HTML Page (embedded as string)
// ============================================================================
static const char *html_page_template = 
"<!DOCTYPE html>"
"<html><head>"
"<meta charset='UTF-8'>"
"<meta name='viewport' content='width=device-width, initial-scale=1.0'>"
"<title>ESP32-C6 Dashboard</title>"
"<style>"
"* { box-sizing: border-box; }"
"body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif; "
"       margin: 0; padding: 20px; background: #131313; color: #eee; }"
"h1 { color: #cacaca; text-align: center; margin-bottom: 30px; }"
"h2 { color: #cacaca; margin-top: 0; border-bottom: 2px solid #cacaca; padding-bottom: 10px; }"
".container { max-width: 1100px; margin: 0 auto; }"
".card { background: #2c2c2c; padding: 20px; margin: 15px 0; border-radius: 12px; "
"        box-shadow: 0 4px 6px rgba(0,0,0,0.3); }"
".grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(200px, 1fr)); gap: 15px; }"
".stat { background: #323232; padding: 10px; border-radius: 8px; text-align: center; }"
".stat-value { font-size: 28px; font-weight: bold; color: #cacaca; }"
".stat-value-tank1 { font-size: 28px; font-weight: bold; color: #5778a4; }"
".stat-value-tank2 { font-size: 28px; font-weight: bold; color: #e49444; }"
".stat-value-temperature { font-size: 28px; font-weight: bold; color: #d1615d; }"
".stat-value-humidity { font-size: 28px; font-weight: bold; color: #6a9f58; }"
".stat-value-small { font-size: 18px; font-weight: bold; color: #cacaca; }"
".stat-label { font-size: 12px; color: #888; text-transform: uppercase; margin-top: 5px; }"
"table { width: 100%%; border-collapse: collapse; margin-top: 10px; }"
"th, td { padding: 10px; text-align: left; border-bottom: 1px solid #232323; }"
"th { background: #232323; color: #cacaca; }"
"tr:hover { background: #131313; }"
".btn { background: #e94560; color: white; border: none; padding: 12px 24px; "
"       border-radius: 6px; cursor: pointer; font-size: 14px; margin: 5px; "
"       transition: background 0.3s; }"
".btn:hover { background: #ff6b6b; }"
".btn-blue { background: #0077b6; }"
".btn-blue:hover { background: #00a8e8; }"
".file-input { margin: 10px 0; }"
"input[type='file'] { color: #eee; }"
".progress { width: 100%%; height: 20px; background: #0f3460; border-radius: 10px; "
"            overflow: hidden; margin: 10px 0; display: none; }"
".progress-bar { height: 100%%; background: linear-gradient(90deg, #00d9ff, #e94560); "
"                width: 0%%; transition: width 0.3s; }"
".status { padding: 10px; border-radius: 6px; margin: 10px 0; display: none; }"
".status-success { background: #1b4332; color: #95d5b2; }"
".status-error { background: #641220; color: #f8d7da; }"
"#refresh-indicator { position: fixed; top: 10px; right: 10px; padding: 5px 10px; "
"                     background: #0f3460; border-radius: 4px; font-size: 12px; }"
"</style>"
"</head><body>"
"<div class='container'>"
"<h1>LN<sub>2</sub> CdF dashboard</h1>"
"<div id='refresh-indicator'>Auto-refresh: <span id='countdown'>10</span>s</div>"

"<div class='card'>"
"<h2>Real time data</h2>"
"<div class='grid'>"
"<div class='stat'><div class='stat-value-tank1' id='var_a'>%.2f %%</div><div class='stat-label'>Tank 1</div></div>"
"<div class='stat'><div class='stat-value-tank2' id='var_b'>%.2f %%</div><div class='stat-label'>Tank 2</div></div>"
"<div class='stat'><div class='stat-value-temperature' id='var_c'>%.2f °C</div><div class='stat-label'>Temperature</div></div>"
"<div class='stat'><div class='stat-value-humidity' id='var_d'>%.2f %%</div><div class='stat-label'>Humidity</div></div>"
"<div class='stat'><div class='stat-value' id='var_e'>%s</div><div class='stat-label'>Current user</div></div>"
"</div>"
"</div>"

"<div class='card'>"
"<h2>History</h2>"
"<div style='margin-bottom: 10px;'>"
"<label>Update Interval (seconds): <input type='number' id='interval-input' min='1' max='3600' value='%d' style='width: 80px; padding: 5px;'></label>"
"<button class='btn btn-blue' onclick='updateInterval()' style='margin-left: 10px;'>Update</button>"
"</div>"
"<p style='color:#888; font-size: 12px;'>Current time (uptime): <strong>%s</strong></p>"
"%s"  // chart_svg goes here
"</div>"

"<div class='card'>"
"<h2>Diagnostics</h2>"
"<div class='grid'>"
"<div class='stat'><div class='stat-value-small'>%s</div><div class='stat-label'>Total Heap</div></div>"
"<div class='stat'><div class='stat-value-small'>%s</div><div class='stat-label'>Free Heap</div></div>"
"<div class='stat'><div class='stat-value-small'>%s</div><div class='stat-label'>Min Free Heap</div></div>"
"<div class='stat'><div class='stat-value-small'>%s</div><div class='stat-label'>Total Internal</div></div>"
"<div class='stat'><div class='stat-value-small'>%s</div><div class='stat-label'>Free Internal</div></div>"
"<div class='stat'><div class='stat-value-small'>%s</div><div class='stat-label'>Largest Free Block</div></div>"
"<div class='stat'><div class='stat-value-small'>%s</div><div class='stat-label'>Total DMA</div></div>"
"<div class='stat'><div class='stat-value-small'>%s</div><div class='stat-label'>Free DMA</div></div>"
"<div class='stat'><div class='stat-value-small'>%lu s</div><div class='stat-label'>Uptime</div></div>"
"<div class='stat'><div class='stat-value-small'>%ld dBm</div><div class='stat-label'>Wifi strength</div></div>"
"<div class='stat'><div class='stat-value-small'>%s</div><div class='stat-label'>Reset Reason</div></div>"
"<div class='stat'><div class='stat-value-small'>%s</div><div class='stat-label'>Wifi MAC address</div></div>"
"<div class='stat'><div class='stat-value-small'>%lu</div><div class='stat-label'>Active Tasks</div></div>"
"</div>"
"</div>"

"<div class='card'>"
"<h2>Logs</h2>"
"<pre id='logbox' style='height:220px; overflow:auto; background:#0b0b0b; color:#eee; padding:10px; border-radius:6px;'>%s</pre>"
"</div>"

"<div class='card'>"
"<h2>Running tasks</h2>"
"<table>"
"<tr><th>Task Name</th><th>Priority</th><th>Stack Free</th><th>State</th></tr>"
"%s"
"</table>"
"</div>"

"<div class='card'>"
"<h2>Firmware update (OTA)</h2>"
"<p>Select a .bin firmware file to upload:</p>"
"<div class='file-input'>"
"<input type='file' id='firmware' accept='.bin'>"
"</div>"
"<button class='btn btn-blue' onclick='uploadFirmware()'>Upload Firmware</button>"
"<div class='progress' id='progress'><div class='progress-bar' id='progress-bar'></div></div>"
"<div class='status' id='status'></div>"
"</div>"

"<div class='card'>"
"<h2>System control</h2>"
"<button class='btn' onclick='rebootDevice()'>Reboot Device</button>"
"</div>"

"</div>"

"<script>"
"let countdown = 10;"
"let refreshTimer = setInterval(function() {"
"    countdown--;"
"    document.getElementById('countdown').textContent = countdown;"
"    if (countdown <= 0) { location.reload(); }"
"}, 1000);"

"function uploadFirmware() {"
"    const fileInput = document.getElementById('firmware');"
"    const file = fileInput.files[0];"
"    if (!file) { alert('Please select a firmware file first'); return; }"
"    "
"    clearInterval(refreshTimer);"
"    document.getElementById('refresh-indicator').style.display = 'none';"
"    "
"    const xhr = new XMLHttpRequest();"
"    const progress = document.getElementById('progress');"
"    const progressBar = document.getElementById('progress-bar');"
"    const status = document.getElementById('status');"
"    "
"    progress.style.display = 'block';"
"    status.style.display = 'none';"
"    "
"    xhr.upload.addEventListener('progress', function(e) {"
"        if (e.lengthComputable) {"
"            const percent = (e.loaded / e.total) * 100;"
"            progressBar.style.width = percent + '%%';"
"        }"
"    });"
"    "
"    xhr.onreadystatechange = function() {"
"        if (xhr.readyState === 4) {"
"            status.style.display = 'block';"
"            if (xhr.status === 200) {"
"                status.className = 'status status-success';"
"                status.textContent = 'Firmware uploaded successfully! Device will reboot...';"
"            } else {"
"                status.className = 'status status-error';"
"                status.textContent = 'Upload failed: ' + xhr.responseText;"
"            }"
"        }"
"    };"
"    "
"    xhr.open('POST', '/ota', true);"
"    xhr.send(file);"
"}"

"function rebootDevice() {"
"    if (confirm('Are you sure you want to reboot the device?')) {"
"        fetch('/reboot', { method: 'POST' })"
"        .then(response => response.text())"
"        .then(data => {"
"            alert('Device is rebooting...');"
"            clearInterval(refreshTimer);"
"        })"
"        .catch(err => alert('Error: ' + err));"
"    }"
"}"

"function updateInterval() {"
"    const intervalInput = document.getElementById('interval-input');"
"    const interval = intervalInput.value;"
"    if (!interval || interval < 1 || interval > 3600) {"
"        alert('Please enter a value between 1 and 3600 seconds');"
"        return;"
"    }"
"    fetch('/set_interval', { method: 'POST', body: interval })"
"    .then(response => response.text())"
"    .then(data => {"
"        alert('Update interval set to ' + interval + ' seconds');"
"    })"
"    .catch(err => alert('Error: ' + err));"
"}"
""
"// Auto-refresh logs every 2 seconds"
"let logRefreshInterval = setInterval(function() {"
"    fetch('/logs')"
"    .then(response => response.text())"
"    .then(text => {"
"        const logbox = document.getElementById('logbox');"
"        logbox.textContent = text;"
"        logbox.scrollTop = logbox.scrollHeight;"
"    })"
"    .catch(err => console.log('Error fetching logs: ' + err));"
"}, 2000);"
"</script>"
"</body></html>";

// ============================================================================
// HTTP Handlers
// ============================================================================

#if configUSE_TRACE_FACILITY
static const char* task_state_to_str(eTaskState state) {
    switch(state) {
        case eRunning:   return "Running";
        case eReady:     return "Ready";
        case eBlocked:   return "Blocked";
        case eSuspended: return "Suspended";
        case eDeleted:   return "Deleted";
        default:         return "Unknown";
    }
}
#endif

static esp_err_t root_handler(httpd_req_t *req) {


    // Generate chart SVG (allocate on heap to avoid large stack usage)
    size_t chart_svg_size = 2560;
    char *chart_svg = malloc(chart_svg_size);
    if (chart_svg == NULL) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Memory allocation failed for chart");
        return ESP_FAIL;
    }
    int64_t current_time_us = esp_timer_get_time();
    generate_chart_svg(chart_svg, chart_svg_size, var_a_history, var_b_history, var_c_history, var_d_history,
                        history_timestamps, history_index, history_count, 1000+4*15, 200, current_time_us);  // width=1000 + margins for grid gaps

    // Gather system info
    size_t total_heap = heap_caps_get_total_size(MALLOC_CAP_8BIT);;
    size_t free_heap = esp_get_free_heap_size();
    size_t min_free_heap = esp_get_minimum_free_heap_size();
    size_t total_internal = heap_caps_get_total_size(MALLOC_CAP_INTERNAL);
    size_t free_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t largest_free_block = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    size_t free_dma = heap_caps_get_free_size(MALLOC_CAP_DMA);
    size_t total_dma = heap_caps_get_total_size(MALLOC_CAP_DMA);

    // Uptime
    int64_t uptime_us = esp_timer_get_time();  // Microseconds since boot
    uint32_t uptime_sec = uptime_us / 1000000;
    
    // Format current time (simplified: just show uptime as hours:minutes:seconds)
    char datetime_now_str[32];
    time_t datetime_now = time(NULL);
    strftime(datetime_now_str, sizeof(datetime_now_str), "%Y/%m/%d %H:%M:%S", localtime(&datetime_now) );

    // WiFi signal strength
    int8_t rssi = -99; // Default invalid RSSI
    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        rssi = ap_info.rssi;  // Signal strength in dBm
    }

    // Get reset reason
    esp_reset_reason_t reset_reason = esp_reset_reason();
    const char *reset_reason_str = reset_reason_to_str(reset_reason);

    // Get MAC address
    uint8_t mac[6];
    char mac_str[18];
    esp_wifi_get_mac(WIFI_IF_STA, mac);  // WIFI_IF_STA or WIFI_IF_AP
    snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
         mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    // Get number of tasks
    UBaseType_t task_count = uxTaskGetNumberOfTasks();
    
    char total_heap_str[32], free_heap_str[32], min_heap_str[32], total_internal_str[32], free_internal_str[32], largest_free_block_str[32], total_dma_str[32], free_dma_str[32];
    format_bytes(total_heap, total_heap_str, sizeof(total_heap_str));
    format_bytes(free_heap, free_heap_str, sizeof(free_heap_str));
    format_bytes(min_free_heap, min_heap_str, sizeof(min_heap_str));
    format_bytes(total_internal, total_internal_str, sizeof(total_internal_str));
    format_bytes(free_internal, free_internal_str, sizeof(free_internal_str));
    format_bytes(largest_free_block, largest_free_block_str, sizeof(largest_free_block_str));
    format_bytes(total_dma, total_dma_str, sizeof(total_dma_str));
    format_bytes(free_dma, free_dma_str, sizeof(free_dma_str));


    // Build task list HTML
        // Build task list HTML (allocate on heap)
        size_t task_list_size = 2048;
        char *task_list = malloc(task_list_size);
        if (task_list == NULL) {
            free(chart_svg);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Memory allocation failed for task list");
            return ESP_FAIL;
        }
        task_list[0] = '\0';
    
#if configUSE_TRACE_FACILITY
    int offset = 0;
    TaskStatus_t *task_array = pvPortMalloc(task_count * sizeof(TaskStatus_t));
    if (task_array != NULL) {
        UBaseType_t actual_count = uxTaskGetSystemState(task_array, task_count, NULL);
            for (UBaseType_t i = 0; i < actual_count && offset < (int)task_list_size - 150; i++) {
            char stack_str[32];
            format_bytes(task_array[i].usStackHighWaterMark * sizeof(StackType_t), 
                        stack_str, sizeof(stack_str));
                offset += snprintf(task_list + offset, task_list_size - offset,
                "<tr><td>%s</td><td>%lu</td><td>%s</td><td>%s</td></tr>",
                task_array[i].pcTaskName,
                (unsigned long)task_array[i].uxCurrentPriority,
                stack_str,
                task_state_to_str(task_array[i].eCurrentState));
        }
        vPortFree(task_array);
    }
#else
    snprintf(task_list, task_list_size,
        "<tr><td colspan='4' style='text-align:center;color:#888;'>"
        "Enable configUSE_TRACE_FACILITY in sdkconfig for task details</td></tr>");
#endif

    // Allocate buffer for complete HTML
    // Template is ~4KB, task_list up to 2KB, variables add ~200 bytes
    size_t html_size = 16384; // ideally, size should be strlen(html_page_template) + 2048 (but compiler throws an error);
    // The lines below should suppress format-truncation warning if strlen(html_page_template) is used but throws an error
    // #pragma GCC diagnostic push
    // #pragma GCC diagnostic ignored "-Wformat-truncation"
    // plus don't forget #pragma GCC diagnostic pop below before return

    char *html = malloc(html_size);
    if (html == NULL) {
            free(task_list);
            free(chart_svg);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Memory allocation failed");
        return ESP_FAIL;
    }
    
    // Prepare logs HTML
    size_t logs_html_size = 4096;
    char *logs_html = malloc(logs_html_size);
    if (logs_html == NULL) {
        free(task_list);
        free(chart_svg);
        free(html);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Memory allocation failed for logs");
        return ESP_FAIL;
    }
    get_logs_html(logs_html, logs_html_size);

    int written = snprintf(html, html_size, html_page_template,
        var_a, var_b, var_c, var_d, var_e, 
        get_history_update_interval_s(), datetime_now_str, chart_svg,
        total_heap_str, free_heap_str, min_heap_str, total_internal_str, free_internal_str, largest_free_block_str, total_dma_str, free_dma_str,
        (unsigned long)uptime_sec, (long)rssi, reset_reason_str, mac_str,
        (unsigned long)task_count,
        logs_html,
        task_list);
    
    if (written < 0 || written >= (int)html_size) {
        ESP_LOGE(TAG, "HTML buffer overflow, written %d bytes into %zu bytes", written, html_size);
        free(html);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "HTML generation failed");
        return ESP_FAIL;
    }
    
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
    free(html);
        free(task_list);
        free(chart_svg);
        free(logs_html);
    
    // #pragma GCC diagnostic pop // restore warnings
    return ESP_OK;
}

static esp_err_t ota_handler(httpd_req_t *req) {
    esp_ota_handle_t ota_handle = 0;
    const esp_partition_t *ota_partition = NULL;
    
    ESP_LOGI(TAG, "OTA update started, content length: %d", req->content_len);
    
    // Get next OTA partition
    ota_partition = esp_ota_get_next_update_partition(NULL);
    if (ota_partition == NULL) {
        ESP_LOGE(TAG, "No OTA partition found");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No OTA partition found");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Writing to partition: %s", ota_partition->label);
    
    // Begin OTA
    esp_err_t err = esp_ota_begin(ota_partition, OTA_SIZE_UNKNOWN, &ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA begin failed: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA begin failed");
        return ESP_FAIL;
    }
    
    // Receive and write firmware data
    char buffer[1024];
    int received;
    int total_received = 0;
    
    while ((received = httpd_req_recv(req, buffer, sizeof(buffer))) > 0) {
        err = esp_ota_write(ota_handle, buffer, received);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "OTA write failed: %s", esp_err_to_name(err));
            esp_ota_abort(ota_handle);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA write failed");
            return ESP_FAIL;
        }
        total_received += received;
    }
    
    if (received < 0) {
        ESP_LOGE(TAG, "Error receiving data");
        esp_ota_abort(ota_handle);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Error receiving data");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Total received: %d bytes", total_received);
    
    // Finalize OTA
    err = esp_ota_end(ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA end failed: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA end failed - invalid firmware?");
        return ESP_FAIL;
    }
    
    // Set new boot partition
    err = esp_ota_set_boot_partition(ota_partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Set boot partition failed: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Set boot partition failed");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "OTA successful, scheduling reboot");
    httpd_resp_sendstr(req, "OTA successful, rebooting...");
    
    // Reboot after short delay
    delayed_restart_local();
    
    return ESP_OK;
}

static esp_err_t reboot_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "Reboot requested");
    httpd_resp_sendstr(req, "Rebooting...");
    delayed_restart_local();
    return ESP_OK;
}

static esp_err_t logs_handler(httpd_req_t *req) {
    // Return current logs as plain text
    size_t logs_size = 4096;
    char *logs_text = malloc(logs_size);
    if (logs_text == NULL) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Memory allocation failed");
        return ESP_FAIL;
    }
    get_logs_html(logs_text, logs_size);
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_send(req, logs_text, HTTPD_RESP_USE_STRLEN);
    free(logs_text);
    return ESP_OK;
}

static esp_err_t update_interval_handler(httpd_req_t *req) {
    char buf[64] = "";
    if (httpd_req_recv(req, buf, sizeof(buf) - 1) > 0) {
        int interval_s = atoi(buf);
        set_history_update_interval_s(interval_s);
        ESP_LOGI(TAG, "History update interval set to %d seconds", interval_s);
    }
    httpd_resp_sendstr(req, "OK");
    return ESP_OK;
}

// ============================================================================
// Server Start/Stop
// ============================================================================

httpd_handle_t start_webserver(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 8192;  // Increase stack for HTML generation
    config.max_uri_handlers = 8;
    
    httpd_handle_t server = NULL;
    
    // Install log capture hook so ESP_LOG* messages are captured into the web UI
    // Now uses heap buffers instead of stack to avoid overflow
    orig_vprintf = esp_log_set_vprintf(capture_vprintf);

    if (httpd_start(&server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start web server");
        return NULL;
    }
    
    // Register URI handlers
    httpd_uri_t root_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = root_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &root_uri);
    
    httpd_uri_t ota_uri = {
        .uri = "/ota",
        .method = HTTP_POST,
        .handler = ota_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &ota_uri);
    
    httpd_uri_t reboot_uri = {
        .uri = "/reboot",
        .method = HTTP_POST,
        .handler = reboot_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &reboot_uri);
    
    httpd_uri_t interval_uri = {
        .uri = "/set_interval",
        .method = HTTP_POST,
        .handler = update_interval_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &interval_uri);
    
    httpd_uri_t logs_uri = {
        .uri = "/logs",
        .method = HTTP_GET,
        .handler = logs_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &logs_uri);
    
    ESP_LOGI(TAG, "Web server started successfully");
    return server;
}

void stop_webserver(httpd_handle_t server) {
    if (server != NULL) {
        httpd_stop(server);
        ESP_LOGI(TAG, "Web server stopped");
    }
}