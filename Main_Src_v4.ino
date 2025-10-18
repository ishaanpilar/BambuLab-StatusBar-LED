// ===== BLLEDController ESP32 + Bambu MQTT + FastLED =====
// Dipasha’s Extended Version with Heating/Progress Animations

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <FastLED.h>
#include <map>

// ----------------- USER CONFIG -----------------
#define WIFI_SSID        "ssid"
#define WIFI_PASS        "password"

#define PRINTER_IP       "ipaddress"
#define PRINTER_PORT_TLS 8883

// Bambu local MQTT credentials:
#define MQTT_USER        "bblp"
#define MQTT_PASS        "accesscode"
#define PRINTER_SERIAL   "serialnumber"

// LED strip config:
#define LED_PIN          13
#define NUM_LEDS         13
#define LED_TYPE         WS2812B
#define COLOR_ORDER      GRB
#define LED_BRIGHTNESS   100

// ----------------- INTERNALS -----------------
WiFiClientSecure secureClient;
PubSubClient mqtt(secureClient);

#ifndef MQTT_MAX_PACKET_SIZE
  #define MQTT_MAX_PACKET_SIZE 8192
#endif

CRGB leds[NUM_LEDS];

float nozzleTemp = 0;
float nozzleTarget = 0;
int mcPercent = 0;

enum class StatusMode {
  NET_CONNECTING,
  MQTT_CONNECTING,
  HOMING,
  LEVELING,
  HEATING,
  PRINTING,
  FINISH,
  ERROR,
  IDLE,
  PREPARE,
  UNKNOWN
};

StatusMode currentMode = StatusMode::NET_CONNECTING;
unsigned long specialStart = 0;
uint16_t animStep = 0;

// -------------- Utility --------------
void logLine(const String& s) { Serial.println(s); }

// ---------------- LED Helpers -------------------
void showSolid(const CRGB& c) {
  fill_solid(leds, NUM_LEDS, c);
  FastLED.show();
}

void strobe(const CRGB& c, uint16_t onMs=80, uint16_t offMs=120) {
  uint16_t t = (millis() % (onMs + offMs));
  if (t < onMs) showSolid(c);
  else showSolid(CRGB::Black);
}

bool isIdle(JsonVariant jprint) {
  // check that printer has no active stage, no progress, no gcode state
  if (jprint["stg_cur"].isNull() &&
      jprint["mc_percent"].isNull() &&
      jprint["gcode_state"].isNull()) {

    // check nozzle temperature < 70
    if (!jprint["nozzle_temper"].isNull()) {
      float nozzle = jprint["nozzle_temper"].as<float>();
      if (nozzle < 120.0) {
        return true;
      }
    }
  }
  return false;
}


void blinkBlueTwice() {
  static int count = 0;
  static unsigned long last = 0;
  if (millis() - last > 300) {
    last = millis();
    if (count % 2 == 0) showSolid(CRGB::Blue);
    else showSolid(CRGB::Black);
    count++;
    if (count > 3) count = 0;
   
  }
}
void waveBlue() {
  EVERY_N_MILLISECONDS(50) { animStep++; }
  for (int i = 0; i < NUM_LEDS; i++) {
    uint8_t wave = sin8(i * 16 + animStep * 8);  // sine wave pattern
    leds[i] = CHSV(160, 255, wave);              // hue ~160 = blue
  }
  FastLED.show();
}
void levelingWave() {
  static int pos = 0;         // head position of the block
  static int dir = 1;         // 1 = forward, -1 = backward

  EVERY_N_MILLISECONDS(120) { 
    pos += dir;
    if (pos >= NUM_LEDS - 1) dir = -1;   // bounce at far right
    if (pos <= 0) dir = 1;               // bounce at far left
  }

  fill_solid(leds, NUM_LEDS, CRGB::Black);

  // Gradient brightness for 4 LEDs
  if (pos >= 0 && pos < NUM_LEDS)       leds[pos]     = CRGB::Magenta;        // brightest head
  if (pos-1 >= 0 && pos-1 < NUM_LEDS)   leds[pos-1]   = CRGB::Magenta / 2;    // dimmer
  if (pos-2 >= 0 && pos-2 < NUM_LEDS)   leds[pos-2]   = CRGB::Magenta / 4;    // even dimmer
  if (pos-3 >= 0 && pos-3 < NUM_LEDS)   leds[pos-3]   = CRGB::Magenta / 8;    // faint tail

  FastLED.show();
}

