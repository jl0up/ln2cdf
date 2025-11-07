# Liquid nitrogen tanks: monitoring level gauges with ESP32

## Purpose

Remotely monitor the level of two liquid nitrogen tanks + ambient temperature & humidity, with user identification feature

Uses Espressif ESP-IDF framwork, not Arduino. Arduino libraries may be added later as ESP-IDF components, if needed.

## Requirements

- ESP32-C6-DevKitC-1 from Espressif (official dev board bought from Mouser)
- Python installed via Miniforge installer
- Visual Studio Code with C/C++ Extension pack
- ESP-IDF components and tools installed via ESP-IDF VSCode extension (Miniforge python install was correctly detected)
- tested versions: 
```
  idf:
    version: '>=4.1.0'
  lvgl/lvgl: 9.2.2
  espressif/aht20: ^2.0.0
```

- keypad RS Stock No.: 146-014, Mfr. Part No.: ECO.12150.06
- SPI TFT screen: 1.9", 170x320, ST7789 chip (from Aliexpress, but probably similar to Adafruit 5394)
- SHT20-based encased temperature & humidity sensor: ASAIR AM2315C, sold by Adafruit as Product ID: 1293

## Roadmap

- [x] Connect to wifi hotspot
    - [x] SSID/password is stored in `secret.h` (template `secret.h.example` in Github repo)
    - [x] IP is displayed on screen
    - [ ] multi-Wifi
    - [ ] Wifi simple connect (DPP-connect), preferably using QRcode on a display rather than on serial terminal
    - [ ] Web Wifi manager
- [x] Synchronise time with NTP server
- [x] Read two capacitive level gauges (4-20 mA current loop)
    - [x] using internal ADC for the moment
    - [ ] external, floating ADC modules may be used if more accuracy is needed (doesn't seem so) or if having a common ground for both sensors is a problem (doesn't seem to be the case)
- [x] Automatically fill-up a Google Sheet for logging level
    - [ ] periodic sampling (every 15 min ?)
    - [x] threshold based (each time level is down by 1.1% for tank 1, 0.5% for tank 2 because first sensor is noisier)
    - [x] upon user change
- [ ] Automatically send email when level goes below 30%
    - [ ] via Google Sheet
    - [ ] or in the ESP32 ?
- [x] Implement login feature to add user info to Google Sheet
    - [x] via keypad
    - [ ] via clickable rotative encoder for selection and TFT display
    - [ ] via smartphone 
- [ ] Add interlock: withdrawal of LN2 only possible when user identified
    - [ ] Activate existing electrovalve if logged-in
- [x] Read ambient temperature
    - [ ] using DS18B20 1-wire sensor
    - [ ] using PT-100 or thermocouple and MAX31865 on SPI/I2C
    - [x] using AHT20 I2C sensor
- [x] Read ambient humidity
    - [x] using AHT20 I2C sensor
- [x] Display info on TFT (eg. ST7789)
- [ ] Read tank pressures
- [ ] Watch ambient oxygen
    - [ ] Monitor existing low-O2 alarm (binary)
    - [ ] Read existing O2 sensor (easy if it is also a 4-20 mA current loop)
    - [ ] Add O2 sensor
- [ ] Easy firmware update
    - [ ] OTA (over the air) with automatic pull from Github branch on boot
    - [ ] idem but pull update by pressing a button
    - [x] OTA update by pushing firmware to IP (`curl -X POST -H "Content-Type: application/octet-stream" --data-binary "@ln2cdf.bin" http://192.168.120.91/ota`) (see IP on screen)

## Log-in via keypad

- The following users are hardcoded in `keypad.c`. Recompile if changes are needed.
```c
#define DEFAULT_PASSWORD "0000" // password of defaut identifier
#define DEFAULT_IDENTIFIER "None" // default identifier for logging out ("0000" or "#")

static const password_entry_t password_table[] = {
    {DEFAULT_PASSWORD, DEFAULT_IDENTIFIER},
    {"1064", "Admin"},
    {"1110", "SB"},
    {"2022", "PQ"},
    {"3303", "CPB"},
    {"0444", "CSE"},
    {"5050", "LAM"},
    {"0606", "UAR1"},
    {"7007", "UAR2"},    
    {"8800", "A&B"},    
    {"0990", "LKB"},    
    {"1111", "Guest"}
};
```
- `#` button logs out (returns to default identifier "None")
- `*` cancels current password typing (or wait a few seconds before retrying)


