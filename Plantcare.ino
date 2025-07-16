/* ---------------------------------------------------------------------------
 * Needs Boards
 *	"esp32 by Espressif Systems" https://github.com/espressif/arduino-esp32
 * Needs Libraries
 *	"URLCode by XieXuan" https://github.com/MR-XieXuan/URLCode_for_Arduino
 *	"incbin by Dale Weiler" https://github.com/AlexIII/incbin-arduino
 * Tested on
 *	"WEMOS D1 MINI ESP32"
 * Fritzing parts
 *	ESP32 https://forum.fritzing.org/t/doit-esp32-devkit-v1-30-pin/8443/3
 *	Sensor https://github.com/OgreTransporter/fritzing-parts-extra/
 *	Relais https://forum.fritzing.org/t/kf-301-relay-request/7992/10
 * ------------------------------------------------------------------------ */
#include <Arduino.h>
#include <esp_wps.h>
#include <HTTPClient.h>
#include <incbin.h>
#include <Preferences.h>
#include <URLCode.h>
#include <WiFi.h>
/* ---------------------------------------------------------------------------
 * Edit ssid and password or use Button WPS! If you want to use a fixed ssid
 * and password because you can't reach your router or it doesn't support WPS
 * edit them in "ssidAndPassword.h". Also you can always use WPS (push button)
 * mode, which the ESP will automatically switch to, in case it can't connect
 * to the given wifi, within 40 seconds after cold start, recognizable by a
 * LED light constantly shining until the connection is successful. */
#include "ssidAndPassword.h"
/* ------------------------------------------------------------------------ */

#define HUMIDITY_SENSOR_GPIO_NUMBERS 34, 35, 39
#define PUMP_ACTIVE_GPIO_NUMBER 33
#define PUMP_CURRENT_GPIO_NUMBER 36

#define PUMP_CURRENT_MULTIPLIER 2.8

#define PAGE_DEFAULT 0
#define PAGE_HUMIDITYVAL 1
#define PAGE_HUMIDITY 2
#define PAGE_PUMPVAL 3
#define PAGE_JQUERY 4
#define PAGE_DEBUG 5
#define PAGE_DEBUG_BODY_PART 6
#define PAGE_DEBUG_BODY_FULL 7

#define DEBUG_ERROR 1
#define DEBUG_WARN 2
#define DEBUG_INFO 3
#define DEBUG_VERBOSE 4
#define DEBUG_DEBUG 5
#define DEBUG_TRACE 6

#define DEFAULT_URL_EMPTY "https://my.domain.net/empty.php"
#define DEFAULT_URL_STATUS "https://my.domain.net/status.php"

INCTXT(JQuery, "jquery-3.7.1.min.js");  // -> gJQueryData , see https://github.com/AlexIII/incbin-arduino

// Wemos D1 Mini ESP32
// https://wiki.csgalileo.org/_media/projects/internetofthings/d1_mini_esp32_-_pinout.pdf
Preferences prefs;
int sensorPort[] = { HUMIDITY_SENSOR_GPIO_NUMBERS };
int sensorPortTotalNumber = sizeof(sensorPort) / sizeof(int);
bool activeSensor[sizeof(sensorPort) / sizeof(int)];
double averageHumidity[sizeof(sensorPort) / sizeof(int)];
int sensorDryHumidity[sizeof(sensorPort) / sizeof(int)];
int sensorWetHumidity[sizeof(sensorPort) / sizeof(int)];
double overallAverageHumidity, lastOverallAverageHumidity;
WiFiServer server(80);
unsigned long startOfMainLoopMillis, lastMillis60s, startPumpMillis, lastPumpStartMillis, refreshPagesMillis;
int page, subpage;
bool checkWifiConnectionFlag = true, humidityThresholdHysteresisFalling, serialDebug, serialDebugActive;
String ssid, pwd, emptyWaterURL, reportURL;
int humidityThreshold1, humidityThreshold2, pumpDelayInMinutes, dryWetPumpBorderValue;
double pumpRuntimeInSeconds;
int currentPumpCurrentValue, lastPumpCurrentValue = 0;
#define DEBUG_BUFFER_SIZE 1000
String debugBufferArray[DEBUG_BUFFER_SIZE];
String debugBufferLine;
int debugBufferIndexNextEmpty, debugBufferIndexLastShown, debugLevel;

// Variadic template functions
template<typename... Args> void debug(const int level, Args... args) {
  if (serialDebug)
    Serial.print(args...);
  if (level <= debugLevel)
    (debugBufferLine += String(args), ...);
}
template<typename... Args> void debugln(const int level, Args... args) {
  if (serialDebug)
    Serial.println(args...);
  if (level <= debugLevel) {
    (debugBufferLine += String(args), ...);
    unsigned long now = (millis() + 500) / 1000;  // Seconds
    int d = now / 86400, h = (now / 3600) % 24, m = (now / 60) % 60, s = now % 60;
    char sign;
    switch (level) {
      case DEBUG_ERROR:
        sign = 'E';
        break;
      case DEBUG_WARN:
        sign = 'W';
        break;
      case DEBUG_INFO:
        sign = 'I';
        break;
      case DEBUG_VERBOSE:
        sign = 'V';
        break;
      case DEBUG_DEBUG:
        sign = 'D';
        break;
      case DEBUG_TRACE:
        sign = 'T';
        break;
      default:
        sign = '?';
    }
    debugBufferArray[debugBufferIndexNextEmpty] = String(d) + ":" + String(h < 10 ? "0" + String(h) : h) + ":" + String(m < 10 ? "0" + String(m) : m) + ":" + String(s < 10 ? "0" + String(s) : s) + " [" + sign + "] " + debugBufferLine;
    debugBufferLine = "";
    debugBufferIndexNextEmpty = (debugBufferIndexNextEmpty + 1) % DEBUG_BUFFER_SIZE;
    if (debugBufferIndexLastShown == debugBufferIndexNextEmpty)  // If circular buffer is full release oldest entry
      debugBufferIndexLastShown = (debugBufferIndexLastShown + 1) % DEBUG_BUFFER_SIZE;
  }
}