void pulse(const CRGB& c, uint16_t periodMs = 1500) {
  uint16_t t = (millis() % periodMs);
  float phase = (t < periodMs/2) 
                  ? (t / (float)(periodMs/2)) 
                  : (1.0f - (t - periodMs/2) / (float)(periodMs/2));
  CRGB col = c;
  col.nscale8(uint8_t(32 + phase * 223));  // brightness breathing
  fill_solid(leds, NUM_LEDS, col);
  FastLED.show();
}
void nozzleHeatingBar(float cur, float tar) {
  int lit = map(cur, 120, tar, 0, NUM_LEDS);
  lit = constrain(lit, 0, NUM_LEDS);

  fill_solid(leds, NUM_LEDS, CRGB::Black);

  for (int i = 0; i < lit; i++) {
    int idx = (NUM_LEDS - 1) - i;  // reverse index
    leds[idx] = CRGB::Orange;
  }

  FastLED.show();
}

void prepareWave() {
  static int pos = 0;
  static int dir = 1;  // 1 = forward, -1 = backward

  EVERY_N_MILLISECONDS(80) {
    pos += dir;
    if (pos >= NUM_LEDS - 1) dir = -1;  // bounce back at right
    if (pos <= 0) dir = 1;              // bounce forward at left
  }

  fill_solid(leds, NUM_LEDS, CRGB::Black);

  // 4-LED yellow gradient "comet"
  if (pos >= 0 && pos < NUM_LEDS)       leds[pos]     = CRGB::Yellow;
  if (pos-1 >= 0 && pos-1 < NUM_LEDS)   leds[pos-1]   = CRGB::Yellow / 2;
  if (pos-2 >= 0 && pos-2 < NUM_LEDS)   leds[pos-2]   = CRGB::Yellow / 4;
  if (pos-3 >= 0 && pos-3 < NUM_LEDS)   leds[pos-3]   = CRGB::Yellow / 8;

  FastLED.show();
}

void printProgressWave(int percent) {
  int lit = map(percent, 20, 100, 0, NUM_LEDS);
  lit = constrain(lit, 0, NUM_LEDS);

  EVERY_N_MILLISECONDS(60) { animStep++; }
  fill_solid(leds, NUM_LEDS, CRGB::Black);

  for (int i = 0; i < lit; i++) {
    int idx = (NUM_LEDS - 1) - i;  // reverse index
    leds[idx] = CHSV(96, 255, 180 + (sin8(i * 16 + animStep) / 4));
  }

  FastLED.show();
}

// ---------------- applyMode ----------------
void applyMode(StatusMode m) {
  currentMode = m;
  specialStart = millis();
  logLine("[LED] Mode=" + String((int)m));
}

// ---------------- renderLEDs ----------------
void renderLEDs() {
  switch (currentMode) {
    case StatusMode::NET_CONNECTING:
      waveBlue(); 
      break;

    case StatusMode::MQTT_CONNECTING:
      strobe(CRGB::Blue, 100, 100);
      break;

    case StatusMode::HOMING:
      blinkBlueTwice();
      break;

    case StatusMode::LEVELING:
      levelingWave();
      break;

    case StatusMode::HEATING:
      nozzleHeatingBar(nozzleTemp, nozzleTarget);
      break;

    case StatusMode::PRINTING:
      printProgressWave(mcPercent);
      break;

    case StatusMode::FINISH:
      showSolid(CRGB::Green);
      break;

    case StatusMode::ERROR:
      strobe(CRGB::Red, 100, 100);
      break;

    case StatusMode::IDLE:
    pulse(CRGB::Green, 3000); // slow breathing white
    break;

    case StatusMode::PREPARE:
    prepareWave();
    break;
    
    default:
      showSolid(CRGB::White);
      break;
  }
}

