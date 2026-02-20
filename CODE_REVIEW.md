# ESP32-C6 Code Review - Major Issues Found

## CRITICAL ISSUES (Production-blocking)

### 1. ⛔ **Race Condition on `last_identifier` Global Variable**
**Severity:** CRITICAL - Data corruption risk  
**Location:** display.c, keypad.c, main.c  

**Problem:**
- `last_identifier` is accessed/modified from multiple tasks without proper synchronization
- display.c calls `strcpy(last_identifier, identifier)` **without holding keypad_mutex** (lines 308, 321)
- main.c reads `last_identifier` in main loop **without any mutex** (lines 369, 396, 410, 432)
- keypad_mutex exists but is used in keypad.c, not coordinated with display.c modifications

**Risk:** 
- String corruption if display task writes while main task reads
- Undefined behavior with concurrent access to same buffer
- May work fine for weeks then suddenly fail randomly

**Fix Required:**
- Protect all `last_identifier` accesses with keypad_mutex in display.c
- Protect all reads in main.c with keypad_mutex
- Or create a separate dedicated mutex for `last_identifier`

**Example fix for display.c:**
```c
void display_show_login_result(const char *identifier, bool success) {
    // ... existing lvgl_mutex code ...
    
    // NEW: Take keypad_mutex before modifying last_identifier
    if (xSemaphoreTake(keypad_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        strncpy(last_identifier, identifier, MAX_IDENTIFIER_LENGTH);
        last_identifier[MAX_IDENTIFIER_LENGTH] = '\0';
        xSemaphoreGive(keypad_mutex);
    }
}
```

---

### 2. ⛔ **NULL Pointer Dereference in display_show_login_result()**
**Severity:** CRITICAL - Crash on login  
**Location:** display.c, line 330

**Problem:**
```c
if (login_result_timer == NULL) {
    login_result_timer = xTimerCreate("login_timer", pdMS_TO_TICKS(1000), 
                                      pdFALSE, NULL, login_result_timer_callback);
}
xTimerReset(login_result_timer, 0);  // <-- CRASHES if xTimerCreate returned NULL
```

If `xTimerCreate()` fails (out of memory?), it returns NULL, and then `xTimerReset()` dereferences NULL.

**Fix Required:**
```c
if (login_result_timer == NULL) {
    login_result_timer = xTimerCreate("login_timer", pdMS_TO_TICKS(1000), 
                                      pdFALSE, NULL, login_result_timer_callback);
}
if (login_result_timer != NULL) {
    xTimerReset(login_result_timer, 0);
} else {
    ESP_LOGE(TAG, "Failed to create/reset login result timer");
}
```

---

### 3. ⛔ **Unsafe String Operations on `last_identifier`**
**Severity:** HIGH - Buffer overflow risk  
**Location:** display.c, lines 308 and 321

**Problem:**
```c
strcpy(last_identifier, identifier);  // NO LENGTH CHECK!
```

If `identifier` exceeds `MAX_IDENTIFIER_LENGTH`, this will overflow the buffer.
The function receives `identifier` parameter from keypad without validation.

**Fix Required:**
Replace both instances with:
```c
strncpy(last_identifier, identifier, MAX_IDENTIFIER_LENGTH);
last_identifier[MAX_IDENTIFIER_LENGTH] = '\0';
```

---

## HIGH-PRIORITY ISSUES

### 4. ⚠️  **AHT20/I2C Error Path Returns from app_main During Initialization**
**Severity:** HIGH - Silent failure, device unresponsive  
**Location:** main.c, lines 231-237

**Problem:**
```c
ret = aht20_sensor_init();
if (ret != ESP_OK) {
    ESP_LOGE(TAG, "AHT20 sensor initialization failed");
    cleanup_resources();
    return;  // <-- Returns from app_main!
}
```

If AHT20 init fails, app_main returns after WDT is already armed (line 157). 
The main loop never starts, tasks aren't created, but WDT is running and will trigger a reboot in 120 seconds.

**However:** This **is acceptable for your use case** because:
- WDT will detect the hang and reboot
- OTA mode will be available after reboot
- The device can still receive OTA updates

**Recommendation:** If you want faster boot to OTA mode in failure cases, reduce WDT timeout or add explicit fallback to OTA-only mode here.

---

### 5. ⚠️  **Blocking Network Operations in Main Loop**
**Severity:** MEDIUM - WDT trip risk  
**Location:** main.c, main loop, line ~410

**Problem:**
```c
send_to_google_script(...)  // Can block for 30+ seconds with retries
```