double mapf(const double x, const double in_min, const double in_max, const double out_min, const double out_max) {
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

void setup() {
  analogReadResolution(10);  // Backwards compatibility: 0-1023
  pinMode(LED_BUILTIN, OUTPUT);

  pinMode(PUMP_ACTIVE_GPIO_NUMBER, OUTPUT);
  digitalWrite(PUMP_ACTIVE_GPIO_NUMBER, HIGH);  // The used relais is LOW active!

  prefs.begin("p");

  int numberOfActiveSensors = 0;
  overallAverageHumidity = 0;
  for (int v = 0; v < sensorPortTotalNumber; v++) {
    char portname[10];  // gpioXX\0 gpiodryXX\0 gpiowetXX\0
    (String("gpio") + String(sensorPort[v])).toCharArray(portname, sizeof(portname));
    activeSensor[v] = prefs.getBool(portname);
    numberOfActiveSensors += (activeSensor[v] ? 1 : 0);
    debugln(DEBUG_VERBOSE, String("GPIO") + sensorPort[v] + " is " + (activeSensor[v] ? " en" : "dis") + "abled for a humidity sensor");

    (String("gpiodry") + String(sensorPort[v])).toCharArray(portname, sizeof(portname));
    if (prefs.getInt(portname) == 0)
      prefs.putInt(portname, 670);
    sensorDryHumidity[v] = prefs.getInt(portname);

    (String("gpiowet") + String(sensorPort[v])).toCharArray(portname, sizeof(portname));
    if (prefs.getInt(portname) == 0)
      prefs.putInt(portname, 260);
    sensorWetHumidity[v] = prefs.getInt(portname);

    if (activeSensor[v]) {
      for (int t = 0; t < 20; t++) {
        averageHumidity[v] = analogRead(sensorPort[v]);  // Preload with current values
        delay(5);
      }
      overallAverageHumidity += mapf(averageHumidity[v], sensorDryHumidity[v], sensorWetHumidity[v], 0, 100);
    }
  }
  overallAverageHumidity /= numberOfActiveSensors;

  ssid = prefs.getString("ssid");
  pwd = prefs.getString("password");
  humidityThreshold1 = prefs.getInt("threshold1");
  humidityThreshold2 = prefs.getInt("threshold2");
  pumpRuntimeInSeconds = prefs.getDouble("pumptime");
  pumpDelayInMinutes = prefs.getInt("pumpdelay");
  dryWetPumpBorderValue = prefs.getInt("drypump");
  emptyWaterURL = prefs.getString("emptyUrl");
  reportURL = prefs.getString("reportURL");
  if (numberOfActiveSensors == 0) {
    debugln(DEBUG_INFO, "No GPIO enabled, activating first in line");
    char portname[7];  // gpioXX\0
    (String("gpio") + String(sensorPort[0])).toCharArray(portname, sizeof(portname));
    prefs.putBool(portname, true);
    activeSensor[0] = prefs.getBool(portname);
  }
  if (humidityThreshold1 == 0) {  // Default values in case no Settings have been saved, yet
    humidityThreshold1 = 70;      // Dry: ~673, Submerged: ~264 absolute, No sensor: 0, My plant: 380(71%)
    prefs.putInt("threshold1", humidityThreshold1);
  }
  if (humidityThreshold2 == 0) {  // Default values in case no Settings have been saved, yet
    humidityThreshold2 = 50;      // Dry: ~673, Submerged: ~264 absolute
    prefs.putInt("threshold2", humidityThreshold2);
  }
  if (isnan(pumpRuntimeInSeconds)) {
    pumpRuntimeInSeconds = 1.2;
    prefs.putDouble("pumptime", pumpRuntimeInSeconds);
  }
  if (pumpDelayInMinutes == 0) {
    pumpDelayInMinutes = 150;
    prefs.putInt("pumpdelay", pumpDelayInMinutes);
  }
  if (dryWetPumpBorderValue == 0) {
    dryWetPumpBorderValue = 300;  // Dry: ~130 mA, Submerged: ~397 Ah // TODO
    prefs.putInt("drypump", dryWetPumpBorderValue);
  }
  if (emptyWaterURL == NULL) {
    emptyWaterURL = DEFAULT_URL_EMPTY;
    prefs.putString("emptyUrl", emptyWaterURL);
  }
  if (reportURL == NULL) {
    reportURL = DEFAULT_URL_STATUS;
    prefs.putString("reportURL", reportURL);
  }
  if (debugLevel < DEBUG_ERROR) {
    debugLevel = DEBUG_INFO;
    prefs.putInt("debugLevel", debugLevel);
  }

  if (!connectToWiFi()) {
    digitalWrite(LED_BUILTIN, HIGH);
    WiFi.disconnect(true, true);  // Wipe credentials or it look like it won't work!
    wpsSetup();
    checkWifiConnectionFlag = false;
  }
}

void loop() {
  if (startOfMainLoopMillis == 0)
    startOfMainLoopMillis = millis();  // For averaging stuff warmup phase

  if (serialDebug && !serialDebugActive) {  // Only initialize once
    Serial.begin(115200);
    serialDebugActive = true;
  }

  debugln(DEBUG_TRACE, __LINE__);
  webServerReaction();
  debugln(DEBUG_TRACE, __LINE__);

  overallAverageHumidity = 0;
  int sensorcount = 0;
  for (int v = 0; v < sensorPortTotalNumber; v++) {
    if (activeSensor[v]) {
      averageHumidity[v] = averageHumidity[v] * .98 + analogRead(sensorPort[v]) * .02;
      overallAverageHumidity += mapf(averageHumidity[v], sensorDryHumidity[v], sensorWetHumidity[v], 0, 100);
      sensorcount++;
    }
  }
  overallAverageHumidity /= sensorcount;

  debugln(DEBUG_TRACE, __LINE__);

  unsigned long currentMillis = millis();
  // Pump mode hysteresis for mold prevention - only check for switching down after pump cooldown!
  if (!humidityThresholdHysteresisFalling && overallAverageHumidity >= humidityThreshold1 && (lastPumpStartMillis == 0 || currentMillis > lastPumpStartMillis + pumpDelayInMinutes * 60000)) {
    debugln(DEBUG_TRACE, __LINE__);
    debugln(DEBUG_INFO, "Average humidity (" + String(overallAverageHumidity) + ") is above upper threshold (" + String(humidityThreshold1) + "), switching to falling mode");
    humidityThresholdHysteresisFalling = true;
  } else if (humidityThresholdHysteresisFalling && overallAverageHumidity <= humidityThreshold2) {
    debugln(DEBUG_TRACE, __LINE__);
    debugln(DEBUG_INFO, "Average humidity (" + String(overallAverageHumidity) + ") is below lower threshold (" + String(humidityThreshold2) + "), switching to rising mode");
    humidityThresholdHysteresisFalling = false;
  }

  currentMillis = millis();
  if (abs(lastOverallAverageHumidity - overallAverageHumidity) >= .5 && currentMillis - startOfMainLoopMillis > 5000) {  // Only after average warmup
    debugln(DEBUG_TRACE, __LINE__);
    doStatusReporting();
    lastOverallAverageHumidity = overallAverageHumidity;
  }

  currentMillis = millis();
  if (currentMillis - lastMillis60s > 60000) {
    debugln(DEBUG_TRACE, __LINE__);
    if (checkWifiConnectionFlag) {
      debugln(DEBUG_TRACE, __LINE__);
      doCheckWiFiConnection();
      debugln(DEBUG_VERBOSE, getAvgValues());
    }
    lastMillis60s += 60000;
  }

  currentMillis = millis();
  if (startPumpMillis == 0 && !humidityThresholdHysteresisFalling && overallAverageHumidity < humidityThreshold1 && currentMillis - startOfMainLoopMillis > 5000 && (lastPumpStartMillis == 0 || currentMillis > lastPumpStartMillis + pumpDelayInMinutes * 60000)) {  // Only after average warmup
    debugln(DEBUG_TRACE, __LINE__);
    debugln(DEBUG_INFO, "Average humidity (" + String(overallAverageHumidity) + ") is lower than upper threshold (" + String(humidityThreshold1) + "), starting pump");
    startPump();
  }

  if (currentPumpCurrentValue > 0) {
    debugln(DEBUG_TRACE, __LINE__);
    debugln(DEBUG_VERBOSE, String(currentPumpCurrentValue) + " mA");
  }

  currentMillis = millis();
  if (startPumpMillis != 0 && (currentMillis - startPumpMillis > pumpRuntimeInSeconds * 1000 || currentMillis < startPumpMillis)) {  // Force switch off pump in case of timer overflow (every ~52 days)
    stopPump();
    debugln(DEBUG_TRACE, __LINE__);
    debugln(DEBUG_INFO, "Average current: " + currentPumpCurrentValue);
    lastPumpCurrentValue = currentPumpCurrentValue;
    if (currentPumpCurrentValue < dryWetPumpBorderValue) {
      debugln(DEBUG_TRACE, __LINE__);
      debugln(DEBUG_WARN, "Average current (" + String(currentPumpCurrentValue) + ") is lower than threshold (" + String(dryWetPumpBorderValue) + "), reporting water is empty");
      doEmptyWaterWarning();
    }
  }

  for (int t = 0; t < 20; t++) {
    currentPumpCurrentValue = currentPumpCurrentValue * .99 + (analogRead(PUMP_CURRENT_GPIO_NUMBER) * PUMP_CURRENT_MULTIPLIER) * .01;
    delay(1);
  }

  currentMillis = millis();
  delay(startPumpMillis == 0 && currentMillis - refreshPagesMillis > 5000 ? 2000 : 2);  // React quickly on startup and page load as well as when pump is running
}

void doEmptyWaterWarning() {
  debugln(DEBUG_TRACE, __LINE__);
  if (emptyWaterURL == "" || emptyWaterURL == DEFAULT_URL_EMPTY) {
    debugln(DEBUG_VERBOSE, "Empty water supply url not set, aborting");
    return;
  }
  if (WiFi.status() == WL_CONNECTED) {
    debugln(DEBUG_DEBUG, "Request: " + emptyWaterURL);
    HTTPClient http;
    http.begin(String(emptyWaterURL));
    int httpResponseCode = http.GET();
    debugln(DEBUG_VERBOSE, "Response: " + httpResponseCode);
    if (httpResponseCode != 200)
      debugln(DEBUG_ERROR, http.getString());
    http.end();
  } else {
    debugln(DEBUG_WARN, "WiFi Disconnected");
  }
}

void doStatusReporting() {
  debugln(DEBUG_TRACE, __LINE__);
  if (reportURL == "" || reportURL == DEFAULT_URL_STATUS) {
    debugln(DEBUG_VERBOSE, "Status url not set, aborting");
    return;
  }
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(String(reportURL) + "?oah=" + overallAverageHumidity + "&t1=" + humidityThreshold1 + "&t2=" + humidityThreshold2);
    int httpResponseCode = http.GET();
    if (httpResponseCode != 200) {
      debugln(DEBUG_ERROR, "Error while reporting: " + http.getString());
    }
    http.end();
  } else {
    debugln(DEBUG_WARN, "WiFi Disconnected");
  }
}

