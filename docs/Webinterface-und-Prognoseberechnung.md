# Bedienung und Prognoseberechnung

Diese Dokumentation beschreibt den aktuellen Stand des Sketches. Die `.ino` enthält nur den Arduino-Einstieg, die Programmschnittstelle liegt in `SungrowMonitor.h` und die eigentliche Anwendung in `SungrowMonitor.cpp`.

## 1. Was die Prognose tatsächlich berechnet

Die Software gibt **keine feste Ladeleistung** für die Batterie vor. Sie schätzt, wie viel PV-Energie bis zum geplanten Ladeende noch für die Batterie verfügbar sein dürfte, und hebt daraus den erlaubten **Max-SOC** schrittweise an.

Open-Meteo liefert für jede aktivierte Dachfläche stündliche Werte der Einstrahlung auf die geneigte Modulfläche (`global_tilted_irradiance`, GTI) in W/m². Für jede Prognosestunde wird gerechnet:

```text
PV-Leistung = Summe je Dachfläche aus
              (installierte kWp × GTI / 1000 × PV-Systemwirkungsgrad)

PV-Überschuss = max(0, PV-Leistung − erwartete Hauslast)

Energie für Batterie = PV-Überschuss × 1 Stunde
                       × Batterie-Ladewirkungsgrad
                       × Prognose-Sicherheitsfaktor
```

Da Open-Meteo Stundenwerte liefert, entspricht eine volle Stunde mit einem kW-Mittelwert numerisch derselben Energiemenge in kWh. Berücksichtigt wird der Zeitraum vom Abruf der aktuellen Prognose bis:

```text
Sonnenuntergang − „Fertig vor Sonnenuntergang“
```

Die angezeigte Prognose „PV“ ist damit eine Energiemenge in kWh, nicht die momentan verlangte Ladeleistung. Angebrochene Anfangs- und Endstunden werden zeitanteilig gerechnet. Während einer Stunde wächst der Fahrplan entsprechend dem bereits verstrichenen Stundenanteil kontinuierlich weiter, statt den gesamten Stundenwert sofort vorwegzunehmen.

### Zahlenbeispiel für eine Stunde

Angenommen:

- eine Dachfläche mit 10,0 kWp,
- GTI laut Open-Meteo: 800 W/m²,
- PV-Systemwirkungsgrad: 95 %,
- erwartete Hauslast: 1,0 kW,
- Batterie-Ladewirkungsgrad: 95 %,
- Prognose-Sicherheitsfaktor: 80 %.

Dann ergibt sich:

```text
PV-Leistung          = 10,0 × 800 / 1000 × 0,95 = 7,60 kW
PV-Überschuss        = 7,60 − 1,00              = 6,60 kW
nach Ladeverlusten   = 6,60 × 0,95              = 6,27 kWh
sicher angerechnet   = 6,27 × 0,80              = 5,016 kWh
```

Für diese Stunde plant die Software also mit rund **5,02 kWh nutzbarer Batterieenergie**. Bei fünf identischen Stunden wären es 25,08 kWh. Eine Begrenzung durch die maximale Wechselrichter- oder Batterieladeleistung ist in dieser Prognoserechnung derzeit nicht enthalten.

## 2. Bedeutung der drei Faktoren

### Prognose-Sicherheitsfaktor

Dieser Faktor ist ein absichtlicher Sicherheitsabschlag für Wetter- und Modellfehler. Bei 80 % werden nur 80 % des nach Hausverbrauch und Ladeverlusten errechneten Überschusses als sicher verfügbar betrachtet.

- kleinerer Wert: vorsichtiger; Max-SOC wird tendenziell früher angehoben,
- größerer Wert: vertraut der Prognose stärker,
- 100 %: kein zusätzlicher Sicherheitsabschlag.

Der Faktor soll vor allem wechselnde Bewölkung, ungenaue Wetterdaten und nicht gelernte Verbraucher abfedern. Startempfehlung: **80 %**.

### PV-Systemwirkungsgrad

