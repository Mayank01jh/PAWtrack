/*
 * ======================================================================================
 * Pawtrack — Smart Dog Collar Firmware
 * Target Hardware: ESP32 / ESP32-CAM (AI-Thinker)
 * Sensors: MAX30205 (Body Temp, I2C), Ai-Thinker GP-02 (GPS/GNSS, UART), Battery ADC
 * Camera: OV2640 (onboard ESP32-CAM)
 * 
 * Features:
 *  - Dual Wi-Fi: Station Mode (connects to router/hotspot) + AP Mode (Pawtrack-Collar-AP)
 *  - REST API with CORS headers enabled for direct browser communication
 *  - Real-time JSON telemetry endpoint (/api/telemetry)
 *  - On-demand OV2640 Camera Snapshot endpoint (/capture)
 *  - Emergency SOS Trigger (/api/sos)
 *  - Serial Debug JSON Output for Arduino Serial Monitor
 * ======================================================================================
 */

#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include "esp_camera.h"

// ======================== HARDWARE PROFILE SELECTION ========================
// Set to 1 if using AI-Thinker ESP32-CAM; set to 0 if using standard ESP32 DevKit (WROOM)
#define BOARD_IS_ESP32_CAM 1

// ======================== USER WI-FI CONFIGURATION ========================
// Enter your local Wi-Fi / Mobile Hotspot credentials here:
const char* WIFI_SSID     = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

// Fallback Access Point (AP) credentials (used when Wi-Fi is unavailable / in field):
const char* AP_SSID       = "Pawtrack-Collar-01";
const char* AP_PASSWORD   = "12345678"; // At least 8 characters
IPAddress AP_IP(192, 168, 4, 1);
IPAddress AP_GATEWAY(192, 168, 4, 1);
IPAddress AP_SUBNET(255, 255, 255, 0);

// Collar Identity
const char* COLLAR_ID     = "COLLAR-01";

// ======================== PIN ASSIGNMENTS ========================
#if BOARD_IS_ESP32_CAM
  // AI-Thinker ESP32-CAM Pin Configuration
  #define I2C_SDA_PIN     14    // MAX30205 SDA (Wire)
  #define I2C_SCL_PIN     15    // MAX30205 SCL (Wire)
  #define GPS_RX_PIN      13    // ESP32 RX <- GP-02 TX
  #define GPS_TX_PIN      12    // ESP32 TX -> GP-02 RX
  #define BATT_ADC_PIN    33    // Battery Voltage Divider ADC
  #define LED_PIN         33    // Small onboard red LED (active LOW)
  #define FLASH_LED_PIN   4     // High-power flash LED

  // Camera Pin definitions for AI-Thinker model
  #define PWDN_GPIO_NUM     32
  #define RESET_GPIO_NUM    -1
  #define XCLK_GPIO_NUM      0
  #define SIOD_GPIO_NUM     26
  #define SIOC_GPIO_NUM     27
  #define Y9_GPIO_NUM       35
  #define Y8_GPIO_NUM       34
  #define Y7_GPIO_NUM       39
  #define Y6_GPIO_NUM       36
  #define Y5_GPIO_NUM       21
  #define Y4_GPIO_NUM       19
  #define Y3_GPIO_NUM       18
  #define Y2_GPIO_NUM        5
  #define VSYNC_GPIO_NUM    25
  #define HREF_GPIO_NUM     23
  #define PCLK_GPIO_NUM     22
#else
  // Standard ESP32 DevKit WROOM-32 Pin Configuration
  #define I2C_SDA_PIN     21
  #define I2C_SCL_PIN     22
  #define GPS_RX_PIN      16
  #define GPS_TX_PIN      17
  #define BATT_ADC_PIN    34
  #define LED_PIN          2
  #define FLASH_LED_PIN   -1
#endif

// MAX30205 I2C Address (default is 0x48 when A0, A1, A2 are connected to GND)
#define MAX30205_I2C_ADDR 0x48
#define MAX30205_TEMP_REG 0x00
#define MAX30205_CONF_REG 0x01