String getAvgValues() {
  debugln(DEBUG_TRACE, __LINE__);
  char buffer[7];
  String retVal = "[";
  for (int v = 0; v < sensorPortTotalNumber; v++) {
    dtostrf(averageHumidity[v], 1, 1, buffer);
    retVal += buffer;
    if (v < sensorPortTotalNumber - 1)
      retVal += ",";
  }
  return retVal + "]";
}

void startPump() {
  debugln(DEBUG_INFO, "Starting Pump");
  digitalWrite(PUMP_ACTIVE_GPIO_NUMBER, LOW);
  startPumpMillis = millis();
  lastPumpStartMillis = startPumpMillis;
}

void stopPump() {
  debugln(DEBUG_INFO, "Stopping Pump");
  digitalWrite(PUMP_ACTIVE_GPIO_NUMBER, HIGH);
  startPumpMillis = 0;
}

bool connectToWiFi() {
  // Try the stored values for ssid and password, if any
  if (ssid != NULL && pwd != NULL) {
    debugln(DEBUG_INFO, "Connecting to SSID: " + ssid);
    WiFi.begin(ssid, pwd);
    for (int t = 0; t < 20; t++) {  // Try for 20 seconds
      if (WiFi.status() == WL_CONNECTED)
        break;
      delay(1000);
      debug(DEBUG_INFO, ".");
    }
    debugln(DEBUG_INFO);
  }

  // Try the values from the "ssidAndPassword.h" file
  if (WiFi.status() != WL_CONNECTED) {
    debugln(DEBUG_INFO, "Connecting to SSID: " + wifiName);
    WiFi.begin(wifiName, wifiPassword);
    for (int t = 0; t < 20; t++) {  // Try for 20 seconds
      if (WiFi.status() == WL_CONNECTED) {
        ssid = wifiName;
        prefs.putString("ssid", ssid);
        pwd = wifiPassword;
        prefs.putString("password", pwd);
        break;
      }
      delay(1000);
      debug(DEBUG_INFO, ".");
    }
    debugln(DEBUG_INFO);
  }

  if (WiFi.status() == WL_CONNECTED) {
    digitalWrite(LED_BUILTIN, HIGH);
    debugln(DEBUG_VERBOSE, "Success: " + WiFi.localIP().toString());
    server.begin();
    delay(100);
    digitalWrite(LED_BUILTIN, LOW);
    return true;
  } else {
    return false;
  }
}

