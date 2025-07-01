// Needs Boards
//  "esp32 by Espressif Systems" https://github.com/espressif/arduino-esp32
// Needs Libraries
//  "URLCode by XieXuan"         https://github.com/MR-XieXuan/URLCode_for_Arduino
//  "incbin by Dale Weiler"      https://github.com/AlexIII/incbin-arduino
// Tested on
//  "WEMOS D1 MINI ESP32"
// Fritzing parts
//  ESP32  https://forum.fritzing.org/t/doit-esp32-devkit-v1-30-pin/8443/3
//  Sensor https://github.com/OgreTransporter/fritzing-parts-extra/
//  Relais https://forum.fritzing.org/t/kf-301-relay-request/7992/10

#include <Arduino.h>
#include <WiFi.h>
#include <esp_wps.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <URLCode.h>
#include <incbin.h>

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

INCTXT(JQuery, "jquery-3.7.1.min.js");  // -> gJQueryData , see https://github.com/AlexIII/incbin-arduino

// Wemos D1 Mini ESP32
// https://wiki.csgalileo.org/_media/projects/internetofthings/d1_mini_esp32_-_pinout.pdf
Preferences prefs;
int sensorPort[] = { HUMIDITY_SENSOR_GPIO_NUMBERS };
int sensorPortTotalNumber = sizeof(sensorPort) / sizeof(int);
bool activeSensor[sizeof(sensorPort) / sizeof(int)];
double averageHumidity[sizeof(sensorPort) / sizeof(int)];
double overallAverageHumidity, lastOverallAverageHumidity;
WiFiServer server(80);
unsigned long startOfMainLoop, lastMillis60s, startPumpMillis, lastPumpRunStart;
int page;
bool checkWifiConnectionFlag = true, checkAverage, humidityThresholdHysteresisFalling;
char ssid[32 + 1], pwd[63 + 1], emptyWaterURL[256], reportURL[256];
int humidityThreshold1, humidityThreshold2, pumpDelayInMinutes, dryWetPumpBorderValue;
double pumpRuntimeInSeconds;
int lastPumpCurrentValue;
#define DEBUG_BUFFER_SIZE 1000
String debugBufferArray[DEBUG_BUFFER_SIZE];
String debugBufferLine;
int debugBufferIndexNextEmpty, debugBufferIndexLastShown;

// Variadic template function
template<typename... Args> void debug(Args... args) {
  (debugBufferLine += String(args), ...);
}
template<typename... Args> void debugln(Args... args) {
  (debugBufferLine += String(args), ...);
  //debugln(debugBufferLine);
  unsigned long now = (millis() + 500) / 1000;  // Seconds
  int d = now / 86400, h = (now / 3600) % 24, m = (now / 60) % 60, s = now % 60;
  debugBufferArray[debugBufferIndexNextEmpty] = String(d) + ":" + String(h < 10 ? "0" + String(h) : h) + ":" + String(m < 10 ? "0" + String(m) : m) + ":" + String(s < 10 ? "0" + String(s) : s) + " - " + debugBufferLine;
  debugBufferLine = "";
  debugBufferIndexNextEmpty = (debugBufferIndexNextEmpty + 1) % DEBUG_BUFFER_SIZE;
  if (debugBufferIndexLastShown == debugBufferIndexNextEmpty)  // If circular buffer is full release oldest entry
    debugBufferIndexLastShown = (debugBufferIndexLastShown + 1) % DEBUG_BUFFER_SIZE;
}

