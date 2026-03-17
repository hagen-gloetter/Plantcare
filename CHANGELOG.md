# Changelog

## [Unreleased]

## [Session 3] — 2026-03-17

### Fixed

---

#### BUG-09 — `abs()` statt `fabs()` bei `double`-Vergleich (Schwere: **MITTEL**)

**Was war das Problem?**  
Im Status-Reporting-Trigger in `loop()` wurde `abs()` auf einen `double`-Wert
angewendet:

```cpp
if (abs(lastOverallAverageHumidity - overallAverageHumidity) >= .5 ...
```

In Standard-C++ ist `abs()` die Integer-Überladung (`<cstdlib>`).  Ein `double`-
Argument wird zu `int` trunkiert — `0.7` wird zu `0`, `0.3` wird zu `0`.  Damit
feuert der Vergleich `>= .5` erst bei einer Differenz ≥ 1.0 statt ≥ 0.5, was
Telemetrie-Reports verschluckt.  Arduinos `abs()`-Makro umgeht das Problem auf
AVR, aber auf ESP32 kann der Compiler die C++-Überladung verwenden.

**Wie wurde es behoben?**  
`abs()` durch `fabs()` ersetzt und `#include <cmath>` hinzugefügt.  `fabs()` ist
die explizite `double`-Variante und verhält sich auf allen Plattformen korrekt.

---

#### BUG-10 — `isnan()` erkannte fehlende Pump-Laufzeiten auf neuen Geräten nicht (Schwere: **MITTEL**)

**Was war das Problem?**  
Die Default-Werte für `pumpRuntime1InSeconds` und `pumpRuntimeNInSeconds` wurden
nur geschrieben, wenn `isnan()` wahr war:

```cpp
if (isnan(pumpRuntime1InSeconds)) {
    pumpRuntime1InSeconds = 7.0;
```

`prefs.getDouble()` gibt bei fehlendem NVS-Key aber `0.0` zurück — niemals NaN.
Bei einem fabrikneuen Gerät blieb die Pumplaufzeit daher bei 0 Sekunden.  Die Pumpe
wurde sofort gestartet *und* gestoppt, was einen endlosen Start/Stop-Zyklus
verursachte und unnötig NVS-Schreibzyklen verbrauchte.

**Wie wurde es behoben?**  
Beide Prüfungen erweitert um `|| pumpRuntime1InSeconds <= 0.0`:

```cpp
if (isnan(pumpRuntime1InSeconds) || pumpRuntime1InSeconds <= 0.0) {
```

---

#### BUG-11 — `contentLength` und `lineLength` uninitialisiert (Schwere: **MITTEL**)

**Was war das Problem?**  
In `webServerReaction()` wurden die lokalen Variablen `contentLength` und
`lineLength` ohne Initialwert deklariert:

```cpp
int contentLength, lineLength;
```

In C++ haben uninitalisierte lokale Variablen unbestimmte Werte.  Die Bedingung
`contentLength == lineLength` konnte dadurch zufällig wahr werden und den
POST-Body-Parser auslösen, obwohl kein POST-Request vorlag.  Je nach Stack-Inhalt
führte das zu sporadisch falschem Verhalten (falsche Settings in NVS geschrieben).

**Wie wurde es behoben?**  
Beide Variablen werden jetzt auf `0` initialisiert:

```cpp
int contentLength = 0, lineLength = 0;
```

---

#### BUG-12 — `lastMillis60s` nicht initialisiert — sofortiger WiFi-Check bei Erststart (Schwere: **NIEDRIG**)

**Was war das Problem?**  
`lastMillis60s` wurde als globale Variable automatisch mit `0` initialisiert.
Beim ersten `loop()`-Durchlauf war `millis() - 0 > 60000` sofort wahr (da `millis()`
nach `setup()` bereits > 40 s ist — WiFi-Verbindung wartet 20+20 s).
Dadurch wurde `doCheckWiFiConnection()` in der allerersten Iteration aufgerufen,
obwohl die Verbindung gerade erst aufgebaut wurde.

**Wie wurde es behoben?**  
`lastMillis60s` wird jetzt beim ersten `loop()`-Eintritt auf `startOfMainLoopMillis`
gesetzt, so dass der erste WiFi-Check erst 60 s nach Loop-Start stattfindet:

```cpp
if (startOfMainLoopMillis == 0) {
    startOfMainLoopMillis = millis();
    lastMillis60s = startOfMainLoopMillis;
}
```

---