void doCheckWiFiConnection() {
  debugln(DEBUG_TRACE, __LINE__);
  if (WiFi.status() != WL_CONNECTED) {
    debugln(DEBUG_WARN, "Connection lost, reconnecting...");
    connectToWiFi();
  }
}

#define ESP_WPS_MODE WPS_TYPE_PBC
#define ESP_MANUFACTURER "ESPRESSIF"
#define ESP_MODEL_NUMBER "ESP32-S3"
#define ESP_MODEL_NAME "ESP32S3 Dev Module"
#define ESP_DEVICE_NAME "ESP STATION"

static esp_wps_config_t config;

void wpsInitConfig() {
  config.wps_type = ESP_WPS_MODE;
  strcpy(config.factory_info.manufacturer, ESP_MANUFACTURER);
  strcpy(config.factory_info.model_number, ESP_MODEL_NUMBER);
  strcpy(config.factory_info.model_name, ESP_MODEL_NAME);
  strcpy(config.factory_info.device_name, ESP_DEVICE_NAME);
}

void wpsStart() {
  if (esp_wifi_wps_enable(&config)) {
    debugln(DEBUG_ERROR, "WPS Enable Failed");
  } else if (esp_wifi_wps_start(0)) {
    debugln(DEBUG_ERROR, "WPS Start Failed");
  }
}

void wpsStop() {
  if (esp_wifi_wps_disable()) {
    debugln(DEBUG_ERROR, "WPS Disable Failed");
  }
}

String wpspin2string(uint8_t a[]) {
  char wps_pin[9];
  for (int i = 0; i < 8; i++) {
    wps_pin[i] = a[i];
  }
  wps_pin[8] = '\0';
  return (String)wps_pin;
}