Dieser Wert reduziert die theoretische PV-Leistung aus kWp und Einstrahlung. Er ist **nicht** der Modulwirkungsgrad aus dem Datenblatt. Die installierten kWp enthalten die Modul-Nennleistung bereits. Gemeint ist ein pauschaler Anlagenfaktor für Abweichungen von Laborbedingungen, insbesondere Modultemperatur, Wechselrichter- und Leitungsverluste, Verschmutzung, Mismatch und kleinere Verschattungen.

- kleinerer Wert: weniger erwartete PV-Energie,
- größerer Wert: höhere erwartete PV-Energie.

Standard- und Startwert: **95 %**. Am besten wird dieser Wert an klaren Tagen so angepasst, dass die vorhergesagte PV-Kurve zur realen Erzeugung passt.

### Batterie-Ladewirkungsgrad

Dieser Wert beschreibt, welcher Anteil des nach Abzug der Hauslast verbleibenden PV-Überschusses tatsächlich als Energie im Akku ankommt. Er berücksichtigt Umwandlungs- und Ladeverluste.

- 95 % bedeutet: Von rechnerisch 10 kWh Überschuss werden 9,5 kWh als speicherbar angenommen,
- kleinerer Wert: weniger erwarteter SOC-Zuwachs und vorsichtigere Freigabe.

Startempfehlung: **95 %**. Dieser Wert sollte nicht benutzt werden, um eine schlechte Wetterprognose auszugleichen; dafür ist der Sicherheitsfaktor gedacht.

Die Faktoren werden multipliziert, nicht addiert. Mit 95 % Ladewirkungsgrad und 80 % Sicherheit werden nach Abzug der Hauslast insgesamt `0,95 × 0,80 = 0,76`, also 76 %, als sicher speicherbar angerechnet. Der PV-Systemwirkungsgrad wird bereits davor auf die theoretische PV-Leistung angewendet.

## 3. Vom Energieplan zum Max-SOC

Beim Abruf einer neuen Prognose merkt sich die Software den aktuellen SOC als Startpunkt. Danach entstehen zwei SOC-Kurven:

1. **Prognoseplan:** Der Weg vom Start-SOC zum eingestellten Max-SOC wird entsprechend dem zeitlichen Anteil der prognostizierten Batterieenergie verteilt. Stunden mit viel erwarteter Energie heben die Grenze stärker an.
2. **Mindest-SOC zum Erreichen des Max-SOC:** Die Grenze wird mindestens so weit geöffnet, dass die noch erwartete Energie rechnerisch genügt, um den eingestellten Max-SOC bis zum Ladeende zu erreichen.

Vereinfacht:

```text
Planfortschritt = bisherige Batterieenergie vor Sicherheitsabschlag
                  / (gesamte Batterieenergie vor Sicherheitsabschlag
                     × Prognose-Sicherheitsfaktor)

Plan-SOC = Start-SOC
           + (Max-SOC − Start-SOC) × begrenzter Planfortschritt

Mindest-SOC zum Erreichen des Max-SOC = Max-SOC
                                        − restliche Batterieenergie / Batteriekapazität × 100
```

Der berechnete Mindest-SOC wird anschließend auf **0 % bis Max-SOC** begrenzt. Ein Wert von 0 % bedeutet, dass die verbleibende prognostizierte Energie selbst bei leerem Akku noch zum Max-SOC reichen würde.

Als Sollwert nimmt die Regelung den höheren der beiden Werte. Zusätzlich gilt:

- niemals unter 50 %,
- niemals über dem eingestellten Max-SOC,
- Fahrplan-Rasterung ausgehend von 50 % auf die konfigurierte SOC-Schrittweite,
- anschließend niemals unter dem aktuellen direkten SOC, ohne diesen Istwert auf die nächste Stufe aufzurunden,
- Beachtung von Mindestabstand und der automatisch ausreichend großen Tagesgrenze der Schreibzugriffe.

Damit folgt die Freigabe ausschließlich der Prognosezeit: Bei 3 % Schrittweite entstehen die Grenzen 50, 53, 56 … 100 %. Erreicht der Akku beispielsweise die freigegebenen 56 %, bleibt diese Grenze bestehen und die Ladung pausiert. Erst wenn der zeitliche Fahrplan die nächste Stufe erreicht, werden 59 % freigegeben. Der Ist-SOC kann keine weitere Stufe auslösen; er verhindert nur einen Sollwert unterhalb eines bereits erreichten Batteriestands.