#### BUG-13 — Hysterese-Schwellwerte vertauschbar über Web-UI (Schwere: **NIEDRIG**)

**Was war das Problem?**  
Wenn ein Benutzer über das Webinterface den oberen Schwellwert niedriger als den
unteren eingibt (z. B. Upper=40%, Lower=60%), wurden die Werte so in NVS
gespeichert.  Die Hysterese-Logik konnte dann nie den Zustand wechseln — die Pumpe
blieb entweder dauerhaft an oder dauerhaft aus, je nach initialem Zustand.

**Wie wurde es behoben?**  
Im POST-Handler werden die Werte jetzt nach dem Clamping automatisch getauscht,
wenn Upper < Lower:

```cpp
if (humidityThresholdUpper < humidityThresholdLower) {
    int tmp = humidityThresholdUpper;
    humidityThresholdUpper = humidityThresholdLower;
    humidityThresholdLower = tmp;
}
```

---

### Improved

- **`sprintf()` → `snprintf()`** — In `debugln()` wurde `sprintf(buffer, ...)` durch
  `snprintf(buffer, sizeof(buffer), ...)` ersetzt.  Schützt gegen Buffer-Overrun
  wenn die Uptime-Tage mehr als 8 Stellen benötigen (theoretisch nach ~27.000 Jahren,
  aber gute C-Praxis).

- **`constexpr int MAX_SENSORS`** — Die 6-fach wiederholte `sizeof(sensorPort) /
  sizeof(int)`-Expression wurde durch eine einzelne `constexpr`-Konstante ersetzt.
  Reduziert Fehlerquellen und verbessert Lesbarkeit.

- **`(unsigned long) * 60000UL`** — Die Multiplikation `pumpDelayInMinutes * 60000`
  wurde explizit auf `unsigned long` gecastet.  Bei `int`-Zwischenwerten und
  Delay-Werten nahe 1440 kann die Multiplikation den 32-Bit `int`-Bereich berühren.

- **`Connection: close` HTTP-Header** — Der ESP32-Webserver unterstützt kein
  HTTP Keep-Alive korrekt.  Der Header weist den Browser an, die TCP-Verbindung
  nach jeder Response zu schließen, was hängende Sockets verhindert.

- **`#include <cmath>`** — Hinzugefügt für portables `fabs()`.

### Documentation

- Thread-Safety-Warnung als Kommentar über `debug()`/`debugln()` (BUG-06 Querverweis).
- Div-by-Zero-Hinweis im Docblock von `mapf()` (wenn dry == wet Kalibrierung).
- 7 benannte Phasen-Kommentare in `setup()` (GPIO → NVS → Sensoren → Settings →
  Defaults → WiFi → Hysterese).
- Abschnittskommentare für alle Blöcke in `loop()` (Sensor-EMA, Hysterese,
  Status-Reporting, WiFi-Watchdog, Pump-Start, Pump-Stop, Strom-Sampling, Delay).
- `webServerReaction()`-Docblock: POST State Machine Diagramm + `@warning`
  für stalled-client und indexOf(-1) Risiken.

---

## [Session 2] — 2026-03-17

### Fixed

- **BUG-10** — `isnan()` guard on pump runtimes (see Session 3 for full details).
- **BUG-11** — Uninitialized `contentLength`/`lineLength` (see Session 3).

### Documentation

- Phase labels in `setup()`, section comments in `loop()`.
- Improved docblocks for `mapf()`, `debug()`/`debugln()`, `webServerReaction()`.

--- [Session 1] — 2026-03-09

### Fixed

---

#### BUG-01 — SQL-Injection in `status.php` (Schwere: **KRITISCH**)

**Was war das Problem?**  
Der ESP32 ruft `status.php` mit GET-Parametern auf, um Feuchtewerte in die Datenbank
zu schreiben (`oah`) und die Schwellwerte zu aktualisieren (`t1`, `t2`).  
Der ursprüngliche Code baute die SQL-Queries per String-Konkatenation zusammen:

```php
db_query("INSERT INTO Humidity (Average) VALUES ('".$_REQUEST['oah']."')");
db_query("UPDATE Config SET Value='".$_REQUEST['t1']."' WHERE Id='Threshold1'");
```

Ein Angreifer, der die IP des ESP32 im Netzwerk kennt, hätte über einen manipulierten
URL-Aufruf beliebige SQL-Befehle einschleusen können — z. B. alle Tabellen löschen
(`'; DROP TABLE Humidity; --`) oder Zugangsdaten auslesen.

