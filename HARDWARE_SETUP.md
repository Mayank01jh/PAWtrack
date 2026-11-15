# Pawtrack Hardware Setup & Flashing Guide

This guide walks you through flashing the ESP32 / ESP32-CAM firmware, wiring the sensors, and connecting the physical collar to the Pawtrack software dashboard.

---

## 1. Required Components

1. **Microcontroller:** ESP32-CAM (AI-Thinker) or standard ESP32 DevKit WROOM-32
2. **Temperature Sensor:** MAX30205 Human/Canine Body Temperature Sensor (I2C)
3. **GPS / GNSS Module:** Ai-Thinker GP-02 (or NEO-6M / ATGM336H)
4. **Camera:** OV2640 2MP Camera Sensor (included with ESP32-CAM)
5. **Battery:** 3.7V 1200mAh LiPo Battery with TP4056 USB-C Charger & 100k/100k voltage divider resistors
6. **Programmer (for ESP32-CAM):** FTDI USB-to-TTL Adapter or ESP32-CAM MB Shield

---

## 2. Wiring Diagram

### 2.1 AI-Thinker ESP32-CAM Wiring

| Component | Component Pin | ESP32-CAM Pin | Notes |
| :--- | :--- | :--- | :--- |
| **Power** | LiPo Battery (+) | TP4056 OUT+ | Battery connected through TP4056 |
| **Power** | TP4056 OUT+ | **5V** or **3V3** | Power rail |
| **Ground** | All Modules GND | **GND** | Common ground for all sensors |
| **MAX30205** | **VCC** | **3V3** | 3.3V Operating Voltage |
| **MAX30205** | **GND** | **GND** | Ground |
| **MAX30205** | **SDA** | **GPIO 14** | Software/Hardware I2C Data |
| **MAX30205** | **SCL** | **GPIO 15** | Software/Hardware I2C Clock |
| **GP-02 GPS** | **VCC** | **3V3** or **5V** | Depending on GPS breakout board |
| **GP-02 GPS** | **GND** | **GND** | Ground |
| **GP-02 GPS** | **TX** | **GPIO 13** | ESP32 Hardware Serial RX |
| **GP-02 GPS** | **RX** | **GPIO 12** | ESP32 Hardware Serial TX |
| **Battery Divider** | Midpoint of 100k/100k | **GPIO 33** | ADC Battery Voltage Monitor |

> **Flashing Mode Note for ESP32-CAM:**
> Connect **GPIO 0 to GND** before powering on or pressing RESET to enter flash/bootloader mode. Disconnect GPIO 0 from GND after uploading to run the firmware.

---

## 3. Arduino IDE Setup

1. **Install Arduino IDE:** (Version 2.x recommended) from [arduino.cc](https://www.arduino.cc/).
2. **Add ESP32 Board Support:**
   - In Arduino IDE, go to **File** -> **Preferences**.
   - In *Additional Boards Manager URLs*, add:
     ```text
     https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
     ```
   - Go to **Tools** -> **Board** -> **Boards Manager**.
   - Search for **esp32** by *Espressif Systems* and click **Install**.
3. **Select Board:**
   - If using ESP32-CAM: Select **AI Thinker ESP32-CAM**.
     - CPU Frequency: **240MHz**
     - Flash Frequency: **80MHz**
     - Flash Mode: **QIO**
     - Partition Scheme: **Huge APP (3MB No OTA/1MB SPIFFS)**
     - PSRAM: **Enabled**
   - If using ESP32 DevKit: Select **ESP32 Dev Module**.

---

## 4. Configuring & Flashing Firmware

1. Open the sketch in Arduino IDE:
   `firmware/pawtrack_esp32_collar/pawtrack_esp32_collar.ino`
2. Update Wi-Fi settings on lines 25–26:
   ```cpp
   const char* WIFI_SSID     = "YOUR_HOME_WIFI_OR_PHONE_HOTSPOT";
   const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
   ```
3. Connect your programmer / USB cable to your PC.
4. Select the matching **Port** under **Tools** -> **Port** (e.g. `COM3` on Windows).
5. (For ESP32-CAM without shield) Ensure **GPIO 0 is connected to GND**, then click **Upload**.
6. When upload is complete:
   - Disconnect **GPIO 0 from GND**.
   - Press the **RESET** button on the ESP32.
   - Open **Tools** -> **Serial Monitor** at **115200 baud**.
7. Note the printed IP address:
   - **Station Mode (Home Wi-Fi/Hotspot):** e.g., `192.168.1.105`
   - **Access Point Mode (Collar Hotspot):** `192.168.4.1`

---

## 5. Connecting to the Web Dashboard

1. Open `Pawtrack — Smart Dog Collar.html` in your web browser (Chrome, Edge, Firefox, or Safari).
2. Go to **Dashboard** (`📊` icon on any registered dog).
3. In the **Hardware Connection** box:
   - Enter your Collar IP address (e.g., `192.168.1.105` or `192.168.4.1`).
   - Click **Connect Collar**.
4. The dashboard will instantly transition from simulation to live hardware:
   - Real **MAX30205** body temperature values stream into the chart.
   - Real **GP-02** GPS coordinates display with a direct Google Maps link.
   - Real **Battery %** updates continuously.
   - Click **Capture Photo** to receive a live snapshot directly from the collar's OV2640 camera!