Umgekehrt wird eine wegen Bewölkung verpasste Stufe nicht abgewartet. Ist der zeitliche Fahrplan bereits bei 69 %, darf direkt 69 % geschrieben werden, auch wenn Akku oder vorherige Freigabe noch unter 66 % liegen. So kann die Batterie nach einer Unterbrechung wieder aufholen. Die 3 % beschreiben daher das Fahrplanraster und nicht zwingend die maximale Differenz eines einzelnen Schreibtelegramms.

Bei einem Sicherheitsfaktor von 80 % erreicht der Plan-SOC sein Ziel nach ungefähr 80 % der prognostizierten nutzbaren Energie. Die verbleibenden 20 % bilden eine Energiereserve. Der zusätzliche Zeitpuffer „Fertig vor Sonnenuntergang“ bleibt davon unabhängig bestehen.

Die Software begrenzt dabei keine Ladeleistung. Innerhalb eines freigegebenen Abschnitts kann der Wechselrichter mit der verfügbaren Leistung bis zur SOC-Grenze laden. Das Ergebnis sind über den Tag verteilte Ladeblöcke mit Pausen und keine kontinuierliche Ladung mit fest vorgegebenem kleinem Ladestrom.

Die wirksame Tagesgrenze ist mindestens `aufrunden((Max-SOC − 50 %) / Schrittweite)`. Bei 100 % Max-SOC und 3 % Schrittweite sind daher mindestens 17 normale Regelwrites möglich, auch wenn noch der frühere Standardwert 12 gespeichert ist. Eine höher eingestellte Schreibobergrenze bleibt wirksam.

Zum eingestellten Ladeende wird der konfigurierte Max-SOC einmal abschließend geschrieben und zurückgelesen. Diese Sicherheitsfreigabe ist nicht durch das Tageslimit oder den normalen Mindestabstand blockiert. Im Prognose-Tab werden normale Regelwrites und solche Sonderwrites getrennt angezeigt.

Der Max-SOC wird auf Unit-ID 1 an die nullbasierte Holding-Adresse **13057** geschrieben (Herstellerregister 13058, Skalierung 0,1 %) und sofort zurückgelesen. Nur ein passender Rücklesewert gilt als erfolgreicher Schreibvorgang.

Ist die Prognose veraltet oder nicht verfügbar, wird der konfigurierte Max-SOC einmalig als sichere Freigabe wiederhergestellt. Im Bypass wird der Max-SOC ebenfalls einmal geschrieben und geprüft; anschließend werden spätere Änderungen aus der Sungrow-App nicht überschrieben.

## 4. Gelerntes Lastprofil

Der Hausverbrauch wird aus Modbus-Adresse 13007 übernommen und in 96 Viertelstunden je Wochentag gelernt. Leistungen eigener eingeschalteter Überschuss-Stufen werden vorher abgezogen, damit sie nicht fälschlich als normaler Hausverbrauch gelernt werden.

Grundlast und wiederkehrende Großlasten werden getrennt gespeichert. Dadurch kann zum Beispiel eine regelmäßig nachmittags auftretende Autoladung mit ihrer bisherigen Wahrscheinlichkeit in die nächste Prognose eingehen. Neue Beobachtungen werden mit der eingestellten Lernrate in das vorhandene Profil gemischt. Das Profil liegt dauerhaft im NVS-Flash des ESP32 und wird bei Änderungen mindestens stündlich gesichert. Vor einem Browser-Firmwareupdate wird auch der laufende Viertelstundenblock übernommen und das Profil erneut gespeichert. Anschließend wird die Sicherung vollständig zurückgelesen und geprüft. Ein normales Browserupdate lässt den Lernstand dadurch erhalten. „Lernprofil löschen“, ein vollständiges Löschen des ESP32-Flashs oder eine inkompatible Änderung des Profildatenformats setzt ihn weiterhin zurück.

### Anlagenverlauf über 31 Tage