void setup() {
  //Serial.begin(115200);
  analogReadResolution(10);  // Backwards compatibility: 0-1023
  pinMode(LED_BUILTIN, OUTPUT);

  pinMode(PUMP_ACTIVE_GPIO_NUMBER, OUTPUT);
  digitalWrite(PUMP_ACTIVE_GPIO_NUMBER, HIGH);  // The used relais is LOW active!

  prefs.begin("p");

  bool anySensorActive = false;
  overallAverageHumidity = 0;
  for (int v = 0; v < sensorPortTotalNumber; v++) {
    averageHumidity[v] = analogRead(sensorPort[v]);  // Preload with current values
    overallAverageHumidity += averageHumidity[v];    // TODO: Use percentage, not absolute values!
    char portname[7];                                // gpioXX\0
    (String("gpio") + String(sensorPort[v])).toCharArray(portname, sizeof(portname));
    activeSensor[v] = prefs.getBool(portname);
    if (activeSensor[v])
      anySensorActive = true;
    debugln(String("GPIO") + sensorPort[v] + " is " + (activeSensor[v] ? " en" : "dis") + "abled for a humidity sensor");
  }
  overallAverageHumidity /= sensorPortTotalNumber;

  prefs.getBytes("ssid", &ssid, sizeof(ssid));
  prefs.getBytes("password", &pwd, sizeof(pwd));
  humidityThreshold1 = prefs.getInt("threshold1");
  humidityThreshold2 = prefs.getInt("threshold2");
  pumpRuntimeInSeconds = prefs.getDouble("pumptime");
  pumpDelayInMinutes = prefs.getInt("pumpdelay");
  dryWetPumpBorderValue = prefs.getInt("drypump");
  prefs.getBytes("emptyUrl", &emptyWaterURL, sizeof(emptyWaterURL));
  prefs.getBytes("reportURL", &reportURL, sizeof(reportURL));
  if (!anySensorActive) {
    debugln("No GPIO enabled, activating first in line");
    char portname[7];  // gpioXX\0
    (String("gpio") + String(sensorPort[0])).toCharArray(portname, sizeof(portname));
    prefs.putBool(portname, true);
    activeSensor[0] = prefs.getBool(portname);
  }
  if (humidityThreshold1 == 0) {  // Default values in case no Settings have been saved, yet
    humidityThreshold1 = 78;      // Dry: ~673, Submerged: ~264 absolute, No sensor: 0, My plant: 380(71%)
    prefs.putInt("threshold1", humidityThreshold1);
  }
  if (humidityThreshold2 == 0) {  // Default values in case no Settings have been saved, yet
    humidityThreshold2 = 54;      // Dry: ~673, Submerged: ~264 absolute
    prefs.putInt("threshold2", humidityThreshold2);
  }
  if (pumpRuntimeInSeconds == 0) {
    pumpRuntimeInSeconds = 2.5;
    prefs.putDouble("pumptime", pumpRuntimeInSeconds);
  }
  if (pumpDelayInMinutes == 0) {
    pumpDelayInMinutes = 30;
    prefs.putInt("pumpdelay", pumpDelayInMinutes);
  }
  if (dryWetPumpBorderValue == 0) {
    dryWetPumpBorderValue = 300;  // Dry: ~130 mA, Submerged: ~397 Ah // TODO
    prefs.putInt("drypump", dryWetPumpBorderValue);
  }
  if (emptyWaterURL == "") {
    strcpy(emptyWaterURL, "https://my.domain.net/empty.php");
    prefs.putBytes("emptyUrl", (byte*)(&emptyWaterURL), sizeof(emptyWaterURL));
  }
  if (reportURL == "") {
    strcpy(reportURL, "https://my.domain.net/status.php");
    prefs.putBytes("reportURL", (byte*)(&reportURL), sizeof(reportURL));
  }

  if (!connectToWiFi()) {
    digitalWrite(LED_BUILTIN, HIGH);
    WiFi.disconnect(true, true);  // Wipe credentials or it look like it won't work!
    wpsSetup();
    checkWifiConnectionFlag = false;
  }
}

