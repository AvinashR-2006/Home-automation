#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <WebServer.h>
#include <Preferences.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <time.h>
#include <Arduino.h>

#define RELAY_PIN 5
#define AP_SSID "SmartDevice_Setup"
#define AP_PASSWORD "12345678"
#define MQTT_CLIENT_ID "ESP32_SmartDevice"
#ifdef LED_BUILTIN
#define STATUS_LED_PIN LED_BUILTIN
#else
#define STATUS_LED_PIN -1
#endif

WebServer server(80);
Preferences prefs;

WiFiClient wifiClient;
WiFiClientSecure secureClient;
PubSubClient mqttClient(wifiClient);

struct DeviceConfig {
  String ssid;
  String password;
  String brokerUrl;
  uint16_t mqttPort;
  String mqttUser;
  String mqttPass;
  String topic;
};

struct TimerConfig {
  int onHour;
  int onMinute;
  int offHour;
  int offMinute;
  bool valid;
};

DeviceConfig config;
TimerConfig timerConfig = {8, 0, 9, 0, true};

bool apMode = false;
bool relayState = false;
bool mqttConfigured = false;
bool restartPending = false;
unsigned long lastWiFiAttemptMs = 0;
unsigned long lastMQTTAttemptMs = 0;
unsigned long lastTimerCheckMs = 0;
unsigned long restartAtMs = 0;

const unsigned long WIFI_RETRY_INTERVAL_MS = 10000;
const unsigned long MQTT_RETRY_INTERVAL_MS = 5000;
const unsigned long TIMER_CHECK_INTERVAL_MS = 1000;

String formatTimeValue(int hour, int minute) {
  char buffer[6];
  snprintf(buffer, sizeof(buffer), "%02d:%02d", hour, minute);
  return String(buffer);
}

bool parseTimeString(const String &value, int &hour, int &minute) {
  if (value.length() != 5 || value.charAt(2) != ':') {
    return false;
  }

  hour = value.substring(0, 2).toInt();
  minute = value.substring(3, 5).toInt();

  return hour >= 0 && hour <= 23 && minute >= 0 && minute <= 59;
}

String normalizedBrokerHost(String brokerUrl) {
  brokerUrl.trim();
  brokerUrl.replace("mqtt://", "");
  brokerUrl.replace("mqtts://", "");
  brokerUrl.replace("ssl://", "");
  brokerUrl.replace("tcp://", "");
  brokerUrl.trim();

  int slashIndex = brokerUrl.indexOf('/');
  if (slashIndex >= 0) {
    brokerUrl = brokerUrl.substring(0, slashIndex);
  }

  int colonIndex = brokerUrl.indexOf(':');
  if (colonIndex >= 0) {
    brokerUrl = brokerUrl.substring(0, colonIndex);
  }

  brokerUrl.trim();
  return brokerUrl;
}

bool useSecureMQTT() {
  return config.mqttPort == 8883 || config.brokerUrl.startsWith("mqtts://") || config.brokerUrl.startsWith("ssl://");
}

void setRelay(bool state) {
  relayState = state;
  digitalWrite(RELAY_PIN, relayState ? HIGH : LOW);
  if (STATUS_LED_PIN >= 0) {
    digitalWrite(STATUS_LED_PIN, relayState ? HIGH : LOW);
  }
  Serial.printf("Relay state: %s\n", relayState ? "ON" : "OFF");
}

void saveTimerToPreferences() {
  prefs.begin("timer", false);
  prefs.putInt("onHour", timerConfig.onHour);
  prefs.putInt("onMin", timerConfig.onMinute);
  prefs.putInt("offHour", timerConfig.offHour);
  prefs.putInt("offMin", timerConfig.offMinute);
  prefs.putBool("valid", timerConfig.valid);
  prefs.end();
}

void loadTimerFromPreferences() {
  prefs.begin("timer", false);
  timerConfig.onHour = prefs.getInt("onHour", 8);
  timerConfig.onMinute = prefs.getInt("onMin", 0);
  timerConfig.offHour = prefs.getInt("offHour", 9);
  timerConfig.offMinute = prefs.getInt("offMin", 0);
  timerConfig.valid = prefs.getBool("valid", true);
  prefs.end();
}

void loadConfig() {
  prefs.begin("config", false);
  config.ssid = prefs.getString("ssid", "");
  config.password = prefs.getString("pass", "");
  config.brokerUrl = prefs.getString("broker", "");
  config.mqttPort = static_cast<uint16_t>(prefs.getUInt("port", 1883));
  config.mqttUser = prefs.getString("user", "");
  config.mqttPass = prefs.getString("mpass", "");
  config.topic = prefs.getString("topic", "home/device/control");
  prefs.end();

  mqttConfigured = config.brokerUrl.length() > 0 && config.topic.length() > 0;
}