Der Tab **Verlauf** ist von der Lernkurve getrennt. Er zeichnet alle fünf Minuten einen realen Messpunkt für PV-Leistung, Hausverbrauch, Leistung am Netzanschlusspunkt, Batterieleistung und direkten Batterie-SOC auf. Positive Werte des Netzanschlusspunkts erscheinen als Einspeisung, negative Werte als positiver Netzbezug. Der Browser kann 24 Stunden, 7 Tage oder 31 Tage darstellen und jede Kurve einzeln ausblenden.

Leistungen werden intern mit 10-W-Auflösung und der SOC mit 0,1-Prozent-Auflösung tageweise in LittleFS gespeichert. Jeder neue Punkt wird geschrieben und direkt zurückgelesen. Für die Anzeige werden 24 Stunden ohne Verdichtung, 7 Tage in 15-Minuten-Blöcken und 31 Tage in Stundenblöcken ausgeliefert. Bypass oder Prognose ändern die Aufzeichnung nicht. Bei fehlenden oder veralteten Einzelwerten entsteht für die betreffende Kurve eine Lücke, während vorhandene Werte desselben Zeitpunkts erhalten bleiben.

## 5. Seiten des Webinterfaces

| Seite | Funktion |
|---|---|
| **Wechselrichter** | Zeigt die aktiven Wechselrichter-, Energie- und Netzübergabewerte einschließlich des optionalen Netzstatus 13030. Zusätzlich werden die Einspeisegrenze 13073 in Prozent und der Zustand der Begrenzung 13086 angezeigt. Statusfelder melden WLAN, Modbus-Verbindung, Access Point, Überschuss-Stufen und Rundsteuerung. Das Suchfeld filtert Adresse, Name, Beschreibung und Einheit. |
| **Batterie** | Zeigt SOC, SOH, Temperatur, Lade-/Entladeleistung, Kapazität und Energiezähler. Direkter SOC/SOH wird über TCP-ID 2 oder RS485-ID 200 gelesen. |
| **Prognose** | Zeigt Betriebsart, absoluten Batteriestand und freigegebenen SOC, Holding-Werte, erwartete PV-/Last-/Batterieenergie, SOC-Fahrplan, gerasterte Freigabewerte je Uhrzeit sowie stündliche GTI-Werte aller Dachflächen. „Open-Meteo neu laden“ stößt einen neuen Abruf an. |
| **Lastprofil** | Zeigt je Wochentag Grundlast, Großlast-Anteil, Erwartungswert, heutige Messung und Ereigniswahrscheinlichkeit in 15-Minuten-Blöcken. „Lernprofil löschen“ setzt alle Lerndaten zurück. |
| **Verlauf** | Zeigt wahlweise die letzten 24 Stunden, 7 Tage oder 31 Tage für PV, Verbrauch, Einspeisung, Netzbezug, Batterieleistung und direkten Batterie-SOC. Die Kurven lassen sich einzeln ein- und ausblenden. |
| **Einstellungen** | Enthält WLAN, Modbus, Prognose, Dachflächen, Rundsteuerempfänger, fünf Überschuss-Stufen und optionale Pushover-Benachrichtigungen. „Speichern und neu starten“ legt die Werte dauerhaft ab. |
| **Firmware** | Speichert zuerst den aktuellen Lernstand und Anlagenverlauf und verifiziert beide Sicherungen durch vollständiges Rücklesen. Erst danach wird eine zum Board und zur Partitionstabelle passende Firmware-`.bin` installiert. Schlägt die Prüfung fehl, wird das Update abgebrochen. Eine deutlich gekennzeichnete, standardmäßig ausgeschaltete Notfallfreigabe kann den Update-Lockout bewusst umgehen; die betroffenen Profildaten können dann verloren gehen. Vor dem Firmware-Schreiben werden alle Stufen ausgeschaltet; danach startet der ESP32 neu. ArduinoOTA ist nicht enthalten. |
| **About** | Zeigt installierte Firmware-Version, Hersteller, PolyForm Noncommercial 1.0.0, Chipmodell, Flashgröße, aktiven OTA-Slot, System-NVS, Profil-NVS und Verlaufspartition sowie den Quellcode-/Projektlink. Ein eigener Abschnitt erklärt die nichtkommerzielle Nutzung, verweist auf eine separate kommerzielle Lizenzierung und nennt Haftungsausschluss, Nutzung auf eigene Gefahr sowie die Unabhängigkeit von Sungrow. Am Ende kann Web-Debug aktiviert, angezeigt und geleert werden. Der Puffer umfasst maximal 12.000 Zeichen; sehr frühe Bootmeldungen werden nicht erfasst. |

