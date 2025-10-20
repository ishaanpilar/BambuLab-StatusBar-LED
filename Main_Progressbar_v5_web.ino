#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <FastLED.h>
#include <ArduinoJson.h>

// ===============================
// USER CONFIGURATION
// ===============================
const char* WIFI_SSID = "wifissid";
const char* WIFI_PASS = "wifipassword";

const char* MQTT_SERVER = "ipaddress";
const int MQTT_PORT = 8883;
const char* MQTT_USER = "bblp";
const char* MQTT_PASS = "acesscode";
const char* PRINTER_SERIAL = "printerserial";  // Printer Serial

// ===============================
// LED CONFIGURATION
// ===============================
#define DATA_PIN 5
#define NUM_LEDS 13
#define BRIGHTNESS 64
#define MAX_MILLIWATTS 850

int progressDirection = -1;
uint8_t lastProgress = 0;
CRGB leds[NUM_LEDS];
CRGB PROGRESS_ON_LED = CRGB::Green;
CRGB PROGRESS_OFF_LED = CRGB::Red;

// ===============================
// GLOBALS
// ===============================
WiFiClientSecure espClient;
PubSubClient mqttClient(espClient);

enum PrinterState { Connecting,
                    Heating,
                    Loading,
                    Printing,
                    Finished,
                    Error,
                    Paused };
PrinterState printerState = Connecting;

uint8_t progressPercent = 0;
unsigned long finishedSince = 0;
float lastNozzleTemp = 0.0;
bool isHeating = false;
unsigned long lastHmsTime = 0;
// ===============================
// WIFI SETUP
// ===============================
void connectWiFi() {
  Serial.print("Connecting to WiFi: ");
  Serial.println(WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWiFi connected!");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
  Serial.println("WiFi connected — waiting for MQTT...");
}

// ===============================
// MQTT HANDLING
// ===============================
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  Serial.print("\n[MQTT Message] Topic: ");
  Serial.println(topic);

  String message;
  for (unsigned int i = 0; i < length; i++) message += (char)payload[i];
  Serial.println("Payload: " + message);

  StaticJsonDocument<2048> doc;
  DeserializationError error = deserializeJson(doc, message);
  if (error) return;
  if (!doc["print"]) return;

  // ====== Handle gcode_state transitions ======
  if (doc["print"]["gcode_state"]) {
    const char* state = doc["print"]["gcode_state"];
    if (strcmp(state, "RUNNING") == 0) printerState = Printing;
    else if (strcmp(state, "FINISH") == 0) {
      printerState = Finished;
      finishedSince = millis();
    } else if (strcmp(state, "FAILED") == 0) printerState = Error;
    else if (strcmp(state, "PAUSE") == 0) printerState = Paused;

    Serial.print("State changed to: ");
    Serial.println(state);
  }

  // ====== Context-aware nozzle heating detection ======
  if (doc["print"]["nozzle_temper"]) {
    float nozzleTemp = doc["print"]["nozzle_temper"].as<float>();
    float delta = nozzleTemp - lastNozzleTemp;

    static bool hasBaseline = false;
    if (!hasBaseline) {
      lastNozzleTemp = nozzleTemp;
      hasBaseline = true;
      return;
    }

    if ((printerState == Loading || printerState == Connecting || (printerState == Error && (millis() - lastHmsTime) < 5000)) && nozzleTemp >= 40 && nozzleTemp < 200 && delta >= 5.0) {
      if (!isHeating) {
        isHeating = true;
        printerState = Heating;
        Serial.println("🔥 Nozzle pre-heating detected — switching to HEATING mode");
      }
    }

    if (isHeating) {
      float clamped = constrain(nozzleTemp, 40.0, 200.0);
      uint8_t newPercent = map((int)clamped, 40, 200, 0, 100);
      if (newPercent != progressPercent) {
        progressPercent = newPercent;
        Serial.printf("Heating progress: %.1f°C → %d%%\n", nozzleTemp, progressPercent);
      }
    }

    bool tempReached = nozzleTemp >= 200.0;
    bool printStarted = doc["print"]["mc_percent"];
    bool cooling = (delta < -2.0);

    if (isHeating && (tempReached || printStarted || cooling)) {
      isHeating = false;
      printerState = Printing;
      Serial.println("🌡️ Heating complete — switching to PRINTING");
    }

    lastNozzleTemp = nozzleTemp;
  }

  // ====== Detect print completion via stg_cur (robust + atomic) ======
  if (doc["print"].containsKey("stg_cur")) {
    int stgCurr = -1;
    if (doc["print"]["stg_cur"].is<const char*>())
      stgCurr = atoi(doc["print"]["stg_cur"]);
    else
      stgCurr = doc["print"]["stg_cur"].as<int>();

    if (stgCurr == 255 && printerState != Finished) {
      Serial.println("✅ Print completed (stg_cur = 255)");
      printerState = Finished;
      progressPercent = 100;
      finishedSince = millis();
      setAll(CRGB::Green);
    }
  }

  // ====== HMS (Health Monitoring System) handling ======
  if (doc["print"].containsKey("hms")) {
    JsonArray hmsArray = doc["print"]["hms"].as<JsonArray>();
    if (!hmsArray.isNull() && hmsArray.size() > 0) {
      Serial.println("⚠️ HMS event detected — switching to ERROR state");
      printerState = Error;
      setAll(CRGB::Red);
      lastHmsTime = millis();  // track when HMS was received
    }
  }

  // ====== Error recovery conditions ======
  if (printerState == Error) {
    // If we get valid progress updates or print resumes, exit error
    if (doc["print"]["mc_percent"] || (doc["print"]["gcode_state"] && strcmp(doc["print"]["gcode_state"], "RUNNING") == 0)) {
      Serial.println("✅ Error cleared — resuming PRINTING");
      printerState = Printing;
    } else if (doc["print"]["gcode_state"] && strcmp(doc["print"]["gcode_state"], "FINISH") == 0) {
      Serial.println("✅ Error cleared — print already FINISHED");
      printerState = Finished;
      progressPercent = 100;
      finishedSince = millis();
      setAll(CRGB::Green);
    }
  }

  // ====== Handle mc_percent safely (skip if Finished in same packet) ======
  if (printerState != Finished && doc["print"].containsKey("mc_percent")) {
    uint8_t newPercent = doc["print"]["mc_percent"].as<uint8_t>();

    if (newPercent != progressPercent) {
      lastProgress = progressPercent;
      progressPercent = newPercent;

      Serial.print("Progress updated: ");
      Serial.print(progressPercent);
      Serial.println("%");

      if ((printerState == Loading || printerState == Connecting || printerState == Heating) && progressPercent > lastProgress) {
        printerState = Printing;
        Serial.println("Auto-detected active print — switching to PRINTING mode");
      }
    }
  }
}

