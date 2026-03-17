# Plantcare

<!-- DE -->
## Beschreibung (DE)

Plantcare ist ein automatisches Pflanzenbewässerungssystem auf Basis des ESP32.
Es misst die Bodenfeuchte über bis zu drei kapazitive Sensoren, steuert eine Pumpe
per Relais und kann optional Statusinformationen an selbst gehostete HTTP-Endpunkte
senden.  Die gesamte Konfiguration erfolgt bequem über den integrierten Webserver im
Browser.

<!-- EN -->
## Description (EN)

Plantcare is an automatic plant watering system built around the ESP32.
It reads up to three capacitive soil-moisture sensors, drives a pump via a relay using
a two-threshold hysteresis loop, and optionally reports telemetry to user-supplied HTTP
endpoints.  A built-in web server provides full browser-based configuration and a live
debug log page.

---

<center><img src="https://raw.githubusercontent.com/Joghurt/Plantcare/refs/heads/main/screenshot.png"></center>

---

## Features / Funktionen

* Supports 1–3 capacitive humidity sensors; uses their average value.
* Hysteresis pump control: upper threshold stops cyclic pumping, lower threshold restarts it.
* Push-button WPS fallback — activated automatically 40 s after a failed WiFi connection on cold start.
* HTTP callback when the water tank is empty (configurable URL).
* HTTP telemetry of current humidity values (configurable URL).
* In-browser configuration and debug log viewer.

---

## Build / Toolchain

| Item | Value |
|---|---|
| Language | C++ (Arduino dialect, ESP32 Arduino core) |
| Board package | **esp32** by Espressif Systems — install via Arduino IDE Board Manager |
| Required libraries | **URLCode** by XieXuan; **incbin** by AlexIII (install via Arduino IDE Library Manager) |
| Runtime dependency | `jquery-3.7.1.min.js` — download separately (see below) |
| Tested hardware | WEMOS D1 MINI ESP32 |

---

## Hardware

Required:
* ESP32 board (e.g. WEMOS D1 MINI ESP32)
* Capacitive soil-moisture sensor + relay module + small water pump
* 3.9 Ω resistor, 47 µF capacitor
* USB-C power supply with sufficient current headroom (brownout = pump runs longer!)

See the Fritzing diagram for wiring details.  Fritzing part sources are listed in the
file header of `Plantcare.ino`.

---

## Run / Flash / Deploy

### ESP32 firmware

1. Clone or download the repository.
2. Download `jquery-3.7.1.min.js` from https://code.jquery.com/jquery-3.7.1.min.js
   and place it in the **same directory** as `Plantcare.ino`.
   (Failing to do this causes an "Error: file not found: jquery-3.7.1.min.js" at compile time.)
3. Open `Plantcare.ino` by **double-clicking** it (opens the full Arduino project folder).
4. Install the board package and libraries listed above.
5. Select the correct board and COM port; click **Upload**.

### PHP backend (optional)

Deploy `index.php`, `status.php`, `empty.php`, and a `db.php` (not included — provide
your own with a `$dbcon` MySQLi connection, a `db_query()` wrapper, and a `sendMail()`
helper) to a PHP-capable web server with a MySQL/MariaDB database.

Expected DB schema (minimum):

```sql
CREATE TABLE Humidity (Id INT AUTO_INCREMENT PRIMARY KEY, Date DATETIME DEFAULT NOW(), Average FLOAT);
CREATE TABLE Config   (Id VARCHAR(20) PRIMARY KEY, Value VARCHAR(100));
INSERT INTO Config VALUES ('Threshold1', '70'), ('Threshold2', '50');
```

---

## Configuration

### WiFi credentials

Edit `ssidAndPassword.h` with your SSID and password **or** use WPS push-button mode
(the ESP switches to WPS automatically 40 s after a failed connection attempt).
Credentials entered via WPS or the web UI are persisted in ESP32 NVS.

> **Security note:** Do not commit real credentials to a public repository.
> The file `ssidAndPassword.h` is intentionally kept as a plaintext fallback for
> environments without WPS.  Consider adding it to `.gitignore`.

### Runtime settings (web UI)

Navigate to the ESP32's IP address in a browser.  All settings are stored in NVS and
survive power cycles.

| Parameter | Description | Default |
|---|---|---|
| Sensor GPIOs | Enable/disable sensors; calibrate dry/wet ADC values | GPIO 34 active; dry=670, wet=260 |
| Upper hysteresis (%) | Pumping deactivated above this humidity | 70 % |
| Lower hysteresis (%) | Pumping activated below this humidity | 50 % |
| First pump runtime (s) | Duration of first pump activation per cycle | 7 s |
| First pump delay (min) | Delay before second activation per cycle | 360 min |
| Other pump runtimes (s) | Duration of subsequent activations | 0.8 s |
| Other pump delays (min) | Minimum delay between subsequent activations | 180 min |
| Water detection (mA) | Pump current threshold for empty-tank detection | 250 mA |
| Empty-warning URL | Called via HTTP GET when tank is empty | — |
| Status-report URL | Called via HTTP GET with current humidity | — |
| Debug level | Error / Warn / Info / Verbose / Debug / Trace | Info |
| Serial debug | Mirror debug log to UART @ 115200 baud | off |

---

## Architecture / Modules