**Wie wurde es behoben?**  
Alle drei Queries nutzen jetzt MySQLi Prepared Statements.  Der Nutzerwert wird als
typisierter Parameter gebunden und niemals in den SQL-String eingefügt:

```php
$oah = (float)($_REQUEST['oah'] ?? 0);
$stmt = $dbcon->prepare("INSERT INTO Humidity (Average) VALUES (?)");
$stmt->bind_param("d", $oah);
$stmt->execute();
```

Der Typ-Cast auf `(float)` bzw. `(int)` vor dem Binding ist eine zusätzliche
Absicherung, die sicherstellt, dass nur numerische Werte die Datenbank erreichen.

---

#### BUG-02 — Stored XSS in `index.php` (Schwere: **HOCH**)

**Was war das Problem?**  
`index.php` liest Feuchte- und Schwellwert-Einträge aus der Datenbank und gibt sie
direkt als JavaScript-Literale aus:

```php
{t:'<?=$row2['Start']?>', y:<?=$row['Upper']?>},
{t:'<?=$x[0]?>', y:<?=$x[1]?>},
```

Wenn ein Angreifer es schafft, einen manipulierten Datums- oder Textwert in die
Datenbank einzuschleusen (z. B. über BUG-01, bevor dieser behoben war), würde
dieser Wert ungefiltert in die HTML-Seite eingebettet.  Ein Datum wie
`2026-03-09</script><script>alert(1)` würde als JavaScript ausgeführt werden — ein
klassischer Stored-XSS-Angriff, mit dem Cookies gestohlen oder weitere Skripte
nachgeladen werden können.

**Wie wurde es behoben?**  
- Numerische Datenbankwerte (`Upper`, `Lower`, `Average`) werden mit `(float)` gecastet.
  En Zahlen-Cast kann keine HTML-/JS-Sonderzeichen enthalten.
- Datums-Strings werden mit `htmlspecialchars(..., ENT_QUOTES, 'UTF-8')` escapiert,
  bevor sie in den JavaScript-String-Kontext eingebettet werden.  Sonderzeichen wie
  `<`, `>`, `"`, `'` werden so in harmlose HTML-Entities umgewandelt:

```php
{t:'<?=htmlspecialchars((string)$row2['Start'], ENT_QUOTES, 'UTF-8')?>', y:<?=(float)$row['Upper']?>},
```

---

#### BUG-03 — Undefiniertes Verhalten: `String == NULL` in `Plantcare.ino` (Schwere: **MITTEL**)

**Was war das Problem?**  
An vier Stellen wurden Arduino-`String`-Objekte mit `NULL` verglichen:

```cpp
if (ssid != NULL && pwd != NULL) { ... }
if (emptyWaterURL == NULL) { ... }
if (reportURL == NULL)     { ... }
if (stylesheet == NULL)    { ... }
```

`String` ist eine C++-Klasse.  Der `==`-Operator ruft intern `compareTo()` auf, was
wiederum `strcmp()` auf dem internen C-String-Zeiger ausführt.  Wird dabei `NULL`
übergeben, ist das Verhalten laut C++-Standard undefiniert (Null-Pointer-Dereferenz
in `strcmp`) — auf dem ESP32 kann das zu einem CPU-Exception-Absturz führen.
Erschwerend greift `prefs.getString()` sowieso niemals `NULL` zurück, sondern `""`
wenn der NVS-Key nicht existiert — der Vergleich war daher auch semantisch falsch.

**Wie wurde es behoben?**  
Alle vier Vergleiche wurden durch die typsichere Arduino-API `.isEmpty()` ersetzt,
die korrekt prüft ob der String leer ist:

```cpp
if (!ssid.isEmpty() && !pwd.isEmpty()) { ... }
if (emptyWaterURL.isEmpty()) { ... }
```

---

#### BUG-04 — Pointer-Arithmetik statt String-Konkatenation in `Plantcare.ino` (Schwere: **MITTEL**)

**Was war das Problem?**  
In `doEmptyWaterWarning()` und `doStatusReporting()` wurde der HTTP-Statuscode
mit einem String-Literal addiert:

```cpp
debugln(DEBUG_VERBOSE, "Response: " + httpResponseCode);
```

