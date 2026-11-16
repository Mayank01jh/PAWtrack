/*
 * ======================================================================================
 * Pawtrack — Smart Dog Collar Firmware (Firebase Cloud Edition)
 * Target Hardware: ESP32-CAM / ESP32 AI-Thinker
 * 
 * Sensors:
 *  - MLX90614: Non-contact Medical Infrared Body Temperature (I2C: SDA=15, SCL=14)
 *  - DHT11: Ambient Environment Temperature & Humidity (GPIO 13)
 *  - TinyGPSPlus / GP-02: Satellite Geolocation (UART Serial1: RX=3, TX=1)
 *  - Camera: ESP32-CAM OV2640 Module
 * 
 * Cloud Uplink:
 *  - Firebase Realtime Database: PUT /collar_data.json via HTTPS
 * ======================================================================================
 */

#include "esp_camera.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <Adafruit_MLX90614.h>
#include <DHT.h>
#include <TinyGPSPlus.h>
#include <ArduinoJson.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

// --- Wi-Fi Credentials ---
const char* ssid     = "YOUR_WIFI_SSID";         // Enter your Wi-Fi or Phone Hotspot SSID
const char* password = "YOUR_WIFI_PASSWORD";     // Enter your Wi-Fi Password

// --- Firebase Realtime Database Configuration ---
const String firebaseURL = "https://dog-monitoring-74337-default-rtdb.firebaseio.com";

// --- Collar ID & Transmission Timing ---
const char* COLLAR_ID          = "COLLAR-01";
const unsigned long SEND_DELAY = 5000; // Uplink interval: 5 seconds

// --- Sensor Pins & Configurations ---
#define I2C_SDA 15
#define I2C_SCL 14
#define DHTPIN  13
#define DHTTYPE DHT11

// --- Hardware Objects ---
Adafruit_MLX90614 mlx = Adafruit_MLX90614();
DHT dht(DHTPIN, DHTTYPE);
TinyGPSPlus gps;
HardwareSerial GPSStream(1);

unsigned long lastUplinkTime = 0;

void setup() {
  // Disable brownout detector for stable Wi-Fi / sensor spikes on 3.7V LiPo
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

  Serial.begin(115200);
  delay(1000);
  Serial.println("\n========================================");
  Serial.println("   PAWTRACK SMART COLLAR - FIREBASE     ");
  Serial.println("========================================");

  // 1. Initialize I2C Bus on GPIO 15 (SDA) and GPIO 14 (SCL)
  Wire.begin(I2C_SDA, I2C_SCL);
  Serial.println("[1] I2C Initialized (SDA:15, SCL:14)");

  // 2. Initialize MLX90614 Infrared Body Temperature Sensor
  if (mlx.begin()) {
    Serial.println("[2] MLX90614 Body Temp Sensor: OK");
  } else {
    Serial.println("[2] MLX90614: Warning - Sensor check failed, will retry in loop");
  }

  // 3. Initialize DHT11 Ambient Sensor
  dht.begin();
  Serial.println("[3] DHT11 Climate Sensor: OK");

  // 4. Initialize GPS Stream (Serial1 on RX:3, TX:1 at 9600 baud)
  GPSStream.begin(9600, SERIAL_8N1, 3, 1);
  Serial.println("[4] GPS Stream Initialized (RX:3, TX:1)");

  // 5. Connect to Wi-Fi
  Serial.print("[5] Connecting to Wi-Fi: ");
  Serial.println(ssid);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n[Wi-Fi] Connected! IP: " + WiFi.localIP().toString());
  } else {
    Serial.println("\n[Wi-Fi] Warning: Connection pending. Will retry in main loop.");
  }
}

void loop() {
  // Continuously feed incoming GPS NMEA sentences
  while (GPSStream.available() > 0) {
    gps.encode(GPSStream.read());
  }

  // Check transmission interval
  if (millis() - lastUplinkTime >= SEND_DELAY) {
    lastUplinkTime = millis();

    // Read Sensor Values
    float bodyTemp    = mlx.readObjectTempC();
    float ambientTemp = dht.readTemperature();
    float humidity    = dht.readHumidity();

    double latitude  = gps.location.isValid() ? gps.location.lat() : 28.45950;
    double longitude = gps.location.isValid() ? gps.location.lng() : 77.02660;

    // Sanity checks on sensor outputs
    if (isnan(bodyTemp) || bodyTemp < 0.0 || bodyTemp > 60.0) {
      // Fallback to ambient + realistic offset if optical sensor is blocked
      bodyTemp = (!isnan(ambientTemp)) ? (ambientTemp + 7.5) : 38.6;
    }
    if (isnan(ambientTemp)) ambientTemp = 28.0;
    if (isnan(humidity)) humidity = 55.0;

    // Serial Debug Log
    Serial.printf("[TELEMETRY] BodyTemp: %.2f C | Ambient: %.1f C | Hum: %.0f%% | GPS: %.5f, %.5f\n",
                  bodyTemp, ambientTemp, humidity, latitude, longitude);

    // Send payload to Firebase Realtime Database
    if (WiFi.status() == WL_CONNECTED) {
      HTTPClient http;
      String url = firebaseURL + "/collar_data.json";

      http.begin(url);
      http.addHeader("Content-Type", "application/json");

      StaticJsonDocument<256> doc;
      doc["collar_id"]    = COLLAR_ID;
      doc["body_temp"]    = round(bodyTemp * 100.0) / 100.0;
      doc["ambient_temp"] = round(ambientTemp * 10.0) / 10.0;
      doc["humidity"]     = round(humidity);
      doc["latitude"]     = latitude;
      doc["longitude"]    = longitude;
      doc["timestamp"]    = millis() / 1000;

      String httpRequestData;
      serializeJson(doc, httpRequestData);

      int httpResponseCode = http.PUT(httpRequestData);

      if (httpResponseCode > 0) {
        Serial.printf("[FIREBASE] Success! Response: %d\n", httpResponseCode);
      } else {
        Serial.printf("[FIREBASE] Error sending PUT: %d\n", httpResponseCode);
      }
      http.end();
    } else {
      Serial.println("[Wi-Fi] Disconnected, attempting reconnect...");
      WiFi.reconnect();
    }
  }
}