void connectMQTT() {
  mqttClient.setServer(MQTT_SERVER, MQTT_PORT);
  mqttClient.setCallback(mqttCallback);

  while (!mqttClient.connected()) {
    Serial.println("Connecting to Bambu MQTT...");
    if (mqttClient.connect("Mauvi_Studios", MQTT_USER, MQTT_PASS)) {
      Serial.println("MQTT connected!");
      char topic[128];
      sprintf(topic, "device/%s/report", PRINTER_SERIAL);
      mqttClient.subscribe(topic);
      Serial.print("Subscribed to topic: ");
      Serial.println(topic);
      printerState = Loading;
    } else {
      Serial.print("MQTT failed, rc=");
      Serial.println(mqttClient.state());
      delay(5000);
    }
  }
}

// ===============================
// LED ANIMATIONS
// ===============================
void setAll(CRGB color) {
  for (int i = 0; i < NUM_LEDS; i++) leds[i] = color;
  FastLED.show();
}

void updateLEDs() {
  unsigned long t = millis();
  switch (printerState) {
    case Heating:
      {
        float pct = progressPercent / 100.0;
        int onLeds = (int)(NUM_LEDS * pct + 0.5);
        for (int i = 0; i < NUM_LEDS; i++) {
          int ledIndex = (progressDirection == 1) ? i : (NUM_LEDS - 1 - i);
          leds[ledIndex] = (i < onLeds)
                             ? CRGB(map(i, 0, onLeds - 1, 64, 255), map(i, 0, onLeds - 1, 21, 85), 0)
                             : CRGB(30, 0, 0);
        }
        break;
      }

    case Connecting:
      {
        float intensity = (sin(t / 500.0) + 1.0) * 0.5;
        uint8_t b = (uint8_t)(intensity * 255);
        for (int i = 0; i < NUM_LEDS; i++) leds[i] = CRGB(0, b, min(b / 4, 60));
        break;
      }

    case Loading:
      {
        float waveSpeed = 0.002, waveLength = 2.5, phase = t * waveSpeed;
        for (int i = 0; i < NUM_LEDS; i++) {
          float intensity = (sin((i / waveLength) + phase) + 1.0) * 0.5;
          uint8_t b = (uint8_t)(intensity * 255);
          leds[i] = CRGB(0, b, min(b / 3, 80));
        }
        break;
      }

    case Error:
      {
        uint8_t brightness = (sin(t / 100.0) + 1.0) * 127.5;  // oscillates fast
        for (int i = 0; i < NUM_LEDS; i++) {
          leds[i] = CRGB(brightness, 0, 0);
        }
        break;
      }

    case Printing:
      {
        float pct = progressPercent / 100.0;
        int onLeds = (int)(NUM_LEDS * pct + 0.5);
        for (int i = 0; i < NUM_LEDS; i++) {
          int ledIndex = (progressDirection == 1) ? i : (NUM_LEDS - 1 - i);
          leds[ledIndex] = (i < onLeds) ? PROGRESS_ON_LED : PROGRESS_OFF_LED;
        }
        break;
      }

    case Finished:
      {
        if (millis() - finishedSince < 2000) {
          setAll(PROGRESS_ON_LED);
          return;
        }
        if (millis() - finishedSince > 30000) {
          setAll(PROGRESS_ON_LED);
          return;
        }
        for (int i = 0; i < NUM_LEDS; i++) {
          int period = 500 + (i * 421) % 500;
          leds[i] = CRGB(0, 255 * (t % period < period / 2), 0);
        }
        break;
      }

    case Paused:
      {
        float b = (sin(t / 800.0) + 1.0) * 127.5;
        for (int i = 0; i < NUM_LEDS; i++) leds[i] = CRGB(b / 2, b / 3, 0);
        break;
      }
  }
  FastLED.show();
}

// ===============================
// SETUP & LOOP
// ===============================
void setup() {
  Serial.begin(115200);
  FastLED.addLeds<WS2812B, DATA_PIN, GRB>(leds, NUM_LEDS);
  FastLED.setMaxPowerInMilliWatts(MAX_MILLIWATTS);
  FastLED.setBrightness(BRIGHTNESS);
  FastLED.clear(true);

  printerState = Connecting;
  Serial.println("Mauvi Studios - Lite MQTT Visualizer v2");
  connectWiFi();
  espClient.setInsecure();
  connectMQTT();
}

void loop() {
  if (!mqttClient.connected()) connectMQTT();
  mqttClient.loop();
  updateLEDs();
  delay(67);
}