The Google Sheets upload has:
- 10-second timeout per request
- 2 retries with 1-second delay between
- Total potential: ~30 seconds of blocking

With 120-second WDT timeout, this is safe but leaves only 90 seconds for all other tasks.
If network sluggish, could approach the limit.

**Risk:** If network completely hangs or has longer latency, WDT triggers.

**Recommendation for production:**
- Consider running Google Sheets upload in a separate task instead of main loop
- Increase WDT timeout to 180+ seconds if network is unreliable
- Or add more explicit timeout handling

---

## MEDIUM-PRIORITY ISSUES

### 6. ℹ️  **Inconsistent Null Termination in strncpy Usage**
**Severity:** MEDIUM - String handling inconsistency  
**Location:** main.c, line 417

**Problem:**
```c
strncpy(last_identifier_uploaded, "", sizeof(last_identifier_uploaded));
```

This is confusing. If you want to "clear" the string, use:
```c
last_identifier_uploaded[0] = '\0';
```

Or better yet, define a constant:
```c
#define CLEAR_IDENTIFIER last_identifier_uploaded[0] = '\0'
```

**Current code works** but is unclear.

---

### 7. ℹ️  **Missing Null Termination Guarantee in strncpy**
**Severity:** MEDIUM  
**Location:** main.c, line 426

**Problem:**
```c
strncpy(last_identifier_uploaded, last_identifier, sizeof(last_identifier_uploaded));
```

If `last_identifier` is exactly `MAX_IDENTIFIER_LENGTH` bytes, `strncpy` won't add null terminator.

**Fix:**
```c
strncpy(last_identifier_uploaded, last_identifier, sizeof(last_identifier_uploaded) - 1);
last_identifier_uploaded[sizeof(last_identifier_uploaded) - 1] = '\0';
```

Or use safer function:
```c
snprintf(last_identifier_uploaded, sizeof(last_identifier_uploaded), "%s", last_identifier);
```

---

## LOWER-PRIORITY ISSUES

### 8. ℹ️  **Display Task Not Monitored by WDT**
**Severity:** LOW - Acceptable per requirements  
**Location:** main.c, line 157

The code adds only the main task to WDT:
```c
esp_task_wdt_add(NULL);  // Only app_main task is monitored
```

Display and keypad tasks are not monitored. If display task hangs, the system keeps running but display freezes.

**Per your requirements:** "If something becomes unresponsive, it's not a big problem as long as ESP32 can reboot without looping." This is acceptable.

**Optional improvement:** Add other tasks to WDT:
```c
TaskHandle_t display_task_handle;
xTaskCreate(lcd_lvgl_task, "lvgl_task", 8192, NULL, 6, &display_task_handle);
esp_task_wdt_add(display_task_handle);
```

---

### 9. ℹ️  **Potential Memory Issues with Log Buffer in webserver.c**
**Severity:** LOW - Works but inefficient  
**Location:** webserver.c, logs_handler

Fixed allocation of 4096 bytes for logs regardless of actual size. Fine for small logs, but inefficient.

---

## SUMMARY TABLE

| Issue | Severity | File | Line | Impact | WDT Recoverable |
|-------|----------|------|------|--------|-----------------|
| Race condition on last_identifier | CRITICAL | display.c, main.c | 308,321,369 | Data corruption | ❌ May loop if corrupted |
| NULL timer dereference | CRITICAL | display.c | 330 | Crash on login | ✅ Yes (WDT reboot) |
| strcpy overflow | HIGH | display.c | 308,321 | Buffer overflow | ✅ Yes (WDT reboot) |
| AHT20 init failure handling | HIGH | main.c | 237 | Silent failure | ✅ Yes (WDT reboot) |
| Blocking network operation | MEDIUM | main.c | 410 | WDT trip risk | ✅ Yes |
| String null termination | MEDIUM | main.c | 426 | Potential issue | ✅ Yes |
| Display task not monitored | LOW | main.c | 157 | Display freeze | ✅ Yes |

---

## RECOMMENDED FIXES (Priority Order)

1. **FIRST:** Fix race condition on `last_identifier` - most dangerous
2. **SECOND:** Fix NULL timer dereference in display.c
3. **THIRD:** Fix strcpy calls with strncpy + null termination
4. **FOURTH:** Fix string handling for last_identifier_uploaded
5. **OPTIONAL:** Monitor display task with WDT
6. **OPTIONAL:** Consider separate task for Google Sheets upload

All fixes are relatively straightforward and don't require architecture changes.