void loop() {
  if (startOfMainLoop == 0)
    startOfMainLoop = millis();  // For averaging stuff warmup phase

  webServerReaction();

  overallAverageHumidity = 0;
  int sensorcount = 0;
  for (int v = 0; v < sensorPortTotalNumber; v++) {
    if (activeSensor[v]) {
      averageHumidity[v] = averageHumidity[v] * .98 + analogRead(sensorPort[v]) * .02;
      overallAverageHumidity += averageHumidity[v];  // TODO: Use percentage, not absolute values!
      sensorcount++;
    }
  }
  overallAverageHumidity /= sensorcount;
  overallAverageHumidity = map(overallAverageHumidity, 673, 264, 0, 100);  // Translation to percent

  unsigned long currentMillis = millis();
  // Pump mode hysteresis for mold prevention - only check for switching down after pump cooldown!
  if (!humidityThresholdHysteresisFalling && overallAverageHumidity > humidityThreshold1 && (lastPumpRunStart == 0 || currentMillis > lastPumpRunStart + pumpDelayInMinutes * 60000)) {
    debug("Average humidity (");
    debug(overallAverageHumidity);
    debug(") is above upper threshold (");
    debug(humidityThreshold1);
    debugln("), switching to falling mode");
    humidityThresholdHysteresisFalling = true;
  } else if (humidityThresholdHysteresisFalling && overallAverageHumidity < humidityThreshold2) {
    debug("Average humidity (");
    debug(overallAverageHumidity);
    debug(") is below lower threshold (");
    debug(humidityThreshold2);
    debugln("), switching to rising mode");
    humidityThresholdHysteresisFalling = false;
  }

  currentMillis = millis();
  if (lastOverallAverageHumidity != overallAverageHumidity && currentMillis - startOfMainLoop > 5000) {  // Only after average warmup
    doStatusReporting();
    lastOverallAverageHumidity = overallAverageHumidity;
  }

  currentMillis = millis();
  if (currentMillis - lastMillis60s > 60000) {
    if (checkWifiConnectionFlag) {
      doCheckWiFiConnection();
      debugln(getAvgValues());
    }
    lastMillis60s += 60000;
  }

  currentMillis = millis();
  if (startPumpMillis == 0 && !humidityThresholdHysteresisFalling && overallAverageHumidity < humidityThreshold1 && currentMillis - startOfMainLoop > 5000 && (lastPumpRunStart == 0 || currentMillis > lastPumpRunStart + pumpDelayInMinutes * 60000)) {  // Only after average warmup
    debug("Average humidity (");
    debug(overallAverageHumidity);
    debug(") is lower than upper threshold (");
    debug(humidityThreshold1);
    debugln("), starting pump");
    startPump();
  }

  if (lastPumpCurrentValue > 0)     // DEBUG
    debugln(lastPumpCurrentValue);  // DEBUG

  currentMillis = millis();
  if (startPumpMillis != 0 && checkAverage && currentMillis - startPumpMillis > pumpRuntimeInSeconds * 700) {  // Check at 0.7 of pump runtime
    debug("Average current: ");
    debugln(lastPumpCurrentValue);
    if (lastPumpCurrentValue < dryWetPumpBorderValue) {
      debug("Average current (");
      debug(lastPumpCurrentValue);
      debug(") is lower than threshold (");
      debug(dryWetPumpBorderValue);
      debugln("), reporting water is empty");
      doEmptyWaterWarning();
    }
    checkAverage = false;
  }

  currentMillis = millis();
  if (startPumpMillis != 0 && (currentMillis - startPumpMillis > pumpRuntimeInSeconds * 1000 || currentMillis < startPumpMillis)) {  // Force switch off pump in case of timer overflow (every ~52 days)
    stopPump();
  }

  for (int t = 0; t < 30; t++)
    lastPumpCurrentValue = lastPumpCurrentValue * .99 + (analogRead(PUMP_CURRENT_GPIO_NUMBER) * PUMP_CURRENT_MULTIPLIER) * .01;

  delay(100);
}