## 6. Einstellungen im Einzelnen

### WLAN

| Einstellung | Bedeutung |
|---|---|
| WLAN-Name | SSID des Heimnetzes. |
| WLAN-Passwort | Leer lassen behält das gespeicherte Passwort; es wird nicht im Browser angezeigt. |
| Zugangsdaten löschen | Entfernt die gespeicherte WLAN-Verbindung. Der eigene Access Point bleibt erreichbar. |

Der Access Point läuft dauerhaft als `Solar-Prognose-XXXXXX`, Standardpasswort `solar123`, normalerweise unter `http://192.168.4.1`. Im Heimnetz ist zusätzlich `http://solar-prognose-monitor.local` vorgesehen.

### Modbus-Verbindung

| Einstellung | Bedeutung |
|---|---|
| Betriebsart | Nur TCP, nur RTU/RS485 oder TCP mit RS485-Ersatz. Im kombinierten Modus wird jeder fehlgeschlagene TCP-Block noch einmal über RS485 versucht. |
| Wechselrichter-ID | Modbus Unit-ID des Wechselrichters, normalerweise 1. |
| Abfrageintervall | Abstand zwischen vollständigen Messzyklen. |
| TCP-Adresse / Port | IP oder Hostname des Modbus-TCP-Geräts; Standardport 502. |
| Batterie-ID über TCP | Direkte Batterie-Unit-ID für Modbus TCP, normalerweise 2. |
| Batterie-ID über RS485 | Direkte Batterie-Unit-ID für Modbus RTU, normalerweise 200. |
| RS485-Baudrate | Busgeschwindigkeit; Datenformat ist 8N1. |
| RX (RO), TX (DI) | ESP32-Pins zum RS485-Transceiver. RX darf auch ein reiner Eingangspin sein. |
| DE/RE | Richtungspin eines Halbduplex-Transceivers oder „Nicht verwendet“ bei automatischer Senderichtung. |

RX, TX, DE/RE, aktive Stufen und Rundsteuer-Eingänge dürfen keinen GPIO doppelt verwenden. Es dürfen keine 5-V-Signale direkt am ESP32 anliegen.

### Prognosebasiertes Laden

| Einstellung | Bedeutung |
|---|---|
| Betriebsart | **Prognose** regelt den Max-SOC laufend; **Bypass** schreibt den eingestellten Max-SOC genau einmal. |
| Breiten-/Längengrad | Standort für Wetter, Sonnenaufgang und Sonnenuntergang. |
| Max-SOC | Einziges Tagesziel im Bereich 50–100 %. |
| Fertig vor Sonnenuntergang | Verschiebt das geplante Ladeende um diese Minuten vor den Sonnenuntergang. |
| Prognose-Sicherheitsfaktor | Bestimmt die Energiereserve; 80 % plant das Ziel nach rund 80 % der erwarteten nutzbaren Energie. |
| PV-Systemwirkungsgrad | Pauschaler Minderungsfaktor zwischen Einstrahlung und realer PV-Leistung. |
| Batterie-Ladewirkungsgrad | Anteil des Überschusses, der als im Akku angekommen gilt. |
| SOC-Schrittweite | Fahrplanraster ausgehend von 50 %. Standard sind 10 %, um die Zahl der persistenten Wechselrichter-Schreibzugriffe zu reduzieren. Werte unter 3 % bleiben auswählbar, verursachen aber besonders viele Schreibzugriffe. Verpasste Stufen werden übersprungen. |
| Mindestabstand Schreibzugriffe | Mindestzeit zwischen zwei prognosebedingten Änderungen. |
| Gewünschte Schreibobergrenze pro Tag | Tageslimit für normale Änderungen. Ist für 50 % bis Max-SOC rechnerisch eine höhere Zahl erforderlich, wird die wirksame Grenze automatisch angehoben. Standard: 20. Einmalige Sicherheits-, Bypass- und abschließende Tagesfreigaben werden getrennt gezählt. |
| Open-Meteo-Intervall | Abstand zwischen automatischen Wetterabrufen. |
| Prognose veraltet nach | Danach wird eine nicht erneuerte Prognose nicht mehr zur Regelung verwendet. |
| Lernrate Lastprofil | Gewicht einer neuen Beobachtung gegenüber dem bereits gespeicherten Profil. |
| Schwelle Großlast | Mehrleistung über der Grundlast, ab der ein separates wiederkehrendes Ereignis gelernt wird. |

