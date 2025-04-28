# Liquid nitrogen tanks: monitoring level gauges with ESP32

## Purpose

Remotely monitor the level of two liquid nitrogen tanks

Uses Espressif ESP-IDF framwork, not Arduino. Arduino libraries may be added later as ESP-IDF components, if needed.

## Requirements

- ESP32-C6 N16 from Aliexpress
- Python installed via Miniforge installer
- Visual Studio Code with C/C++ Extension pack
- ESP-IDF comonents and tools installed via ESP-IDF VSCode extension (Miniforge python install was correctly detected)

## Roadmap

- [x] Connect to wifi hotspot
    - [x] SSID/password is stored in `secret.h` (template `secret_template.h` in repo)
    - [ ] multi-Wifi
    - [ ] Wifi simple connect (DPP-connect), preferably using QRcode on a display rather than on serial terminal
    - [ ] Web Wifi manager
- [x] Synchronise time with NTP server
- [x] Read two capacitive level gauges (4-20 mA current loop)
    - [x] using internal ADC for the moment
    - [ ] external ADC modules may be used if more accuracy is needed
- [x] Automatically fill-up a Google Sheet for logging level
    - [x] periodic sampling (every 15 min ?)
    - [ ] threshold based (each time level is down by 2% ?)
- [ ] Automatically send email when level goes below 30%
    - [ ] via Google Sheet
    - [ ] or in the ESP32 ?
- [ ] Implement login feature to add user info to Google Sheet
    - [ ] via keypad (4 digits? -> 24 combinations -> max 24 users or a few users and many invalid codes)
    - [ ] via clickable rotative encoder for selection and TFT display
    - [ ] via smartphone 
- [ ] Add interlock: withdrawal of LN2 only possible when user identified
    - [ ] Activate existing electrovalve if logged-in
- [ ] Read ambient temperature
    - [ ] using DS18B20 1-wire sensor
    - [ ] using PT-100 or thermocouple and MAX31865 on SPI/I2C
- [ ] Read ambient humidity
    - [ ] using SHT-xxx I2C sensor
- [ ] Display info on TFT (eg. ST7789)
- [ ] Read tank pressures
- [ ] Watch ambient oxygen
    - [ ] Monitor existing low-O2 alarm (binary)
    - [ ] Read existing O2 sensor (easy if it is also a 4-20 mA current loop)
    - [ ] Add O2 sensor