// WARNING: WiFiEvent is called from a separate FreeRTOS task (thread)!
void WiFiEvent(WiFiEvent_t event, arduino_event_info_t info) {
  debugln(DEBUG_TRACE, __LINE__);
  switch (event) {
    case ARDUINO_EVENT_WIFI_STA_START:
      debugln(DEBUG_INFO, "Station Mode Started");
      break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      debugln(DEBUG_INFO, "Connecting to SSID: " + WiFi.SSID());
      debugln(DEBUG_TRACE, "Password: " + WiFi.psk());
      ssid = WiFi.SSID();
      pwd = WiFi.psk();
      prefs.putString("ssid", ssid);
      prefs.putString("password", pwd);
      debugln(DEBUG_VERBOSE, "Success: " + WiFi.localIP().toString());
      digitalWrite(LED_BUILTIN, LOW);
      checkWifiConnectionFlag = true;
      server.begin();
      break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      debugln(DEBUG_WARN, "Disconnected from station, attempting reconnection");
      WiFi.reconnect();
      break;
    case ARDUINO_EVENT_WPS_ER_SUCCESS:
      debugln(DEBUG_INFO, "WPS Successful, stopping WPS and connecting to: " + String(WiFi.SSID()));
      wpsStop();
      delay(10);
      WiFi.begin();
      break;
    case ARDUINO_EVENT_WPS_ER_FAILED:
      debugln(DEBUG_WARN, "WPS Failed, retrying");
      wpsStop();
      wpsStart();
      break;
    case ARDUINO_EVENT_WPS_ER_TIMEOUT:
      debugln(DEBUG_WARN, "WPS Timedout, retrying");
      wpsStop();
      wpsStart();
      break;
    case ARDUINO_EVENT_WPS_ER_PIN:
      debugln(DEBUG_DEBUG, "WPS_PIN = " + wpspin2string(info.wps_er_pin.pin_code));
      break;
    default:
      break;
  }
}