```
Plantcare.ino
├── setup()              — One-time init: ADC, GPIO, NVS, WiFi, NTP
├── loop()               — Main control loop (~20 ms cycle when pump idle)
│   ├── webServerReaction()   — Blocking HTTP request handler (one client/iteration)
│   ├── Sensor averaging      — Exponential moving average (α=0.02)
│   ├── Hysteresis logic      — Two-threshold pump mode control
│   ├── doStatusReporting()   — HTTP GET telemetry (on ≥0.5 % change)
│   ├── doCheckWiFiConnection()— WiFi reconnect (every 60 s)
│   └── Pump control          — startPump / stopPump / current measurement
├── WiFiEvent()          — FreeRTOS task: WPS + WiFi event handler [!shared state]
└── wpsSetup()           — Initiates WPS push-button mode
```

**Concurrency:**  
`WiFiEvent()` runs in a separate FreeRTOS task.  It writes to `ssid`, `pwd`,
`checkWifiConnectionFlag`, and the debug ring buffer without a mutex.
`checkWifiConnectionFlag` is declared `volatile`; full mutex protection of the String
globals is a known limitation (see CHANGELOG).

**Pump GPIO:**  GPIO 33 — LOW-active relay (HIGH = off, LOW = on).  
**Sensor GPIOs:** 34, 35, 39 (ADC1, 10-bit resolution, 0–1023).  Max count defined by `constexpr MAX_SENSORS`.  
**Current sense:** GPIO 36 — ADC reading scaled by `PUMP_CURRENT_MULTIPLIER` (2.8).  
**HTTP:** Responses include `Connection: close`; keep-alive is not supported.

---

## Interfaces

| Interface | Direction | Description |
|---|---|---|
| WiFi HTTP server :80 | inbound | Config UI, AJAX endpoints, static assets |
| `emptyWaterURL` (HTTP GET) | outbound | Empty-tank notification |
| `reportURL?oah=&t1=&t2=` (HTTP GET) | outbound | Humidity telemetry |
| NVS (ESP32 Preferences) | local | Persistent settings, namespace `"p"` |
| UART 115200 (optional) | outbound | Serial debug mirror |

### HTTP Endpoints (ESP32 built-in server)

| Path | Method | Returns |
|---|---|---|
| `/` | GET | HTML config page |
| `/` | POST | Saves config, redirects |
| `/H?<n>` | GET | JSON: raw ADC value for sensor n |
| `/H` | GET | JSON: array of all raw ADC values |
| `/O` | GET | JSON: overall average humidity (%) |
| `/M` | GET | Text: "Rising" or "Falling" |
| `/P` | GET | JSON: last pump current (mA) |
| `/jq.js` | GET | jQuery 3.7.1 (cached 10 h) |
| `/s.css /d.css /n.css` | GET | Stylesheets (cached 10 h) |
| `/Da /Db /Dc` | GET | Debug log pages |
| `/T` | GET | Trigger pump test |
| `/E` | GET | Trigger empty-water warning |
| `/R` | GET | Trigger status report |

---

## Tests / Static Analysis

No automated test suite exists for this project (embedded hardware required for
meaningful integration tests).

For static analysis on the C++ code, `cppcheck` can be run against the `.ino` file
treated as C++14:

```sh
cppcheck --std=c++14 --enable=all --language=c++ Plantcare.ino
```

The PHP files can be checked with:

```sh
php -l status.php
php -l index.php
php -l empty.php
```

---

## File Structure

```
Plantcare/
├── Plantcare.ino          # ESP32 firmware (main sketch)
├── ssidAndPassword.h      # WiFi fallback credentials (do not commit real passwords)
├── jquery-3.7.1.min.js    # jQuery (download separately, not in repo)
├── s.css                  # Base stylesheet (served by ESP32)
├── d.css                  # Day theme stylesheet
├── n.css                  # Night theme stylesheet
├── index.php              # Optional: humidity chart (Chart.js, PHP backend)
├── status.php             # Optional: telemetry receiver endpoint
├── empty.php              # Optional: empty-tank notification endpoint
├── CHANGELOG.md           # Change history
├── README.md              # This file
└── LICENSE
```

---

## Limitations / Known Issues

* **BUG-06 (data race):** `WiFiEvent` runs in a FreeRTOS task and writes to shared
  `String` globals without a mutex.  Occasional garbled debug output or spurious
  reconnects are theoretically possible.  Full fix would require FreeRTOS semaphores.
* **POST parsing fragility:** Form fields are parsed with `indexOf()`/`substring()`.
  If a field name is absent, `indexOf()` returns -1 and `substring()` silently reads
  from position 0, potentially writing incorrect values to NVS.  Checkbox fields
  (`gpio*`, `serialDebug`) are safe (they use the `indexOf != -1` pattern).
* **No HTTPS on ESP32 web server:** The built-in HTTP server serves plain HTTP.
  Use only on a trusted local network.
* **No authentication** on the web UI: anyone on the same network can change settings
  or trigger the pump.
* **Single-client HTTP server:** `webServerReaction()` handles one request per
  `loop()` iteration; concurrent browser tabs may stall briefly.
* Browsing to `d.css` or `n.css` directly switches the active stylesheet as a side
  effect (intentional Easter egg).

---

## Changelog / Lizenz

See [CHANGELOG.md](CHANGELOG.md) for a detailed history of changes.  
Licensed under the terms in [LICENSE](LICENSE).