// ---------------- MQTT Payload Handler ----------------
void handleReportPayload(char* topic, byte* payload, unsigned int length) {
  String raw; raw.reserve(length + 1);
  for (unsigned int i=0; i<length; i++) raw += (char)payload[i];
  Serial.println("=== Raw MQTT Payload ===");
  Serial.println(raw);

  DynamicJsonDocument doc(8192);
  if (deserializeJson(doc, raw)) return;
  JsonVariant jprint = doc["print"];
  if (jprint.isNull()) return;

  if (jprint.containsKey("hms") && !jprint["hms"].as<JsonArray>().isNull()) {
    if (jprint["hms"].as<JsonArray>().size() > 0) {
      applyMode(StatusMode::ERROR);
      return;
    }
  }
if (!jprint["mc_print_sub_stage"].isNull()) {
  int sub = jprint["mc_print_sub_stage"].as<int>();
  if (sub == 5) {
    applyMode(StatusMode::LEVELING);
    return;
  }
}
  if (jprint.containsKey("home_flag")) {
    applyMode(StatusMode::HOMING);
    return;
  }

  if (!jprint["nozzle_temper"].isNull()) nozzleTemp = jprint["nozzle_temper"].as<float>();
  if (!jprint["nozzle_target_temper"].isNull()) nozzleTarget = jprint["nozzle_target_temper"].as<float>();
  if (!jprint["mc_percent"].isNull()) mcPercent = jprint["mc_percent"].as<int>();

  if (!jprint["stg_cur"].isNull()) {
    int stg = jprint["stg_cur"].as<int>();
    if (stg == 14) applyMode(StatusMode::LEVELING);
    else if (stg == 0) applyMode(StatusMode::PRINTING);
    else if (stg == 255) applyMode(StatusMode::FINISH);
  }

  if (nozzleTarget > 70 && nozzleTemp < nozzleTarget - 5) {
    applyMode(StatusMode::HEATING);
  }
  if (isIdle(jprint)) {
  applyMode(StatusMode::IDLE); // or make a new StatusMode::IDLE
  return;
}
if (!jprint["gcode_file_prepare_percent"].isNull()) {
  int prep = jprint["gcode_file_prepare_percent"].as<int>();
  if (prep == 100) {
    applyMode(StatusMode::PREPARE);
    return;
  }
}

}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  handleReportPayload(topic, payload, length);
}

// ---------------- WiFi/MQTT Connect ----------------
void connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;
  applyMode(StatusMode::NET_CONNECTING);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    renderLEDs();
    delay(50);
  }
}

bool connectMQTT() {
  if (mqtt.connected()) return true;
  applyMode(StatusMode::MQTT_CONNECTING);
  secureClient.setInsecure();
  mqtt.setServer(PRINTER_IP, PRINTER_PORT_TLS);
  mqtt.setCallback(mqttCallback);
  String clientId = "ESP32-BLLED-" + String((uint32_t)ESP.getEfuseMac(), HEX);
  if (!mqtt.connect(clientId.c_str(), MQTT_USER, MQTT_PASS)) return false;
  mqtt.subscribe(("device/" + String(PRINTER_SERIAL) + "/report").c_str());
  return true;
}

// ---------------- Setup & Loop -------------------
void setup() {
  Serial.begin(115200);
  FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS);
  FastLED.setBrightness(LED_BRIGHTNESS);
  showSolid(CRGB::Black);
  connectWiFi();
}

unsigned long lastMQTry = 0;

void loop() {
  if (WiFi.status() != WL_CONNECTED) connectWiFi();
  if (!mqtt.connected()) {
    unsigned long now = millis();
    if (now - lastMQTry > 3000) {
      connectMQTT();
      lastMQTry = now;
    }
  } else mqtt.loop();
  renderLEDs();
}