void saveConfig(const JsonDocument &doc) {
  config.ssid = doc["ssid"] | "";
  config.password = doc["password"] | "";
  config.brokerUrl = doc["broker_url"] | "";
  config.mqttPort = doc["port"] | 1883;
  config.mqttUser = doc["mqtt_user"] | "";
  config.mqttPass = doc["mqtt_pass"] | "";
  config.topic = doc["topic"] | "home/device/control";

  prefs.begin("config", false);
  prefs.putString("ssid", config.ssid);
  prefs.putString("pass", config.password);
  prefs.putString("broker", config.brokerUrl);
  prefs.putUInt("port", config.mqttPort);
  prefs.putString("user", config.mqttUser);
  prefs.putString("mpass", config.mqttPass);
  prefs.putString("topic", config.topic);
  prefs.end();

  mqttConfigured = config.brokerUrl.length() > 0 && config.topic.length() > 0;
}

void startAPMode() {
  WiFi.disconnect(true, true);
  delay(100);
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  apMode = true;
}

bool hasWiFiCredentials() {
  return config.ssid.length() > 0 && config.password.length() > 0;
}

void connectWiFiIfNeeded(bool forceNow = false) {
  if (!hasWiFiCredentials()) {
    if (!apMode) {
      startAPMode();
    }
    return;
  }

  if (WiFi.status() == WL_CONNECTED) {
    return;
  }

  if (!forceNow && millis() - lastWiFiAttemptMs < WIFI_RETRY_INTERVAL_MS) {
    return;
  }

  if (apMode) {
    WiFi.softAPdisconnect(true);
    apMode = false;
  }

  WiFi.mode(WIFI_STA);
  WiFi.begin(config.ssid.c_str(), config.password.c_str());
  lastWiFiAttemptMs = millis();
}

void mqttCallback(char *topic, byte *payload, unsigned int length) {
  String message;
  message.reserve(length);
  for (unsigned int i = 0; i < length; i++) {
    message += static_cast<char>(payload[i]);
  }

  if (message == "1" || message.equalsIgnoreCase("on")) {
    setRelay(true);
  } else if (message == "0" || message.equalsIgnoreCase("off")) {
    setRelay(false);
  }
}

void configureMQTTClient() {
  if (useSecureMQTT()) {
    secureClient.setInsecure();
    mqttClient.setClient(secureClient);
  } else {
    mqttClient.setClient(wifiClient);
  }

  mqttClient.setServer(normalizedBrokerHost(config.brokerUrl).c_str(), config.mqttPort);
  mqttClient.setCallback(mqttCallback);
}

void connectMQTTIfNeeded(bool forceNow = false) {
  if (!mqttConfigured || WiFi.status() != WL_CONNECTED || mqttClient.connected()) {
    return;
  }

  if (!forceNow && millis() - lastMQTTAttemptMs < MQTT_RETRY_INTERVAL_MS) {
    return;
  }

  lastMQTTAttemptMs = millis();
  configureMQTTClient();

  bool connected = false;
  if (config.mqttUser.length() > 0) {
    connected = mqttClient.connect(MQTT_CLIENT_ID, config.mqttUser.c_str(), config.mqttPass.c_str());
  } else {
    connected = mqttClient.connect(MQTT_CLIENT_ID);
  }

  if (connected) {
    mqttClient.subscribe(config.topic.c_str());
  }
}

void syncTimeIfNeeded() {
  static bool requested = false;
  if (WiFi.status() != WL_CONNECTED || requested) {
    return;
  }
  configTime(19800, 0, "pool.ntp.org", "time.nist.gov");
  requested = true;
}

bool timerShouldBeOn(int currentMinutes, int onMinutes, int offMinutes) {
  if (onMinutes == offMinutes) {
    return false;
  }
  if (onMinutes < offMinutes) {
    return currentMinutes >= onMinutes && currentMinutes < offMinutes;
  }
  return currentMinutes >= onMinutes || currentMinutes < offMinutes;
}

void checkTimer() {
  if (!timerConfig.valid || millis() - lastTimerCheckMs < TIMER_CHECK_INTERVAL_MS) {
    return;
  }

  lastTimerCheckMs = millis();
  struct tm timeInfo;
  if (!getLocalTime(&timeInfo, 50)) {
    return;
  }

  int currentMinutes = (timeInfo.tm_hour * 60) + timeInfo.tm_min;
  int onMinutes = (timerConfig.onHour * 60) + timerConfig.onMinute;
  int offMinutes = (timerConfig.offHour * 60) + timerConfig.offMinute;

  bool shouldBeOn = timerShouldBeOn(currentMinutes, onMinutes, offMinutes);
  if (shouldBeOn != relayState) {
    setRelay(shouldBeOn);
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(RELAY_PIN, OUTPUT);
  if (STATUS_LED_PIN >= 0) {
    pinMode(STATUS_LED_PIN, OUTPUT);
  }
  setRelay(false);
  LittleFS.begin(true);
  loadConfig();
  loadTimerFromPreferences();
  if (hasWiFiCredentials()) {
    connectWiFiIfNeeded(true);
  } else {
    startAPMode();
  }
}

void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    syncTimeIfNeeded();
    connectMQTTIfNeeded();
    mqttClient.loop();
    checkTimer();
  } else {
    connectWiFiIfNeeded();
  }
  delay(5);
}