> **Wartung und Inbetriebnahme:** Vor Wartungs-, Service-, Umbau- oder Inbetriebnahmearbeiten an Wechselrichter oder Batteriesystem auf **Bypass** umschalten und die Einstellung speichern. Im Bypass wird der Max-SOC einmal geschrieben und geprüft; danach erfolgen keine regelmäßigen Eingriffe. Besonders wichtig ist dies bei einer Batterieerweiterung, weil das Batteriesystem neue Module während der Angleichung automatisch bis ungefähr 40 % laden oder entladen kann. Während der Arbeiten Max-SOC und Betriebsart nicht erneut ändern. Prognose erst nach vollständig abgeschlossenen und durch den Fachbetrieb freigegebenen Arbeiten wieder aktivieren. Hersteller- und Fachbetriebsvorgaben haben immer Vorrang.

### Dachflächen 1 bis 4

Jede Fläche kann einzeln aktiviert und benannt werden. Einzutragen sind die tatsächlich installierten kWp, die Modulneigung und die Open-Meteo-Ausrichtung: 0° Süd, −90° Ost, +90° West und ±180° Nord. Die Vorhersageleistung aller aktivierten Flächen wird addiert.

### Rundsteuerempfänger

Die Funktion kann als 3- oder 4-Kontakt-Auswertung betrieben werden. Im 3-Kontakt-Modus müssen genau drei Eingänge aktiv sein; kein anliegendes Signal wird als 100 % behandelt. Im 4-Kontakt-Modus müssen genau vier Eingänge aktiv sein und einer davon muss ausdrücklich 100 % zugeordnet bekommen. Ohne aktives Signal verändert der ESP32 in diesem Modus den vorhandenen Wechselrichterwert nicht. Jeder verwendete Eingang erhält einen geeigneten ESP32-GPIO, einen Prozentwert von 0 bis 100 und eine eigene Auswahl für den aktiven HIGH- oder LOW-Pegel. Bei LOW kann beispielsweise ein potentialfreier Relaiskontakt oder Optokoppler-Ausgang zwischen GPIO und GND verwendet werden; der interne Pull-up hält den Eingang ohne Signal auf HIGH. Bei HIGH wird ein sauberes 3,3-V-Signal erwartet und bei geeigneten Pins der interne Pull-down genutzt. GPIO 34 bis 39 besitzen keine internen Pull-Widerstände und benötigen unabhängig vom gewählten Pegel eine externe Beschaltung.

Nach einer stabilen Eingangslage von zwei Sekunden wird der Sollwert berechnet:

`Exportlimit in W = Summe der aktiven Dachleistungen in kWp × 1000 × Prozent / 100`

Beispiel: Bei 10 kWp und 60 Prozent wird der Rohwert `6000` geschrieben. Holding-Adresse 13073 ist ein vorzeichenloser 16-Bit-Wert mit Faktor 1 und Einheit Watt. Daher ist die derzeit unterstützte Gesamtleistung auf 65,535 kWp begrenzt. Adresse 13086 aktiviert die Einspeisebegrenzung mit `0xAA` (170) und deaktiviert sie mit `0x55` (85). Jeder Schreibzugriff wird per Holding-Read zurückgelesen.