## `secret.h` file

```c
/* Wifi configuration */
#define EXAMPLE_ESP_WIFI_SSID      "my_ssid"
#define EXAMPLE_ESP_WIFI_PASS      "my_password"

 /* Google Script Configuration */
 #define GOOGLE_SCRIPT_ID "AKfyc****************7vB9pb2URqEZK" // ID from the deployed Apps Script (see README/Google Sheet section)
```

## Google Sheet

### Sheet `LN2 monitor`

- owned by salle.blanche.cdf google account
- address: `https://docs.google.com/spreadsheets/d/1Qrxl4_TUl-HDLBYXdbTznASOz_5FRtc-cUEeFFwspNo/edit?usp=sharing`
- tab `LN2 logs` contains useful logs
- tab  `Plots` contains the corresponding plots 
- tab `rawdata` contains data uploaded by the microcontroler
- tab `params` contains some parameters used to convert ADC voltage to sensor current then percentage values

### App Script `ln2-monitor-script`

A copy of the script is on Github: `google_script.js`.

This script must be "deployed" (as defined by Google) and the address of the script must match the `GOOGLE_SCRIPT_ID` in file `secret.h` (not on Github)

```js
function doGet(e) {
  // Get the active spreadsheet and the data sheet
  var spreadsheet = SpreadsheetApp.getActiveSpreadsheet();
  var sheet = spreadsheet.getSheetByName("rawdata");
  
  // Create the sheet if it doesn't exist
  if (!sheet) {
    sheet = spreadsheet.insertSheet("rawdata");
    sheet.appendRow([ "Timestamp (UTC epoch)", 
                      "Voltage 1 (V)",
                      "Voltage 2 (V)",
                      "ADC value 1",
                      "ADC value 2",
                      "User",
                      "Temperature (°C)",
                      "Humidity (%)",
                      "Current 1 (mA)",
                      "Current 2 (mA)"
                      ]);
    var cell = sheet.getRange("I2");
    cell.setFormula('=ARRAYFORMULA(IF(B2:B<>"", B2:B*1000/params!$A$2, ))');
    var cell = sheet.getRange("J2");
    cell.setFormula('=ARRAYFORMULA(IF(C2:C<>"", C2:C*1000/params!$A$2, ))');
}
  
  // Get parameters from the request
  var timestamp = e.parameter.timestamp || new Date().getTime();
  var voltage0 = e.parameter.voltage0 || "No Value";
  var voltage1 = e.parameter.voltage1 || "No Value";
  var rawValue0 = e.parameter.raw_value0 || "No Value";
  var rawValue1 = e.parameter.raw_value1 || "No Value";
  var user = e.parameter.user || "No Value";
  var temperature = e.parameter.temperature || "No Value";
  var humidity = e.parameter.humidity || "No Value";
  
  // Log the incoming data
  Logger.log("Data received - Timestamp: " + timestamp + ", Voltage 0: " + voltage0 + ", Voltage 1: " + voltage1 + ", Raw Value 0: " + rawValue0 + ", Raw Value 1: " + rawValue1 + ", User: " + user + ", Temperature: " + temperature + ", Humidity: " + humidity);
  
  try {
    // Append the data to the sheet
    sheet.appendRow([timestamp, voltage0, voltage1 , rawValue0, rawValue1, user, temperature, humidity]);
    
    // Return success response
    return ContentService.createTextOutput("Success: Data added to Google Sheet")
      .setMimeType(ContentService.MimeType.TEXT);
      
  } catch (error) {
    // Return error response
    return ContentService.createTextOutput("Error: " + error.message)
      .setMimeType(ContentService.MimeType.TEXT);
  }
}
```