void doEmptyWaterWarning() {
  if (WiFi.status() == WL_CONNECTED) {
    debug("Request: ");
    debugln(emptyWaterURL);
    HTTPClient http;
    http.begin(String(emptyWaterURL));
    int httpResponseCode = http.GET();
    debug("Response: ");
    debugln(httpResponseCode);
    if (httpResponseCode != 200)
      debugln(http.getString());
    http.end();
  } else {
    debugln("WiFi Disconnected");
  }
}

void doStatusReporting() {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(String(reportURL) + "?oah=" + overallAverageHumidity);
    int httpResponseCode = http.GET();
    if (httpResponseCode != 200) {
      debug("Error while reporting: ");
      debugln(http.getString());
    }
    http.end();
  } else {
    debugln("WiFi Disconnected");
  }
}

String getAvgValues() {
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
  debugln("Starting Pump");
  digitalWrite(PUMP_ACTIVE_GPIO_NUMBER, LOW);
  startPumpMillis = millis();
  lastPumpRunStart = startPumpMillis;
  checkAverage = true;
}

void stopPump() {
  debugln("Stopping Pump");
  digitalWrite(PUMP_ACTIVE_GPIO_NUMBER, HIGH);
  startPumpMillis = 0;
}

bool connectToWiFi() {
  debug("Connecting to SSID: ");
  debugln(ssid);

  WiFi.begin(ssid, pwd);

  for (int t = 0; t < 20; t++) {  // Try for 20 seconds
    if (WiFi.status() == WL_CONNECTED)
      break;
    delay(1000);
    debug(".");
  }
  debugln();
  if (WiFi.status() == WL_CONNECTED) {
    digitalWrite(LED_BUILTIN, HIGH);
    debug("Success: ");
    debugln(WiFi.localIP().toString());
    server.begin();
    delay(50);
    digitalWrite(LED_BUILTIN, LOW);
    return true;
  } else {
    return false;
  }
}

void doCheckWiFiConnection() {
  if (WiFi.status() != WL_CONNECTED) {
    debugln("Connection lost, reconnecting...");
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
    debugln("WPS Enable Failed");
  } else if (esp_wifi_wps_start(0)) {
    debugln("WPS Start Failed");
  }
}