// Global WebServer instance on port 80
WebServer server(80);

// Telemetry State
float currentTemperature = 38.6;
int   currentBatteryPct  = 85;
float currentLatitude    = 28.45950;
float currentLongitude   = 77.02660;
bool  gpsFixed           = false;
bool  max30205Found      = false;
bool  cameraInitialized  = false;
bool  sosTriggered       = false;
unsigned long sosUntil   = 0;
unsigned long lastSensorRead = 0;

// Hardware Serial for GP-02 GPS
HardwareSerial gpsSerial(1);

// ======================== SENSOR DRIVERS ========================

// Initialize MAX30205 Human/Canine Body Temperature Sensor
void initMAX30205() {
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN, 100000);
  Wire.beginTransmission(MAX30205_I2C_ADDR);
  byte error = Wire.endTransmission();
  if (error == 0) {
    max30205Found = true;
    // Set to continuous conversion mode
    Wire.beginTransmission(MAX30205_I2C_ADDR);
    Wire.write(MAX30205_CONF_REG);
    Wire.write(0x00);
    Wire.endTransmission();
    Serial.println("[MAX30205] Sensor detected and initialized.");
  } else {
    max30205Found = false;
    Serial.print("[MAX30205] Warning: Sensor not found at 0x48 (I2C error code ");
    Serial.print(error);
    Serial.println("). Using fallback/simulation.");
  }
}

// Read raw temperature from MAX30205
float readMAX30205() {
  if (!max30205Found) {
    // Return realistic canine temperature if sensor disconnected
    return 38.4 + ((random(-20, 20)) / 100.0);
  }
  Wire.beginTransmission(MAX30205_I2C_ADDR);
  Wire.write(MAX30205_TEMP_REG);
  if (Wire.endTransmission() != 0) return currentTemperature;

  Wire.requestFrom((uint8_t)MAX30205_I2C_ADDR, (uint8_t)2);
  if (Wire.available() >= 2) {
    int8_t msb = Wire.read();
    uint8_t lsb = Wire.read();
    int16_t raw = (msb << 8) | lsb;
    float temp = raw * 0.00390625; // raw * (1 / 256)
    return temp;
  }
  return currentTemperature;
}

// Read Battery Level from ADC (assumes 100k / 100k voltage divider on LiPo)
int readBatteryLevel() {
  int rawADC = analogRead(BATT_ADC_PIN);
  // ESP32 ADC: 0-4095 over ~3.3V. With a 2:1 divider, 4.2V battery produces ~2.1V at pin.
  float pinVoltage = (rawADC / 4095.0) * 3.3;
  float battVoltage = pinVoltage * 2.0; // 2x voltage divider

  // Calculate percentage: 3.2V (empty) to 4.2V (full)
  int pct = (int)(((battVoltage - 3.2) / (4.2 - 3.2)) * 100.0);
  if (pct > 100) pct = 100;
  if (pct < 0)   pct = 0;
  return pct;
}

// Simple NMEA Sentence Parser for GP-02 GPS
void processGPS() {
  while (gpsSerial.available() > 0) {
    String line = gpsSerial.readStringUntil('\n');
    line.trim();

    // Check for GPGGA or GNRMC sentences
    if (line.startsWith("$GPGGA") || line.startsWith("$GNGGA")) {
      // Example: $GPGGA,123519,2827.5700,N,07701.5960,E,1,08,0.9,545.4,M,46.9,M,,*47
      int commaIndex = 0;
      int prevIndex = 0;
      String fields[15];
      for (int i = 0; i < 14; i++) {
        commaIndex = line.indexOf(',', prevIndex);
        if (commaIndex == -1) break;
        fields[i] = line.substring(prevIndex, commaIndex);
        prevIndex = commaIndex + 1;
      }
      fields[14] = line.substring(prevIndex);

      if (fields[6].toInt() > 0 && fields[2].length() > 4 && fields[4].length() > 4) {
        // Valid Fix
        gpsFixed = true;
        // Parse Latitude (DDMM.MMMM)
        double rawLat = fields[2].toDouble();
        int latDeg = (int)(rawLat / 100);
        double latMin = rawLat - (latDeg * 100);
        currentLatitude = latDeg + (latMin / 60.0);
        if (fields[3] == "S") currentLatitude = -currentLatitude;

        // Parse Longitude (DDDMM.MMMM)
        double rawLng = fields[4].toDouble();
        int lngDeg = (int)(rawLng / 100);
        double lngMin = rawLng - (lngDeg * 100);
        currentLongitude = lngDeg + (lngMin / 60.0);
        if (fields[5] == "W") currentLongitude = -currentLongitude;
      }
    }
  }
}