In C++ ist `"Response: "` ein `const char*`-Zeiger (kein `String`-Objekt).
Das `+`-Zeichen führt hier **Zeiger-Arithmetik** aus: es verschiebt den Zeiger
um `httpResponseCode` Bytes weiter in den Speicher.  Bei einem Rückgabecode von
z. B. `200` zeigt der Zeiger damit 200 Zeichen hinter dem Anfang des String-Literals
— also in beliebigen Speicher.  Das Ergebnis ist undefiniertes Verhalten: je nach
Laufzeit wurde Speichermüll ausgegeben, das Programm abgestürzt oder (im Worst Case)
ein Sicherheitsproblem durch das Auslesen fremder Speicherbereiche erzeugt.

**Wie wurde es behoben?**  
Durch explizites Erzeugen eines Arduino-`String`-Objekts aus dem Literal wird der
`+`-Operator korrekt als String-Konkatenation aufgelöst:

```cpp
debugln(DEBUG_VERBOSE, String("Response: ") + httpResponseCode);
```

---

#### BUG-05 — Division durch Null bei Erststart ohne konfigurierten Sensor (Schwere: **MITTEL**)

**Was war das Problem?**  
Beim allerersten Einschalten (leere NVS, noch kein Sensor aktiviert) war
`numberOfActiveSensors == 0`.  Kurz danach wurde trotzdem dividiert:

```cpp
overallAverageHumidity /= numberOfActiveSensors;  // Division durch 0!
```

Dasselbe tritt in `loop()` auf wenn `sensorcount == 0`.  Ganzzahl-Division durch 0
ist auf dem ESP32 eine CPU-Exception (Illegal Instruction), die den Watchdog-Reset
auslöst und den Controller in einer Boot-Schleife gefangen hält.
Das war tatsächlich der Grund, warum der Code bereits danach einen Fallback hat,
der beim Erststart mindestens einen Sensor aktiviert — aber der Fallback kommt
erst *nach* der Division.

**Wie wurde es behoben?**  
Schutzabfragen vor beiden Divisionen; der `overallAverageHumidity`-Wert bleibt
bei `0.0` wenn kein Sensor aktiv ist (was den Pump-Trigger korrekt verhindert):

```cpp
if (numberOfActiveSensors > 0)
    overallAverageHumidity /= numberOfActiveSensors;
```

---

#### BUG-06 — Data Race auf globalen Variablen durch WiFiEvent-FreeRTOS-Task (Schwere: **MITTEL**, teilweise behoben)

**Was war das Problem?**  
`WiFiEvent()` wird vom ESP32-WiFi-Stack in einem eigenen FreeRTOS-Task aufgerufen —
also in einem echten Parallelthread.  Dieser Task schreibt ohne jede Synchronisierung
auf Variablen, die auch der Haupt-Loop liest und schreibt:

- `ssid`, `pwd` (Arduino `String`-Objekte — deren internes Heap-Management ist nicht
  thread-safe)
- `checkWifiConnectionFlag` (`bool`)
- `debugBufferArray`, `debugBufferIndexNextEmpty`, `debugBufferIndexLastShown`

Ein gleichzeitiger schreibender und lesender Zugriff auf dieselbe Variable aus zwei
Threads ohne Memory Barrier ist laut C++-Standard ein **Data Race** und damit
undefiniertes Verhalten.  In der Praxis kann es zu korrupten String-Inhalten,
falschem Reconnect-Verhalten oder einem korrupten Debug-Ringpuffer führen.

**Was wurde getan?**  
`checkWifiConnectionFlag` wurde als `volatile bool` deklariert.  Das verhindert,
dass der Compiler den Wert in einem CPU-Register cached und ihn damit gegenüber
dem anderen Thread unsichtbar macht.  Das ist eine minimale, aber nicht vollständige
Lösung: `volatile` garantiert keine atomare Lese-Schreib-Reihenfolge und schützt
nicht die `String`-Objekte.

**Warum nicht vollständig behoben?**  
Eine vollständige Lösung erfordert FreeRTOS-Semaphoren (`xSemaphoreCreateMutex()`)
oder das Ersetzen von `WiFiEvent` durch eine Queue-basierte Kommunikation.
Beides würde die Firmware-Architektur erheblich verändern und liegt außerhalb des
Scope eines Hardening-Passes ohne Verhaltensänderung.  Der Bug ist in den
Known Limitations dokumentiert.

---

#### BUG-07 — WiFi-Passwort im HTTP-abrufbaren Debug-Puffer (Schwere: **MITTEL**)

**Was war das Problem?**  
Im `ARDUINO_EVENT_WIFI_STA_GOT_IP`-Handler wurde das WiFi-Passwort in den
Debug-Ringpuffer geschrieben:

```cpp
debugln(DEBUG_TRACE, "Password: " + WiFi.psk());
```