void wpsStop() {
  if (esp_wifi_wps_disable()) {
    debugln("WPS Disable Failed");
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
  switch (event) {
    case ARDUINO_EVENT_WIFI_STA_START:
      debugln("Station Mode Started");
      break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      debugln("Connecting to SSID: " + WiFi.SSID());
      //debugln("Password: " + WiFi.psk()); // DEBUG
      WiFi.SSID().toCharArray(ssid, WiFi.SSID().length() + 1);  // +1 for \0 terminator
      WiFi.psk().toCharArray(pwd, WiFi.psk().length() + 1);     // +1 for \0 terminator
      prefs.putBytes("ssid", (byte*)(&ssid), sizeof(ssid));
      prefs.putBytes("password", (byte*)(&pwd), sizeof(pwd));
      debug("Success: ");
      debugln(WiFi.localIP().toString());
      digitalWrite(LED_BUILTIN, LOW);
      checkWifiConnectionFlag = true;
      server.begin();
      break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      debugln("Disconnected from station, attempting reconnection");
      WiFi.reconnect();
      break;
    case ARDUINO_EVENT_WPS_ER_SUCCESS:
      debugln("WPS Successful, stopping WPS and connecting to: " + String(WiFi.SSID()));
      wpsStop();
      delay(10);
      WiFi.begin();
      break;
    case ARDUINO_EVENT_WPS_ER_FAILED:
      debugln("WPS Failed, retrying");
      wpsStop();
      wpsStart();
      break;
    case ARDUINO_EVENT_WPS_ER_TIMEOUT:
      debugln("WPS Timedout, retrying");
      wpsStop();
      wpsStart();
      break;
    case ARDUINO_EVENT_WPS_ER_PIN:
      debugln("WPS_PIN = " + wpspin2string(info.wps_er_pin.pin_code));
      break;
    default:
      break;
  }
}

void webServerReaction() {
  WiFiClient client = server.accept();
  if (client) {
    String currentLine = "";
    while (client.connected()) {
      if (client.available()) {
        char c = client.read();
        if (c == '\n') {
          //debugln(currentLine); // DEBUG
          if (currentLine.startsWith("GET /H")) {
            debugln(client.remoteIP().toString() + " -> Humidity value block  : " + currentLine);
            page = PAGE_HUMIDITYVAL;
          } else if (currentLine.startsWith("GET /O")) {
            debugln(client.remoteIP().toString() + " -> Overall Humidity block: " + currentLine);
            page = PAGE_HUMIDITY;
          } else if (currentLine.startsWith("GET /P")) {
            debugln(client.remoteIP().toString() + " -> Pump current block    : " + currentLine);
            page = PAGE_PUMPVAL;
          } else if (currentLine.startsWith("GET /T")) {
            debugln(client.remoteIP().toString() + " -> Pump test activate    : " + currentLine);
            startPump();
          } else if (currentLine.startsWith("GET /E")) {
            debugln(client.remoteIP().toString() + " -> Empty test activate   : " + currentLine);
            doEmptyWaterWarning();
          } else if (currentLine.startsWith("GET /R")) {
            debugln(client.remoteIP().toString() + " -> Report test activate  : " + currentLine);
            doStatusReporting();
          } else if (currentLine.startsWith("GET /JQ.js")) {
            debugln(client.remoteIP().toString() + " -> JQuery file           : " + currentLine);
            page = PAGE_JQUERY;
          } else if (currentLine.startsWith("GET /Da")) {
            debugln(client.remoteIP().toString() + " -> Debug main page       : " + currentLine);
            page = PAGE_DEBUG;
          } else if (currentLine.startsWith("GET /Db")) {
            //debugln(client.remoteIP().toString() + " -> Debug body (part)     : " + currentLine); // DEBUG
            page = PAGE_DEBUG_BODY_PART;
          } else if (currentLine.startsWith("GET /Dc")) {
            debugln(client.remoteIP().toString() + " -> Debug body (full)     : " + currentLine);
            page = PAGE_DEBUG_BODY_FULL;
          } else if (currentLine.startsWith("GET /")) {
            debugln(client.remoteIP().toString() + " -> Default page          : " + currentLine);
            if (currentLine.indexOf("?") != -1) {
              for (int v = 0; v < sensorPortTotalNumber; v++) {
                char portname[7];  // gpioXX\0
                (String("gpio") + String(sensorPort[v])).toCharArray(portname, sizeof(portname));
                activeSensor[v] = currentLine.indexOf(portname) != -1;
                prefs.putBool(portname, activeSensor[v]);
              }
            }
            if (currentLine.indexOf("threshold1") != -1) {
              humidityThreshold1 = currentLine.substring(currentLine.indexOf("threshold1") + String("threshold1").length() + 1).toInt();
              humidityThreshold1 = humidityThreshold1 < 1 ? 1 : (humidityThreshold1 > 99 ? 99 : humidityThreshold1);
              prefs.putInt("threshold1", humidityThreshold1);
            }
            if (currentLine.indexOf("threshold2") != -1) {
              humidityThreshold2 = currentLine.substring(currentLine.indexOf("threshold2") + String("threshold2").length() + 1).toInt();
              humidityThreshold2 = humidityThreshold2 < 1 ? 1 : (humidityThreshold2 > 99 ? 99 : humidityThreshold2);
              prefs.putInt("threshold2", humidityThreshold2);
            }
            if (currentLine.indexOf("pumptime") != -1) {
              pumpRuntimeInSeconds = currentLine.substring(currentLine.indexOf("pumptime") + String("pumptime").length() + 1).toDouble();
              pumpRuntimeInSeconds = pumpRuntimeInSeconds < 1 ? 1 : (pumpRuntimeInSeconds > 300 ? 300 : pumpRuntimeInSeconds);
              prefs.putDouble("pumptime", pumpRuntimeInSeconds);
            }
            if (currentLine.indexOf("pumpdelay") != -1) {
              pumpDelayInMinutes = currentLine.substring(currentLine.indexOf("pumpdelay") + String("pumpdelay").length() + 1).toInt();
              pumpDelayInMinutes = pumpDelayInMinutes < 1 ? 1 : (pumpDelayInMinutes > 1440 ? 1440 : pumpDelayInMinutes);
              prefs.putInt("pumpdelay", pumpDelayInMinutes);
            }
            if (currentLine.indexOf("drypump") != -1) {
              dryWetPumpBorderValue = currentLine.substring(currentLine.indexOf("drypump") + String("drypump").length() + 1).toInt();
              dryWetPumpBorderValue = dryWetPumpBorderValue < 1 ? 1 : (dryWetPumpBorderValue > 1022 ? 1022 : dryWetPumpBorderValue);
              prefs.putInt("drypump", dryWetPumpBorderValue);
            }
            if (currentLine.indexOf("emptyUrl") != -1) {
              const int start = currentLine.indexOf("emptyUrl") + String("emptyUrl").length() + 1;
              int a = currentLine.indexOf("&", start);
              int b = currentLine.indexOf(" ", start);
              a = a == -1 ? 99999 : a;
              b = b == -1 ? 99999 : b;
              URLCode url;
              url.urlcode = currentLine.substring(start, min(a, b));
              debugln(url.urlcode);
              url.urldecode();
              debugln(url.strcode);
              url.strcode.toCharArray(emptyWaterURL, sizeof(emptyWaterURL));
              prefs.putBytes("emptyUrl", (byte*)(&emptyWaterURL), sizeof(emptyWaterURL));
            }
            if (currentLine.indexOf("reportURL") != -1) {
              const int start = currentLine.indexOf("reportURL") + String("reportURL").length() + 1;
              int a = currentLine.indexOf("&", start);
              int b = currentLine.indexOf(" ", start);
              a = a == -1 ? 99999 : a;
              b = b == -1 ? 99999 : b;
              URLCode url;
              url.urlcode = currentLine.substring(start, min(a, b));
              debugln(url.urlcode);
              url.urldecode();
              debugln(url.strcode);
              url.strcode.toCharArray(reportURL, sizeof(reportURL));
              prefs.putBytes("reportURL", (byte*)(&reportURL), sizeof(reportURL));
            }
          }
          if (currentLine.length() == 0) {
            client.println("HTTP/1.1 200 OK");
            if (page == PAGE_HUMIDITYVAL) {
              client.println("Content-type:application/json;charset=utf-8");
              client.println();
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
              client.println("<html><head><script src=\"JQ.js\"></script><script>function go(){$(\"#hd\").load(\"/H\");$(\"#ho\").load(\"/O\");$(\"#pc\").load(\"/P\");setTimeout(go,1000)}$(document).ready(go)</script></head><body>");
              client.println("<form action=\"/\"><table>");
              client.print("<tr><td>Nutze Sensor am GPIO:</td><td>");
              for (int v = 0; v < sensorPortTotalNumber; v++) {
                client.print(sensorPort[v]);
                client.print("<input type=\"checkbox\" name=\"gpio");
                client.print(sensorPort[v]);
                client.print("\" value=\"1\"");
                if (activeSensor[v])
                  client.print(" checked");
                client.print("> ");
              }
              client.println("</td><td><span id=\"hd\">[]</span></td></tr>");
              client.print("<tr title=\"Bei überschreiten dieser prozentualen Feuchte wird das zyklische Pumpen zur Schimmelvermeidung deaktiviert.\"><td>Feuchteschwellwert 1:</td><td><input name=\"threshold1\" size=\"10\" value=\"");
              client.print(humidityThreshold1);
              client.println("\">&nbsp;(<span id=\"ho\">[]</span>%)</td></tr>");
              client.print("<tr title=\"Bei unterschreiten dieser prozentualen Feuchte wird das zyklische Pumpen wieder aktiviert.\"><td>Feuchteschwellwert 2:</td><td><input name=\"threshold2\" size=\"10\" value=\"");
              client.print(humidityThreshold2);
              client.println("\"></td></tr>");
              client.print("<tr title=\"Pumpenlaufzeit in Sekunden bei Erreichen des Feuchteschwellwerts oder beim Pumpentest.\"><td>Pumpenlaufzeit:</td><td><input name=\"pumptime\" size=\"10\" value=\"");
              client.print(pumpRuntimeInSeconds);
              client.println("\">&nbsp;s<td></td></tr>");
              client.print("<tr title=\"Mindestpause zwischen zwei Pumpstößen in Minuten.\"><td>Pumpenpause:</td><td><input name=\"pumpdelay\" size=\"10\" value=\"");
              client.print(pumpDelayInMinutes);
              client.println("\">&nbsp;m<td></td></tr>");
              client.print("<tr title=\"Der eingetragene Wert muss zwischen dem angezeigten Wert bei laufender Pumpe bei vollem Wassertank und bei leerem Wassertank liegen. "
                           "Tipp: Der Wert für den jeweiligen Durchgang wird im Debug Log ausgegeben.\"><td>Wasserstandserkennung:</td><td><input name=\"drypump\" size=\"10\" value=\"");
              client.print(dryWetPumpBorderValue);
              client.println("\">&nbsp;(<span id=\"pc\">0</span>mA)</td><td><a href=\"/T\">Test</a></td></tr>");
              client.print("<tr title=\"Diese URL wird aufgerufen, wenn erkannt wird, dass die Pumpe trocken läuft, also der Wasservorrat erschöpft ist. Sie muss mit http:// oder https:// "
                           "beginnen und vom Sensor aus erreichbar sein.\"><td>Fehlermeldung an:</td><td><input name=\"emptyUrl\" maxlength=\"255\" size=\"60\" value=\"");
              client.print(emptyWaterURL);
              client.println("\">&nbsp;</td><td><a href=\"/E\">Fehlertest</a></td></tr>");
              client.print("<tr title=\"Diese URL wird zyklisch mit den aktuellen Feuchtewerten aufgerufen. Sie muss mit http:// oder https:// beginnen und vom Sensor aus erreichbar sein.\">"
                           "<td>Statusreports an:</td><td><input name=\"reportURL\" maxlength=\"255\" size=\"60\" value=\"");
              client.print(reportURL);
              client.println("\">&nbsp;</td><td><a href=\"/R\">Test</a></td></tr>");
              client.println("<tr><td colspan=\"2\" align=\"center\"><input type=\"submit\" value=\"speichern\"></td><td><a href=\"/Da\">Debug</a></td></tr>");
              client.println("</table></form>");
              client.println("<a href=\"/H\">Feuchtewerte</a>");
              client.println("<a href=\"/O\">Durchschnittsfeuchte</a>");
              client.println("<a href=\"/P\">Pumpenwerte</a>");
              client.println("<a href=\"/Dc\">DebugBodyFull</a>");
              client.println("<a href=\"/Db\">DebugBodyPart</a>");
              client.println("<a href=\"/JQ.js\">JQuery</a>");
              client.println("</body></html>");
            }
            page = PAGE_DEFAULT;
            break;
          } else {
            currentLine = "";
          }
        } else if (c != '\r') {
          currentLine += c;
        }
      }
    }
    client.stop();
    //debugln("Client Disconnected.");
  }
}

void wpsSetup() {
  WiFi.onEvent(WiFiEvent);  // Will call WiFiEvent() from another thread.
  WiFi.mode(WIFI_MODE_STA);
  debugln("Starting WPS");
  wpsInitConfig();
  delay(1000);
  wpsStart();
}