// Initialize OV2640 Camera
void initCamera() {
#if BOARD_IS_ESP32_CAM
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer   = LEDC_TIMER_0;
  config.pin_d0       = Y2_GPIO_NUM;
  config.pin_d1       = Y3_GPIO_NUM;
  config.pin_d2       = Y4_GPIO_NUM;
  config.pin_d3       = Y5_GPIO_NUM;
  config.pin_d4       = Y6_GPIO_NUM;
  config.pin_d5       = Y7_GPIO_NUM;
  config.pin_d6       = Y8_GPIO_NUM;
  config.pin_d7       = Y9_GPIO_NUM;
  config.pin_xclk     = XCLK_GPIO_NUM;
  config.pin_pclk     = PCLK_GPIO_NUM;
  config.pin_vsync    = VSYNC_GPIO_NUM;
  config.pin_href     = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn     = PWDN_GPIO_NUM;
  config.pin_reset    = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  if (psramFound()) {
    config.frame_size = FRAMESIZE_VGA; // 640x480
    config.jpeg_quality = 12;
    config.fb_count = 2;
  } else {
    config.frame_size = FRAMESIZE_QVGA; // 320x240
    config.jpeg_quality = 16;
    config.fb_count = 1;
  }

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("[CAMERA] Camera init failed with error 0x%x\n", err);
    cameraInitialized = false;
  } else {
    Serial.println("[CAMERA] OV2640 camera ready.");
    cameraInitialized = true;
  }
#else
  cameraInitialized = false;
#endif
}

// ======================== HTTP REST API ENDPOINTS ========================

void setCORSHeaders() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
}

void handleOptions() {
  setCORSHeaders();
  server.send(204);
}

// Telemetry JSON Payload
void handleTelemetry() {
  setCORSHeaders();

  String alertMsg = "";
  if (millis() < sosUntil) {
    alertMsg = "SOS triggered";
  } else if (currentTemperature > 39.5) {
    alertMsg = "High body temperature";
  } else if (currentTemperature < 37.0) {
    alertMsg = "Low body temperature";
  } else if (currentBatteryPct < 15) {
    alertMsg = "Low battery";
  }

  String json = "{";
  json += "\"collarId\":\"" + String(COLLAR_ID) + "\",";
  json += "\"temp\":" + String(currentTemperature, 2) + ",";
  json += "\"batt\":" + String(currentBatteryPct) + ",";
  json += "\"lat\":" + String(currentLatitude, 5) + ",";
  json += "\"lng\":" + String(currentLongitude, 5) + ",";
  json += "\"gpsFixed\":" + String(gpsFixed ? "true" : "false") + ",";
  json += "\"max30205Ready\":" + String(max30205Found ? "true" : "false") + ",";
  json += "\"camReady\":" + String(cameraInitialized ? "true" : "false") + ",";
  json += "\"alert\":\"" + alertMsg + "\",";
  json += "\"uptimeSec\":" + String(millis() / 1000);
  json += "}";

  server.send(200, "application/json", json);
}

// Camera Frame Snapshot
void handleCapture() {
  setCORSHeaders();
#if BOARD_IS_ESP32_CAM
  if (!cameraInitialized) {
    server.send(503, "text/plain", "Camera not initialized");
    return;
  }

  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("[CAMERA] Camera capture failed");
    server.send(500, "text/plain", "Camera Capture Failed");
    return;
  }

  server.setContentLength(fb->len);
  server.send(200, "image/jpeg", "");
  WiFiClient client = server.client();
  client.write(fb->buf, fb->len);
  esp_camera_fb_return(fb);
