#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <time.h>
#include "RFIDR200.h"
#include "config.h"

#define PIN_A 18
#define PIN_B 19

// Will be changed based on tri-state-switch
// -1: Invalid, 1: left container, 2: middle container, 3: right container
int location_id = -1;

// for now we use a constant until i get a switch to test this
constexpr int LOCATION_ID = 2;

constexpr const char* SERVER_BASE_URL = "http://192.168.8.155:5202/api/";
constexpr const char* NTP_SERVER = "pool.ntp.org";

// RFID Setup
HardwareSerial hwSerial(2);
RFIDR200 rfidReader(hwSerial, 115200);
uint8_t targetepc[12] = { 0xE2, 0x00, 0x00, 0x17, 0x57, 0x0D, 0x01, 0x23, 0x06, 0x30, 0xD6, 0x8E };

// HTTP client
HTTPClient http;
String postEndpoint;

unsigned long lastPollResetTime = 0;

void connectToWiFi() {
  Serial.print("Connecting to WiFi");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("Connected. IP: ");
  Serial.println(WiFi.localIP());
}

void syncTime() {
  configTime(0, 0, NTP_SERVER);
  struct tm timeinfo;
  while (!getLocalTime(&timeinfo)) {
    Serial.println("Waiting for NTP time...");
    delay(1000);
  }
}

void testAPIConnection() {
  HTTPClient tempHttp;
  String testUrl = String(SERVER_BASE_URL) + "arsch/arsch";
  tempHttp.begin(testUrl);
  tempHttp.setAuthorization("x-api-key", API_KEY);

  int response = tempHttp.GET();
  if (response > 0) {
    Serial.print("API Response: ");
    Serial.println(response);
    Serial.println(tempHttp.getString());
  } else {
    Serial.print("API Error: ");
    Serial.println(response);
  }
  tempHttp.end();
}

String getISOTimestamp() {
  struct timeval now;
  gettimeofday(&now, NULL);
  struct tm timeinfo;
  localtime_r(&now.tv_sec, &timeinfo);

  char isoBase[25];
  strftime(isoBase, sizeof(isoBase), "%Y-%m-%dT%H:%M:%S", &timeinfo);

  char isoTimestamp[30];
  snprintf(isoTimestamp, sizeof(isoTimestamp), "%s.%03ldZ", isoBase, now.tv_usec / 1000);
  return String(isoTimestamp);
}

void evalSwitch() {
  bool a = !digitalRead(PIN_A);  // Switch pulls pin LOW when active
  bool b = !digitalRead(PIN_B);

  if (a && !b) {
    location_id = 1;
  } else if (!a && !b) {
    location_id = 2;
  } else if (!a && b) {
    location_id = 3;
  } else {
    location_id = -1;  // Invalid state (both active)
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("Starting up...");

  pinMode(PIN_A, INPUT_PULLUP);
  pinMode(PIN_B, INPUT_PULLUP);

  connectToWiFi();
  syncTime();
  testAPIConnection();

  // Setup RFID reader
  hwSerial.begin(115200, SERIAL_8N1, 16, 17);
  rfidReader.begin();
  rfidReader.setTransmitPower(2000);
  rfidReader.initiateMultiplePolling(10000);

  Serial.println("RFID reader initialized.");

  // Setup HTTP client for sending readings
  postEndpoint = String(SERVER_BASE_URL) + "rfidreading/addreading";
  http.begin(postEndpoint);
  http.addHeader("x-api-key", API_KEY);
  http.addHeader("Content-Type", "application/json");
}

void loop() {
  uint8_t responseBuffer[256];

  // evalSwitch();

  if (rfidReader.getResponse(responseBuffer, sizeof(responseBuffer))) {
    if (rfidReader.hasValidTag(responseBuffer)) {
      uint8_t rssi;
      uint8_t epc[12];
      rfidReader.parseTagResponse(responseBuffer, rssi, epc);

      char hexString[25];
      for (int i = 0; i < 12; ++i) {
        sprintf(hexString + i * 2, "%02X", epc[i]);
      }
      hexString[24] = '\0';

      String json = "{";
      json += "\"TagHexId\":\"" + String(hexString) + "\",";
      // json += "\"LocationId\":" + String(location_id) + ",";
      json += "\"LocationId\":" + String(LOCATION_ID) + ",";
      json += "\"Timestamp\":\"" + getISOTimestamp() + "\",";
      json += "\"SignalStrength\":\"" + String(rssi) + "\"";
      json += "}";

      int response = http.POST(json);
      Serial.print("POST Response: ");
      Serial.println(response);
    }
  } else {
    Serial.println("No tag detected.");
  }

  // Refresh polling every 10 seconds
  if (millis() - lastPollResetTime > 10000) {
    lastPollResetTime = millis();
    rfidReader.initiateMultiplePolling(10000);
    Serial.println("Re-initiated polling.");
  }

  delay(10);
}