void webServerReaction() {
  debugln(DEBUG_TRACE, __LINE__);
  WiFiClient client = server.accept();
  if (client) {
    String currentLine = "";
    while (client.connected()) {
      if (client.available()) {
        char c = client.read();
        if (c == '\n') {
          debugln(DEBUG_DEBUG, currentLine);  // DEBUG
          if (currentLine.startsWith("GET /H?")) {
            debugln(DEBUG_DEBUG, client.remoteIP().toString() + " -> Humidity value block: " + currentLine);
            page = PAGE_HUMIDITYVAL;
            subpage = currentLine.substring(strlen("GET /H?")).toInt();
          } else if (currentLine.startsWith("GET /H")) {
            debugln(DEBUG_DEBUG, client.remoteIP().toString() + " -> Humidity all values block: " + currentLine);
            page = PAGE_HUMIDITYVAL;
            subpage = -1;
          } else if (currentLine.startsWith("GET /S")) {
            debugln(DEBUG_DEBUG, client.remoteIP().toString() + " -> Switch hysteresis direction: " + currentLine);
            humidityThresholdHysteresisFalling = !humidityThresholdHysteresisFalling;
          } else if (currentLine.startsWith("GET /O")) {
            debugln(DEBUG_DEBUG, client.remoteIP().toString() + " -> Average humidity block: " + currentLine);
            page = PAGE_HUMIDITY;
          } else if (currentLine.startsWith("GET /P")) {
            debugln(DEBUG_DEBUG, client.remoteIP().toString() + " -> Pump current block: " + currentLine);
            page = PAGE_PUMPVAL;
          } else if (currentLine.startsWith("GET /T")) {
            debugln(DEBUG_DEBUG, client.remoteIP().toString() + " -> Pump test activate: " + currentLine);
            startPump();
          } else if (currentLine.startsWith("GET /E")) {
            debugln(DEBUG_DEBUG, client.remoteIP().toString() + " -> Empty test activate: " + currentLine);
            doEmptyWaterWarning();
          } else if (currentLine.startsWith("GET /R")) {
            debugln(DEBUG_DEBUG, client.remoteIP().toString() + " -> Report test activate: " + currentLine);
            doStatusReporting();
          } else if (currentLine.startsWith("GET /JQ.js")) {
            debugln(DEBUG_DEBUG, client.remoteIP().toString() + " -> JQuery file: " + currentLine);
            page = PAGE_JQUERY;
          } else if (currentLine.startsWith("GET /Da")) {
            debugln(DEBUG_DEBUG, client.remoteIP().toString() + " -> Debug main page: " + currentLine);
            page = PAGE_DEBUG;
            refreshPagesMillis = millis();
          } else if (currentLine.startsWith("GET /Db")) {
            debugln(DEBUG_DEBUG, client.remoteIP().toString() + " -> Debug body (part): " + currentLine);
            page = PAGE_DEBUG_BODY_PART;
          } else if (currentLine.startsWith("GET /Dc")) {
            debugln(DEBUG_DEBUG, client.remoteIP().toString() + " -> Debug body (full): " + currentLine);
            page = PAGE_DEBUG_BODY_FULL;
          } else if (currentLine.startsWith("GET /?")) {
            debugln(DEBUG_DEBUG, client.remoteIP().toString() + " -> Saving default config page: " + currentLine);
            for (int v = 0; v < sensorPortTotalNumber; v++) {
              char portname[10];  // gpioXX\0 gpiodryXX\0 gpiowetXX\0
              (String("gpio") + String(sensorPort[v])).toCharArray(portname, sizeof(portname));
              activeSensor[v] = currentLine.indexOf(portname) != -1;
              prefs.putBool(portname, activeSensor[v]);

              (String("gpiodry") + String(sensorPort[v])).toCharArray(portname, sizeof(portname));
              sensorDryHumidity[v] = currentLine.substring(currentLine.indexOf(portname) + strlen(portname) + 1).toInt();
              prefs.putInt(portname, sensorDryHumidity[v]);

              (String("gpiowet") + String(sensorPort[v])).toCharArray(portname, sizeof(portname));
              sensorWetHumidity[v] = currentLine.substring(currentLine.indexOf(portname) + strlen(portname) + 1).toInt();
              prefs.putInt(portname, sensorWetHumidity[v]);

              refreshPagesMillis = millis();
            }

            humidityThreshold1 = currentLine.substring(currentLine.indexOf("threshold1") + String("threshold1").length() + 1).toInt();
            humidityThreshold1 = humidityThreshold1 < 1 ? 1 : (humidityThreshold1 > 99 ? 99 : humidityThreshold1);
            prefs.putInt("threshold1", humidityThreshold1);

            humidityThreshold2 = currentLine.substring(currentLine.indexOf("threshold2") + String("threshold2").length() + 1).toInt();
            humidityThreshold2 = humidityThreshold2 < 1 ? 1 : (humidityThreshold2 > 99 ? 99 : humidityThreshold2);
            prefs.putInt("threshold2", humidityThreshold2);

            pumpRuntimeInSeconds = currentLine.substring(currentLine.indexOf("pumptime") + String("pumptime").length() + 1).toDouble();
            pumpRuntimeInSeconds = pumpRuntimeInSeconds < 1 ? 1 : (pumpRuntimeInSeconds > 300 ? 300 : pumpRuntimeInSeconds);
            prefs.putDouble("pumptime", pumpRuntimeInSeconds);

            pumpDelayInMinutes = currentLine.substring(currentLine.indexOf("pumpdelay") + String("pumpdelay").length() + 1).toInt();
            pumpDelayInMinutes = pumpDelayInMinutes < 1 ? 1 : (pumpDelayInMinutes > 1440 ? 1440 : pumpDelayInMinutes);
            prefs.putInt("pumpdelay", pumpDelayInMinutes);

            dryWetPumpBorderValue = currentLine.substring(currentLine.indexOf("drypump") + String("drypump").length() + 1).toInt();
            dryWetPumpBorderValue = dryWetPumpBorderValue < 1 ? 1 : (dryWetPumpBorderValue > 1022 ? 1022 : dryWetPumpBorderValue);
            prefs.putInt("drypump", dryWetPumpBorderValue);

            int start, a, b;
            URLCode url;

            start = currentLine.indexOf("emptyUrl") + String("emptyUrl").length() + 1;
            a = currentLine.indexOf("&", start);
            b = currentLine.indexOf(" ", start);
            a = a == -1 ? 99999 : a;
            b = b == -1 ? 99999 : b;
            url.urlcode = currentLine.substring(start, min(a, b));
            url.urldecode();
            emptyWaterURL = url.strcode;
            prefs.putString("emptyUrl", emptyWaterURL);

            start = currentLine.indexOf("reportURL") + String("reportURL").length() + 1;
            a = currentLine.indexOf("&", start);
            b = currentLine.indexOf(" ", start);
            a = a == -1 ? 99999 : a;
            b = b == -1 ? 99999 : b;
            url.urlcode = currentLine.substring(start, min(a, b));
            url.urldecode();
            reportURL = url.strcode;
            prefs.putString("reportURL", reportURL);

            debugLevel = currentLine.substring(currentLine.indexOf("debugLevel") + String("debugLevel").length() + 1).toInt();
            debugLevel = debugLevel < DEBUG_ERROR ? DEBUG_ERROR : (debugLevel > DEBUG_TRACE ? DEBUG_TRACE : debugLevel);
            prefs.putInt("debugLevel", debugLevel);

            serialDebug = currentLine.indexOf("serialDebug") != -1;
          } else {
            debugln(DEBUG_DEBUG, client.remoteIP().toString() + " -> Default config page: " + currentLine);
            refreshPagesMillis = millis();  // Clean main page
          }
          if (currentLine.length() == 0) {
            client.println("HTTP/1.1 200 OK");
            if (page == PAGE_HUMIDITYVAL) {
              client.println("Content-type:application/json;charset=utf-8");
              client.println();
              if (subpage >= 0 && subpage < sensorPortTotalNumber)
                client.println(averageHumidity[subpage]);
              else
                client.println(getAvgValues());
            } else if (page == PAGE_HUMIDITY) {
              client.println("Content-type:application/json;charset=utf-8");
              client.println();
              client.println(overallAverageHumidity);
            } else if (page == PAGE_PUMPVAL) {
              client.println("Content-type:application/json;charset=utf-8");
              client.println();
              client.print(lastPumpCurrentValue);
            } else if (page == PAGE_JQUERY) {
              client.println("Content-type:application/javascript;charset=utf-8");
              client.println("Pragma: public");
              client.println("Cache-Control: public, max-age=3600;");
              client.println("Last-Modified: Wed, 25 Jun 2025 11:21:08 GMT");
              client.print("Content-Length: ");
              client.println(gJQuerySize);
              client.println();
              client.println(gJQueryData);
            } else if (page == PAGE_DEBUG) {
              client.println("Content-type:text/html;charset=utf-8");
              client.println();
              client.println("<html><head><script src=\"JQ.js\"></script><script>function go(){"
                             "y=$(document).height()-window.pageYOffset-window.innerHeight;"
                             "$.get(\"/Db\",function(x){"
                             "$('#d').append(x);"
                             "if(y<30)$(\"html,body\").animate({scrollTop:$(document).height()},\"slow\")"
                             "});"
                             "setTimeout(go,5000)"
                             "}$(document).ready(function(){$('#d').load(\"/Dc\",go())})</script></head><body style=\"background:#020;color:#0f0\"><pre id=\"d\"></pre></body></html>");
            } else if (page == PAGE_DEBUG_BODY_FULL) {
              client.println("Content-type:text/plain;charset=utf-8");
              client.println();
              for (int t = 0; t < DEBUG_BUFFER_SIZE; t++) {
                const int idx = (debugBufferIndexNextEmpty + t) % DEBUG_BUFFER_SIZE;
                if (debugBufferArray[idx] != "")
                  client.println(debugBufferArray[idx]);
              }
              debugBufferIndexLastShown = debugBufferIndexNextEmpty;
            } else if (page == PAGE_DEBUG_BODY_PART) {
              client.println("Content-type:text/plain;charset=utf-8");
              client.println();
              const int used = (debugBufferIndexNextEmpty + DEBUG_BUFFER_SIZE - debugBufferIndexLastShown) % DEBUG_BUFFER_SIZE;
              for (int t = 0; t < used; t++)
                client.println(debugBufferArray[(debugBufferIndexLastShown + t) % DEBUG_BUFFER_SIZE]);
              debugBufferIndexLastShown = debugBufferIndexNextEmpty;
            } else {
              client.println("Content-type:text/html;charset=utf-8");
              client.println();
              client.println("<html>");
              client.println("<head>");
              client.println("	<script src=\"JQ.js\"></script>");
              client.println("	<script>");
              client.println("		function a() {");
              client.println("			$(\"#pc\").load(\"/P\");");
              client.println("			$(\"#ho\").load(\"/O\");");
              for (int v = 0; v < sensorPortTotalNumber; v++) {
                client.println("			$(\"#h" + String(sensorPort[v]) + "\").load(\"/H?" + String(v) + "\");");
              }
              client.println("			setTimeout(a,10000);");
              client.println("		}");
              client.println("		$(document).ready(function(){a()});");
              client.println("	</script>");
              client.println("	<style>");
              client.println("		body {");
              client.println("			font-family:Arial,Helvetica,sans-serif;");
              client.println("			background:#ddd;");
              client.println("		}");
              client.println("	</style>");
              client.println("</head>");
              client.println("<body>");
              client.println("	<table width=\"100%\" height=\"100%\"><tr align=\"center\"><td>");
              client.println("		<form action=\"/\">");
              client.println("			<table>");
              client.println("				<tr>");
              client.println("					<th>Sensor</th>");
              client.println("					<th>Active</th>");
              client.println("					<th title=\"Sensor value when clean and surrounded by air\">Dry</th>");
              client.println("					<th title=\"Sensor value when submerged in water\">Wet</th>");
              client.println("					<th>Now</th>");
              client.println("				</tr>");
              for (int v = 0; v < sensorPortTotalNumber; v++) {
                client.println("				<tr align=\"center\">");
                client.println("					<td>GPIO " + String(sensorPort[v]) + "</td>");
                client.println("					<td><input type=\"checkbox\" name=\"gpio" + String(sensorPort[v]) + "\" value=\"1\"" + String(activeSensor[v] ? " checked" : "") + "></td>");
                client.println("					<td title=\"Sensor value when clean and surrounded by air\"><input name=\"gpiodry" + String(sensorPort[v]) + "\" size=\"5\" value=\"" + String(sensorDryHumidity[v]) + "\"></td>");
                client.println("					<td title=\"Sensor value when submerged in water\"><input name=\"gpiowet" + String(sensorPort[v]) + "\" size=\"5\" value=\"" + String(sensorWetHumidity[v]) + "\"></td>");
                client.println("					<td><span id=\"h" + String(sensorPort[v]) + "\">X</span></td>");
                client.println("				</tr>");
              }
              client.println("			</table>");
              client.println("			<br>");
              client.println("			<table>");
              client.println("				<tr title=\"When reaching or exceeding this humidity level cyclic pumping will be deactivated for mold prevention until the lower level is reached.\">");
              client.println("					<td>Upper hysteresis level:</td>");
              client.println("					<td><input name=\"threshold1\" size=\"5\" value=\"" + String(humidityThreshold1) + "\">&nbsp;% &nbsp; (Now: <span id=\"ho\">X</span> %)</td>");
              client.println("				</tr>");
              client.println("				<tr title=\"When reaching or undercutting this humidity level cyclic pumping will be activated until the upper level is reached.\">");
              client.println("					<td>Lower hysteresis level:</td>");
              client.println("					<td><input name=\"threshold2\" size=\"5\" value=\"" + String(humidityThreshold2) + "\">&nbsp;%</td>");
              client.println("					<td title=\"Switch current direction of humidity hysteresis. See Debug page and debug level >= INFO for state changes.\"><a href=\"/S\">Switch</a></td>");
              client.println("				</tr>");
              client.println("				<tr title=\"Time the pump will be activated per pump cycle (or when testing) in seconds.\">");
              client.println("					<td>Pump runtime:</td>");
              client.println("					<td><input name=\"pumptime\" size=\"5\" value=\"" + String(pumpRuntimeInSeconds) + "\">&nbsp;s</td>");
              client.println("				</tr>");
              client.println("				<tr title=\"Minimum delay between the starts of two pump activations (not including pump test) in minutes.\">");
              client.println("					<td>Pump delay:</td>");
              client.println("					<td><input name=\"pumpdelay\" size=\"5\" value=\"" + String(pumpDelayInMinutes) + "\">&nbsp;m</td>");
              client.println("				</tr>");
              client.println("				<tr title=\"This value determines if the water supply is empty. It does so by measuring the electric current of the pump which is way lower when no water is pumped. Set this value to somewhere in between the current when pumping water and when running dry.\">");
              client.println("					<td>Water detection:</td>");
              client.println("					<td><input name=\"drypump\" size=\"5\" value=\"" + String(dryWetPumpBorderValue) + "\">&nbsp;mA &nbsp; (Last: <span id=\"pc\">X</span> mA)</td>");
              client.println("					<td><a href=\"/T\">Test</a></td>");
              client.println("				</tr>");
              client.println("				<tr title=\"This URL will be called when there is no more water and the pump is running dry.\">");
              client.println("					<td>Empty warning to:</td>");
              client.println("					<td><input name=\"emptyUrl\" maxlength=\"255\" size=\"40\" value=\"" + String(emptyWaterURL) + "\">&nbsp;</td>");
              client.println("					<td><a href=\"/E\">Test</a></td>");
              client.println("				</tr>");
              client.println("				<tr title=\"This URL will be called cyclically to send the current humidity value to e.g. a database.\">");
              client.println("					<td>Status reports to:</td>");
              client.println("					<td><input name=\"reportURL\" maxlength=\"255\" size=\"40\" value=\"" + String(reportURL) + "\">&nbsp;</td>");
              client.println("					<td><a href=\"/R\">Test</a></td>");
              client.println("				</tr>");
              client.println("				<tr>");
              client.println("					<td>Debug Level</td>");
              client.println("					<td>");
              client.println("						<select name=\"debugLevel\">");
              client.println("							<option value=\"" + String(DEBUG_ERROR) + "\" " + (debugLevel == DEBUG_ERROR ? " selected=\"true\"" : "") + ">Error</option>");
              client.println("							<option value=\"" + String(DEBUG_WARN) + "\" " + (debugLevel == DEBUG_WARN ? " selected=\"true\"" : "") + ">Warn</option>");
              client.println("							<option value=\"" + String(DEBUG_INFO) + "\" " + (debugLevel == DEBUG_INFO ? " selected=\"true\"" : "") + ">Info</option>");
              client.println("							<option value=\"" + String(DEBUG_VERBOSE) + "\" " + (debugLevel == DEBUG_VERBOSE ? " selected=\"true\"" : "") + ">Verbose</option>");
              client.println("							<option value=\"" + String(DEBUG_DEBUG) + "\" " + (debugLevel == DEBUG_DEBUG ? " selected=\"true\"" : "") + ">Debug</option>");
              client.println("							<option value=\"" + String(DEBUG_TRACE) + "\" " + (debugLevel == DEBUG_TRACE ? " selected=\"true\"" : "") + ">Trace</option>");
              client.println("						</select>");
              client.println("						&nbsp; &nbsp; &nbsp;");
              client.println("						<span title=\"Debugging informationen will be transmitted additionally to the serial interface using 115200 Baud.\">");
              client.println("							Serial Debug");
              client.println("							<input type=\"checkbox\" name=\"serialDebug\"" + String(serialDebug ? " checked" : "") + ">");
              client.println("						</span>");
              client.println("					</td>");
              client.println("				</tr>");
              client.println("				<tr>");
              client.println("					<td colspan=\"2\" align=\"center\"><input type=\"submit\" value=\"save\"></td>");
              client.println("					<td><a href=\"/Da\">Debug</a></td>");
              client.println("				</tr>");
              client.println("			</table>");
              client.println("		</form>");
              client.println("		<p>");
              client.println("			<a href=\"/H\">Sensor values</a> &nbsp; <a href=\"/O\">Average humidity</a> &nbsp; <a href=\"/P\">Pump current</a> &nbsp; <a href=\"/JQ.js\">JQuery</a>");
              client.println("		</p>");
              client.println("		<p>");
              client.println("			<small><a href=\"https://github.com/Joghurt/Plantcare/\" target=\"_blank\">https://github.com/Joghurt/Plantcare/</a></small>");
              client.println("		</p>");
              client.println("	</td></tr></table>");
              client.println("</body>");
              client.println("</html>");
            }
            page = PAGE_DEFAULT;
            break;  // Exit while() loop
          } else {
            currentLine = "";
          }
        } else if (c != '\r') {
          currentLine += c;
        }
      }
    }
    client.stop();
    debugln(DEBUG_DEBUG, "Client Disconnected.");
  }
}

void wpsSetup() {
  WiFi.onEvent(WiFiEvent);  // Will call WiFiEvent() from another thread.
  WiFi.mode(WIFI_MODE_STA);
  debugln(DEBUG_INFO, "Starting WPS");
  wpsInitConfig();
  delay(1000);
  wpsStart();
}