Sind während eines Kontaktwechsels mehrere Eingänge aktiv, gilt der kleinste Prozentwert. Ist im 3-Kontakt-Modus kein Eingang aktiv, werden 100 % geschrieben. Im 4-Kontakt-Modus bleibt ohne Signal die vorhandene Wechselrichtereinstellung unverändert. Wird die gesamte Rundsteuerfunktion abgeschaltet, schreibt der ESP32 einmalig `0x55` und lässt spätere App-Änderungen unangetastet.

### Überschuss-Stufen 1 bis 5

Jede aktivierte Stufe besitzt Leistung, Typ und getrennte Ein-/Ausschaltverzögerungen. Nur aktivierte Positionen werden in ihrer Nummernreihenfolge kumuliert. Sind beispielsweise nur Stufe 1 mit 1.000 W und Stufe 5 mit 800 W aktiv, schaltet Stufe 1 ab 1.000 W und Stufe 5 ab insgesamt 1.800 W.

- **GPIO:** Auswahl eines geeigneten Ausgangspins; HIGH bedeutet EIN, LOW AUS. Der Pin darf nur ein Relais, SSR oder Schütz ansteuern.
- **Web-API:** GET, PUT oder POST mit getrennten EIN-/AUS-URLs. PUT und POST können je ein JSON-Feld senden; GET ignoriert JSON. Unterstützt werden lokale `http://`-Adressen.
- **Einschaltverzögerung:** Die Einschaltbedingung muss so lange ununterbrochen erfüllt sein.
- **Ausschaltverzögerung:** Die Ausschaltbedingung muss so lange ununterbrochen erfüllt sein.

Zusätzlich gilt eine feste Leistungshysterese von 150 W. Zur gemessenen Netzeinspeisung wird die Leistung bereits eingeschalteter Stufen zurückgerechnet. Liegt der Batterie-SOC hinter dem Prognoseplan, wird außerdem eine Ladeleistungsreserve für die Batterie abgezogen. Fehlt ein aktueller Einspeisewert, werden alle Stufen ausgeschaltet.

### Pushover-Benachrichtigungen

Optional werden ausgewählte Ereignisse und ein Tagesbericht an Pushover gesendet. In einem Ausklappbereich lassen sich Start/Neustart, Modbus-Ausfall/Wiederherstellung und Netzausfall/Netzwiederkehr getrennt aktivieren. Für den einmaligen Bericht nach Sonnenuntergang sind PV-Tagesertrag 13001 und absoluter Batteriestand 10743 einzeln auswählbar. Dafür werden der Application/API-Token einer eigenen Pushover-Anwendung und der persönliche User-/Group-Key hinterlegt; ein Gerätebezeichner kann den Empfang auf ein Gerät begrenzen. Die beiden Schlüssel werden nach dem Speichern nicht wieder in das Webformular eingesetzt. Leere Schlüsselfelder behalten vorhandene Werte, das Löschfeld entfernt sie ausdrücklich.

Die Ausfallverzögerung unterdrückt kurze Modbus- und Netzstatusstörungen. Der Netzstatus stammt aus dem Sungrow-Herstellerregister 13030, das im nullbasierten Sketch als Adresse 13029 gelesen wird: `0x55` steht für Netzbetrieb, `0xAA` für Inselbetrieb/Netzausfall. Das Register ist optional, damit ältere oder abweichende Firmwarestände die übrige Abfrage nicht blockieren. Nach erfolgreicher Ausfallmeldung entsteht keine wiederholte Dauermeldung; nach Wiederherstellung folgt genau ein weiterer Zustandswechsel. Das Datum eines erfolgreich gesendeten Tagesberichts bleibt im NVS gespeichert und verhindert eine Doppelmeldung nach Neustart. Scheitert die HTTPS-Übertragung, bestimmt der Wiederholungsabstand den nächsten Versuch. Der Testknopf prüft neue Eingaben ohne vorheriges Speichern. Der offizielle Pushover-Endpunkt wird per HTTPS mit Zertifikatsprüfung angesprochen.