Der Debug-Ringpuffer ist über die HTTP-Endpunkte `/Da`, `/Db`, `/Dc` des eingebauten
Webservers für jeden im selben Netzwerk ohne Authentifizierung abrufbar.
Jeder, der die IP des ESP32 kennt, konnte damit das WLAN-Passwort im Klartext
abfragen — besonders kritisch, da es sich um das Heimnetzwerk-Passwort handelt.

**Wie wurde es behoben?**  
Die betreffende Zeile wurde ersatzlos entfernt.  Das Passwort wird an keiner Stelle
mehr geloggt.  Der SSID-Name (nicht sicherheitskritisch) wird weiterhin geloggt.

---

#### BUG-08 — Toter Code: doppelter `GET /M`-Handler in `webServerReaction()` (Schwere: **NIEDRIG**)

**Was war das Problem?**  
In der `else if`-Kette des HTTP-Request-Parsers gab es zwei identische Zweige für
`GET /M`:

```cpp
} else if (currentLine.startsWith("GET /M")) {
    page = PAGE_MODE;          // ← dieser wird immer getroffen
} else if (currentLine.startsWith("GET /O")) {
    page = PAGE_HUMIDITY;
} else if (currentLine.startsWith("GET /M")) {  // ← dieser ist dead code!
    page = PAGE_MODE;
}
```

Da die erste `GET /M`-Prüfung alle passenden Requests abfängt, wird die zweite
niemals ausgeführt.  Toter Code ist zwar kein direktes Sicherheits- oder
Stabilitätsproblem, erhöht aber die Wartungslast und kann bei zukünftigen
Änderungen zu Verwirrung führen (z. B. wenn jemand den ersten Block ändert,
ohne zu wissen, dass noch ein zweiter existiert).

**Wie wurde es behoben?**  
Der zweite, unerreichbare `GET /M`-Block wurde entfernt.

---

### Added

- **IMP-01** — Fehlender HTTP-Timeout in `doEmptyWaterWarning()` und `doStatusReporting()`

  Beide Funktionen führen einen HTTP GET-Request durch, ohne einen Timeout zu setzen.
  Wenn der Ziel-Server nicht antwortet, blockiert `http.GET()` (je nach HTTP-Client-
  Implementierung) theoretisch unbegrenzt lang im Haupt-Loop — genau während der
  Pump-Abschaltelogik läuft.  Das kann dazu führen, dass die Pumpe länger als
  konfiguriert läuft und im schlimmsten Fall den Wassertank leerpumpt.
  `http.setTimeout(5000)` setzt ein explizites 5-Sekunden-Limit für den
  Verbindungsaufbau.

- **IMP-02** — Rückgabewert von `prefs.begin()` nicht geprüft

  `prefs.begin("p")` initialisiert das NVS-Subsystem (Non-Volatile Storage).  Schlägt
  dies fehl (z. B. Flash-Fehler, korrupte Partition), gibt die Funktion `false` zurück.
  Im Originalcode wurde der Rückgabewert ignoriert; alle nachfolgenden `prefs.getInt()`-
  Aufrufe liefern dann `0` zurück, was zu unerwarteten Default-Werten führt.
  Jetzt wird der Fehler erkannt und im Debug-Log gemeldet, damit der Anwender weiß,
  dass er mit Default-Werten arbeitet.

- Doxygen-kompatibler Datei-Header in `Plantcare.ino` (Zweck, Concurrency, Build,
  Ownership).

- Doxygen-Kommentare für alle public Functions: `mapf`, `doEmptyWaterWarning`,
  `doStatusReporting`, `getAvgValues`, `startPump`, `stopPump`, `connectToWiFi`,
  `doCheckWiFiConnection`, `WiFiEvent`, `webServerReaction`, `wpsSetup`.

### Changed

- `checkWifiConnectionFlag` als `volatile bool` deklariert (minimale Absicherung
  gegen Compiler-Optimierung bei FreeRTOS-Task-Zugriff, BUG-06 partial mitigation).

### Known Limitations

- **BUG-06** (dokumentiert, nicht vollständig behoben): `WiFiEvent` läuft in einem
  separaten FreeRTOS-Task und schreibt ohne Mutex auf `ssid`, `pwd`,
  `debugBufferArray` u. a.  Eine vollständige Lösung erfordert FreeRTOS-Semaphoren
  oder ein dediziertes Message-Queue-Pattern und würde die Architektur wesentlich
  verändern.  Für einen Hobbyisten-Betrieb akzeptiert; Vollfix als Future Work.
