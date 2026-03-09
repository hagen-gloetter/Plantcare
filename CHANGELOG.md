# Changelog

## [Unreleased]

## [Session 1] — 2026-03-09

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