Bei vollständigem Strom- oder Internetausfall kann der ESP32 naturgemäß keine Sofortmeldung übertragen. Auch bei nahtlosem Inselbetrieb müssen ESP32, Router und Internetzugang weiter versorgt werden; die Netzwerkkomponenten müssen dafür am Ersatzstrom hängen. Sobald Gerät, WLAN, Internet und NTP-Zeit wieder verfügbar sind, wird der Start/Neustart gemeldet. Die Funktion ist eine Komfortbenachrichtigung und keine zertifizierte Alarmanlage.

## 7. Angezeigte Modbus-Werte

Die Weboberfläche verwendet die nullbasierten Modbus-Adressen aus dem Sketch:

| Ansicht | Adresse | Anzeige |
|---|---:|---|
| Wechselrichter | 5007 | Wechselrichtertemperatur |
| Wechselrichter | 5016 | aktuelle PV-Leistung |
| Wechselrichter | 5746 / 5748 | gesamter Netzbezug / gesamte Netzeinspeisung des Smart Meters |
| Wechselrichter | 13001 / 13002 | PV-Erzeugung heute / gesamt |
| Wechselrichter | 13004 / 13005 | PV-Einspeiseenergie heute / gesamt |
| Wechselrichter | 13007 | aktuelle Haus-/Lastleistung; Grundlage des Lernprofils |
| Wechselrichter | 13009 | Leistung am Netzanschlusspunkt; positive Werte werden als Einspeisung verwendet |
| Wechselrichter | 13029 | Netzstatus aus Herstellerregister 13030: `0x55` Netzbetrieb, `0xAA` Inselbetrieb/Netzausfall; optional |
| Wechselrichter (Holding) | 13073 | aktuelles Exportlimit in Watt, im Webinterface auf die konfigurierte PV-Leistung in Prozent umgerechnet |
| Wechselrichter (Holding) | 13086 | Einspeisebegrenzung EIN (`0xAA`) oder AUS (`0x55`) |
| Batterie über ID 1 | 13011 / 13012 | aus PV geladene Batterieenergie heute / gesamt |
| Batterie über ID 1 | 13021 | aktuelle Batterieleistungsabgabe; Entladen positiv, Laden negativ; U16-Betrag mit Richtung aus der Leistungsbilanz |
| Batterie über ID 1 | 13022 / 13023 | relativer Batteriestand zum aktuellen Max-SOC / SOH über den Wechselrichter |
| Batterie über ID 1 | 13024 / 13038 | Batterietemperatur / Batteriekapazität |
| Batterie über TCP-ID 2 oder RS485-ID 200 | 10743 / 10744 | absoluter Batteriestand / Batterie-SOH aus Batteriedaten |

Für die Regelung wird ausschließlich der direkte SOC aus Adresse 10743 verwendet: über TCP mit ID 2 oder über RS485 mit ID 200. Der möglicherweise relativ skalierte Wechselrichterwert aus ID 1, Adresse 13022, bleibt sichtbar, wird aber nicht mehr als Rückfallwert für die Regelung benutzt. Fehlt der direkte SOC, pausiert die Prognose und gibt den konfigurierten Max-SOC einmalig sicher frei. Die Kapazität stammt aus Adresse 13038 und wird bei der getesteten Anlage mit Faktor 0,01 in kWh umgerechnet.

## 8. Hinweise zur Abstimmung

Für die erste Inbetriebnahme sind 95 % PV-Systemwirkungsgrad, 95 % Batterie-Ladewirkungsgrad und 80 % Sicherheitsfaktor die voreingestellten Ausgangswerte.

1. Zuerst an klaren Tagen den PV-Systemwirkungsgrad anhand vorhergesagter und realer Erzeugung abstimmen.
2. Den Batterie-Ladewirkungsgrad normalerweise im Bereich 90–95 % belassen.
3. Danach den Sicherheitsfaktor an die örtliche Prognosequalität anpassen. Wird der Akku zu oft zu spät voll, den Wert reduzieren. Wird er regelmäßig sehr früh voll, kann er vorsichtig erhöht werden.

Die drei Faktoren sollten nicht gleichzeitig stark verändert werden, weil sonst nicht mehr erkennbar ist, welche Korrektur gewirkt hat.