#else
  server.send(501, "text/plain", "Camera not supported on this board");
#endif
}

// SOS Trigger
void handleSOS() {
  setCORSHeaders();
  sosTriggered = true;
  sosUntil = millis() + 15000; // Trigger SOS for 15 seconds
  Serial.println("[ALARM] SOS Triggered!");
  server.send(200, "application/json", "{\"status\":\"ok\",\"sos\":true}");
}

// Root Status
void handleRoot() {
  setCORSHeaders();
  String html = "<!doctype html><html><head><title>Pawtrack Collar</title></head><body>";
  html += "<h2>Pawtrack Smart Collar (" + String(COLLAR_ID) + ")</h2>";
  html += "<p><b>IP:</b> " + WiFi.localIP().toString() + " / " + WiFi.softAPIP().toString() + "</p>";
  html += "<p><a href='/api/telemetry'>View Telemetry JSON</a></p>";
  html += "<p><a href='/capture'>Take Photo Snapshot</a></p>";
  html += "</body></html>";
  server.send(200, "text/html", html);
}

// ======================== SETUP & MAIN LOOP ========================

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n\n========================================");
  Serial.println("   PAWTRACK SMART DOG COLLAR FIRMWARE   ");
  Serial.println("========================================");

  // Initialize Sensors & Peripherals
  initMAX30205();
  gpsSerial.begin(9600, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
  initCamera();

  // 1. Attempt Wi-Fi Station connection
  Serial.print("Connecting to Wi-Fi SSID: ");
  Serial.println(WIFI_SSID);
  WiFi.mode(WIFI_AP_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long startAttemptTime = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 8000) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n[Wi-Fi] Connected to Local Network!");
    Serial.print("[Wi-Fi] Collar IP Address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\n[Wi-Fi] Could not connect to network. Standalone AP mode only.");
  }

  // 2. Start Access Point for direct connection anywhere
  WiFi.softAPConfig(AP_IP, AP_GATEWAY, AP_SUBNET);
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  Serial.print("[AP] Hotspot created: ");
  Serial.println(AP_SSID);
  Serial.print("[AP] Connect your phone/laptop to hotspot, then open http://");
  Serial.println(WiFi.softAPIP());

  // Setup Web Server Endpoints
  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/telemetry", HTTP_GET, handleTelemetry);
  server.on("/api/telemetry", HTTP_OPTIONS, handleOptions);
  server.on("/data", HTTP_GET, handleTelemetry);
  server.on("/data", HTTP_OPTIONS, handleOptions);
  server.on("/capture", HTTP_GET, handleCapture);
  server.on("/capture", HTTP_OPTIONS, handleOptions);
  server.on("/api/sos", HTTP_POST, handleSOS);
  server.on("/api/sos", HTTP_OPTIONS, handleOptions);
  server.onNotFound(handleOptions);

  server.begin();
  Serial.println("[SERVER] HTTP Server started on port 80.");
}

void loop() {
  // Handle incoming HTTP client requests
  server.handleClient();

  // Process incoming GPS bytes
  processGPS();

  // Periodic sensor readings (every 1 second)
  if (millis() - lastSensorRead >= 1000) {
    lastSensorRead = millis();

    currentTemperature = readMAX30205();
    currentBatteryPct  = readBatteryLevel();

    // Serial monitor debug output
    Serial.print("[DATA] Temp: ");
    Serial.print(currentTemperature, 2);
    Serial.print(" °C | Batt: ");
    Serial.print(currentBatteryPct);
    Serial.print("% | GPS: ");
    Serial.print(currentLatitude, 5);
    Serial.print(", ");
    Serial.print(currentLongitude, 5);
    Serial.print(" (Fixed: ");
    Serial.print(gpsFixed ? "YES" : "NO");
    Serial.println(")");
  }
}

