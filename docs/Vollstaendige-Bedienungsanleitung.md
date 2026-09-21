# Vollständige Bedienungsanleitung

Stand: Firmware **1.2.0**

Diese Anleitung beschreibt jede Seite und jedes sichtbare Bedien- oder Anzeigefeld des Solar Prognose Monitors. Die mathematischen Hintergründe stehen bewusst gesammelt im letzten Kapitel. Die erste Geräteintegration unterstützt Sungrow-Wechselrichter und -Batteriespeicher.

## 1. Sicherheit und Voraussetzungen

- Der ESP32 arbeitet ausschließlich mit 3,3-V-Logik. Niemals 5 V oder Netzspannung direkt an einen GPIO legen.
- Heizpatronen und andere Netzverbraucher dürfen nicht direkt vom ESP32 geschaltet werden. Erforderlich sind passende Relais, SSRs oder Schütze sowie ein fachgerecht abgesicherter Aufbau.
- Rundsteuerkontakte müssen potentialfrei gegen GND schalten oder ein sauberes 3,3-V-Signal liefern. Optokoppler müssen auf der ESP32-Seite passend beschaltet sein.
- GPIO 34, 35, 36 und 39 besitzen keine internen Pull-Widerstände. Bei ihrer Verwendung als Eingang ist immer eine externe Pegelfestlegung nötig.
- Schreibzugriffe auf den Wechselrichter sollten bei der ersten Inbetriebnahme beobachtet und mit der Sungrow-Anzeige verglichen werden.
- Benötigt wird ein klassischer ESP32, unterstützt werden 4 MB und 8 MB Flash. Wegen der größeren Update-Reserve wird 8 MB empfohlen.

## 2. Erster Start und Erreichbarkeit

1. Firmware einschließlich der mitgelieferten Partitionstabelle per USB auf den ESP32 übertragen. Für einen frischen ESP32 muss Version 1.0.1 oder neuer verwendet werden. Version 1.0.0 kann dort wegen eines falschen App-Offsets in ihrer Partitionstabelle nicht starten. Eine USB-Komplett-/Factory-Datei ist ausschließlich für eine Neuinstallation ohne Datenübernahme vorgesehen.
2. Mit dem Access Point `Solar-Prognose-XXXXXX` verbinden. Das Standardpasswort lautet `solar123`.
3. Im Browser `http://192.168.4.1` öffnen.
4. Unter **Einstellungen** Heim-WLAN, Modbus und Anlagendaten eintragen.
5. **Speichern und neu starten** wählen.

Der Access Point bleibt auch nach erfolgreicher Verbindung mit dem Heim-WLAN aktiv. Im Heimnetz ist die Oberfläche über die angezeigte IP und normalerweise über `http://solar-prognose-monitor.local` erreichbar. Alle Einstellungen, das Lastprofil und der Anlagenverlauf liegen lokal auf dem ESP32; ein eigener Server ist nicht nötig.

## 3. Tab „Wechselrichter“

### Statuszeile

| Anzeige | Bedeutung |
|---|---|
| WLAN | Name des verbundenen Heimnetzes und lokale IP. „Nicht verbunden“ betrifft nicht den weiterhin laufenden Access Point. |
| ID … OK | Modbus-Status der Wechselrichter-ID, Zahl der gültigen Werte und zuletzt verwendeter Transportweg. Bei einem Fehler steht hier die konkrete Ursache. |
| Netzeinspeisung | Aktuelle Leistung am Netzanschlusspunkt aus Adresse 13009. Positive Werte gelten als Einspeisung. |
| verfügbar | Für die Überschuss-Stufen berechnete Leistung nach Rückrechnung bereits eingeschalteter Stufen und gegebenenfalls Batterie-Reserve. |
| Stufen | Anzahl eingeschalteter und aktivierter Überschuss-Stufen. |
| Rundsteuerung | Aktuelle Kontaktstufe, berechnetes Watt-Limit, Bereitschaft oder Fehler beim Schreiben/Rücklesen. |
| AP | Name und IP des eigenen ESP32-Access-Points. |

Das Suchfeld filtert die Tabelle nach Adresse, englischem Namen, deutscher Beschreibung oder Einheit.

### Messwertzeilen

| Adresse | Sichtbare Zeile | Zweck |
|---:|---|---|
| 5007 | Inside Temperature | Temperatur im Wechselrichter in °C. |
| 5016 | Total DC Power | Momentane gesamte PV-Leistung in W. |
| 5746 | DTSU666 import energy | Gesamter Netzbezug des Smart Meters in kWh. Das Register wird optional über die feste Unit-ID 254 gelesen; ein Fehler beeinflusst die übrige Abfrage nicht. |
| 5748 | DTSU666 export energy | Gesamte Netzeinspeisung des Smart Meters in kWh, ebenfalls optional über Unit-ID 254. |
| 13001 | Daily PV Generation | PV-Erzeugung des aktuellen Tages in kWh. |
| 13002 | Total PV Generation | Gesamte PV-Erzeugung in kWh. |
| 13004 | Daily export energy from PV | Heute aus PV eingespeiste Energie in kWh. |
| 13005 | Total export energy from PV | Insgesamt aus PV eingespeiste Energie in kWh. |
| 13007 | Load power | Momentaner Hausverbrauch in W; zugleich Eingangswert für das lernende Lastprofil. |
| 13009 | Export power | Leistung am Netzanschlusspunkt in W; positive Werte werden als Einspeisung, negative als Netzbezug behandelt. |
| 13029 | Grid state | Netzstatus aus Herstellerregister 13030: `0x55` Netzbetrieb, `0xAA` Inselbetrieb/Netzausfall. Optional, da nicht jeder Firmwarestand das Register bereitstellt. |
| 13073 | Export Power Limit | Aktuell gelesene maximale Einspeiseleistung. Im Text wird sie anhand der konfigurierten PV-Leistung zusätzlich in Prozent umgerechnet. |
| 13086 | Export Power Limitation | Zeigt, ob die Wechselrichterbegrenzung eingeschaltet (`0xAA`) oder ausgeschaltet (`0x55`) ist. |

Ein Gedankenstrich bedeutet, dass noch kein frischer Wert vorliegt. „Nicht unterstützt“ bei 5746 oder 5748 bedeutet, dass das Smart Meter beziehungsweise die Weiterleitung über Unit-ID 254 die optionale Adresse abgewiesen hat; alle anderen Messwerte werden trotzdem weitergelesen.

## 4. Tab „Batterie“

### Statuszeile

| Anzeige | Bedeutung |
|---|---|
| WLAN | Heimnetz und lokale IP. |
| Wechselrichter-ID | Status der Werte, die über die Wechselrichter-ID gelesen werden. |
| TCP-ID 2 / RS485-ID 200 | Status und Transportweg der direkten Batterieabfrage. Die IDs sind in den Einstellungen veränderbar. |
| AP | Access-Point-Name und IP. |

### Messwertzeilen

| Adresse | Sichtbare Zeile | Zweck |
|---:|---|---|
| 13011 | Daily battery charge energy from PV | Aus PV in die Batterie geladene Energie des Tages; Rohwert × 0,1 kWh. |
| 13012 | Total battery charge energy from PV | Insgesamt aus PV in die Batterie geladene Energie; Rohwert × 0,1 kWh. |
| 13021 | Battery power | Momentane Batterieleistungsabgabe in W: Entladen positiv, Laden negativ. Das Register wird als U16 gelesen; die Richtung wird aus PV-Leistung, Hausverbrauch und Netzleistung abgeleitet. |
| 13022 | Battery level | „Relativer Batteriestand zum aktuellen Max-SOC-Wert“. Dieser Wechselrichterwert wird nur angezeigt, nicht geregelt. |
| 13023 | Battery state of health | Vom Wechselrichter gemeldeter SOH. |
| 13024 | Battery temperature | Batterietemperatur in °C. |
| 13038 | Battery capacity | Gemeldete Batteriekapazität in kWh, Rohwert × 0,01; Grundlage der Energie-zu-SOC-Umrechnung. |
| 10743 | Battery1 SOC | „Absoluter Batteriestand“. Er wird über TCP-ID 2 oder RS485-ID 200 gelesen und ist alleinige SOC-Grundlage der Prognoseregelung. |
| 10744 | Battery1 SOH | „Batterie SOH aus Batteriedaten“ über dieselbe Batterie-ID. |

Fehlt Adresse 10743, verwendet die Software den möglicherweise relativen Wert 13022 ausdrücklich nicht als Ersatz. Die Prognoseregelung pausiert dann und versucht einmalig, den eingestellten Max-SOC sicher freizugeben.

## 5. Tab „Prognose“

Die Betriebsart wird hier nur angezeigt. Umschalten ist ausschließlich unter **Einstellungen** möglich, damit Prognose und Bypass nicht gleichzeitig oder widersprüchlich aktiviert werden können.

### Vier Statuskarten

| Karte/Zeile | Bedeutung |
|---|---|
| Steuerung | Zeigt **Prognose** oder **Bypass**, bei aktiver Prognose die gewählte Strategie **Ideal laden** oder **Vorausladen** sowie den aktuellen Regelstatus, etwa Wetterabruf, fehlenden SOC oder erfolgreichen Schreibvorgang. |
| Absoluter Batteriestand / freigegebener SOC | Links steht der absolute Batteriestand 10743, rechts der tatsächlich zurückgelesene Max-SOC des Wechselrichters. Darunter stehen Transportweg, zeitbasiertes Fahrplan-Soll und Min-SOC. |
| PV / gelernte Last | Erwartete PV-Energie und erwarteter Hausverbrauch im betrachteten Zeitraum. Darunter stehen erwartete Ladung, erwartete Entladung und die daraus gebildete Netto-Batterieenergie. |
| Prognoseplan / min. Batteriestand zum Erreichen des Max-SOC | Zwei unabhängig berechnete SOC-Untergrenzen. Der zweite Wert zeigt, wie hoch der Batteriestand jetzt mindestens sein müsste, damit die verbleibende prognostizierte Energie noch zum eingestellten Max-SOC führt. Der höhere Wert bestimmt die nächste Freigabe. Angezeigt werden außerdem Ladeende, normale Regelwrites samt Tageslimit und Sonderwrites. |

### Schaltfläche und Diagramme

| Element | Bedeutung |
|---|---|
| Open-Meteo neu laden | Plant sofort einen neuen Wetterabruf ein. Die Antwort erfolgt im Hintergrund; die Statusmeldung aktualisiert sich danach. |
| PV-Prognose und gelerntes Lastprofil | Zeigt in 15-Minuten-Schritten PV-Leistung, den jeweils passenden gelernten Lastprofilwert und die daraus erwartete positive Lade- oder negative Entladeenergie. Datum und Uhrzeit stehen auf der X-Achse. |
| SOC-Fahrplan | Zeigt Prognoseplan, mindestens erforderlichen Batteriestand und den daraus gerasterten Freigabe-Zeitplan. Senkrechte Linien markieren den aktuellen Zeitpunkt und das geplante Ladeende. |
| Open-Meteo-Werte je Dachfläche | Tabelle mit Datum/Uhrzeit, Gesamt-PV, erwarteter Last, Batterieenergie mit Vorzeichen, beiden SOC-Berechnungen, gerasterter Freigabe und der Einstrahlung auf jede aktivierte Dachfläche. Positiv bedeutet erwartete Ladung, negativ erwartete Entladung. |

## 6. Tab „Stromtarif“

Der Tab zeigt den Preis- und Netzladeplan für den Zeitraum bis zum nächsten PV-Tag. Die vier Statuskarten enthalten Betriebsart, prognostizierten SOC und Energielücke, geplante Netzenergie/Kosten/Ziel-SOC sowie die ausschließlich gelesene Zwangslade- und Maximalleistung. Im Diagramm und in der Tabelle sind ausgewählte Ladeintervalle grün markiert. **Preise und Plan neu laden** stößt einen neuen Abruf an.

**Nur planen** führt keine Wechselrichter-Schreibzugriffe aus. Erst die zweite Auswahl **Automatische Netzladung ausdrücklich freigeben** erlaubt die Ausführung. Ein Start ist nur bei aktuellen Preis- und Wetterdaten, absolutem Batterie-SOC 10743, plausiblem Batteriekapazitätswert, sicherem On-grid-Status sowie plausibler voreingestellter Zwangslade- und Maximalleistung möglich. Entfällt eine dieser Bedingungen, wird nicht gestartet beziehungsweise gestoppt.

Die Firmware schreibt weder Zwangsladeleistung noch maximale Ladeleistung, Ladestrom oder BMS-Grenzen. Zum Start verwendet sie nur EMS-Modus, Ladebefehl und den berechneten SOC-Grenzwert. Vorher speichert sie den bisherigen EMS-Zustand und SOC-Grenzwert dauerhaft. Nach dem Ladefenster, beim Erreichen des geplanten SOC, bei Fehlern und nach einem Neustart wird zuerst `Stop` gesendet und anschließend werden vorheriger EMS-Modus und vorheriger SOC-Grenzwert wiederhergestellt.

## 7. Tab „Lastprofil“

| Zeile/Bedienung | Bedeutung |
|---|---|
| Wochentag | Wählt den angezeigten Wochentag aus. Jeder Tag besitzt 96 eigene Viertelstundenblöcke. |
| Gelernte Tage | Anzahl der bisherigen Lerndurchläufe für den ausgewählten Wochentag. |
| aktuelle 15-Minuten-Position | Nummer des gerade laufenden Viertelstundenblocks. |
| Lernprofil löschen | Löscht nach Sicherheitsabfrage Grundlast, Großlasten, Wahrscheinlichkeiten und heutige Vergleichswerte. |
| Diagramm Grundlast | Langfristig gelernter normaler Verbrauch. |
| Diagramm Großlast | Erwarteter Anteil einer wiederkehrenden Großlast, also gelernte Ereignisleistung mal Wahrscheinlichkeit. |
| Diagramm Erwartung | Summe aus gelernter Grundlast und erwartetem Großlastanteil. Diese Kurve fließt in die PV-Prognose ein. |
| Diagramm Heute | Heute tatsächlich beobachteter, bereinigter Verbrauch. |

Die Tabelle darunter enthält für jeden 15-Minuten-Block die Spalten **Zeit**, **Grundlast**, **Großlast-Anteil**, **Erwartung**, **Heute** und **Wahrscheinlichkeit**. Eigene eingeschaltete Überschuss-Stufen werden vor dem Lernen abgezogen. Dadurch lernt die Software nicht versehentlich die von ihr selbst zugeschaltete Heizpatrone als normalen Hausverbrauch. Das Profil lernt in Prognose und Bypass weiter. In der Ladeprognose werden alle vier Viertelstunden einer Open-Meteo-Stunde einzeln verrechnet; eine um `:15`, `:30` oder `:45` erwartete Last wird nicht mehr durch den Wert am Stundenanfang ersetzt.

Das Lernprofil wird mindestens stündlich und unmittelbar vor einem Browser-Firmwareupdate gespeichert. Ein normales Firmwareupdate erhält es. Es geht nur durch **Lernprofil löschen**, vollständiges Löschen des ESP32-Flashs oder ein zukünftig inkompatibles Speicherformat verloren.

## 8. Tab „Verlauf“

| Zeile/Bedienung | Bedeutung |
|---|---|
| Ansicht | Wählt Tag (24 Stunden), Woche (7 Tage) oder Monat (31 Tage). |
| Status | Anzahl und zeitliche Auflösung der dargestellten Punkte, Zeitpunkt des letzten Punkts, Belegung der Verlaufspartition und Speicherstatus. |
| PV | Letzter PV-Leistungswert sowie grüne Kurve. |
| Verbrauch | Letzter Hausverbrauch sowie gelbe Kurve. |
| Einspeisung | Positiver Anteil des Netzanschlusspunktwerts sowie pinke Kurve. |
| Netzbezug | Betrag eines negativen Netzanschlusspunktwerts sowie rote Kurve. |
| Batterieleistung | Letzter Wert von Adresse 13021 sowie blaue Kurve. |
| Batterie-SOC | Letzter direkter SOC 10743 sowie dunkelgrüne Kurve auf der rechten Prozentskala. |
| Kontrollkästchen der Legende | Blendet die jeweilige Kurve ein oder aus, ohne Messwerte zu löschen. |

Der Verlauf schreibt alle fünf Minuten einen kompakten Messpunkt in eine Tagesdatei der separaten LittleFS-Partition. Gespeichert werden bis zu 31 Tage. Die Tagesansicht verwendet 5-Minuten-Werte, die Wochenansicht bildet 15-Minuten-Mittelwerte und die Monatsansicht Stundenmittelwerte. Die X-Achse zeigt Datum und Uhrzeit. Fehlende Einzelwerte werden als Lücke dargestellt. Jeder neue Punkt wird geschrieben und direkt zurückgelesen; die Aufzeichnung läuft unabhängig von Prognose oder Bypass. Tagesdateien außerhalb der Aufbewahrungsfrist werden automatisch entfernt.

## 9. Tab „Einstellungen“

**Speichern und neu starten** legt die Werte dauerhaft ab. Das WLAN-Passwort, der Stromtarif-API-Token sowie Pushover-Token und Pushover-User-Key werden nie zurück in das Formular geschrieben; leere Zugangsdatenfelder behalten den jeweiligen gespeicherten Wert, solange nicht ausdrücklich „löschen“ gewählt wird.

### WLAN

| Feld | Wirkung |
|---|---|
| WLAN-Name (SSID) | Name des Heimnetzes. |
| WLAN-Passwort | Passwort des Heimnetzes. Leer lassen behält das gespeicherte Passwort. |
| WLAN-Zugangsdaten löschen | Entfernt SSID und Passwort. Der Access Point bleibt erreichbar. |

### Modbus

| Feld | Wirkung |
|---|---|
| Betriebsart | **Nur TCP**, **nur RTU/RS485** oder **TCP mit RS485-Ersatz**. Beim Ersatzmodus wird ein fehlgeschlagener TCP-Leseblock noch einmal über RS485 versucht. |
| Wechselrichter-ID | Unit-ID des Wechselrichters, normalerweise 1. |
| Abfrageintervall | Abstand zwischen vollständigen Messzyklen, 2 bis 300 Sekunden. |
| TCP-Adresse | IP-Adresse oder Hostname des Wechselrichters beziehungsweise Kommunikationsmoduls; Standard bei einer neuen Konfiguration ist `192.168.0.2`. |
| TCP-Port | Normalerweise 502. |
| Batterie-ID über TCP | Direkte Batterieabfrage, normalerweise ID 2. |
| Batterie-ID über RS485 | Direkte Batterieabfrage, normalerweise ID 200. |
| RS485-Baudrate | Übertragungsgeschwindigkeit; Datenformat 8N1. |
| RX / RO | ESP32-Eingang vom RO-Ausgang des RS485-Transceivers. |
| TX / DI | ESP32-Ausgang zum DI-Eingang des Transceivers. |
| DE/RE | Richtungspin eines Halbduplex-Transceivers. Bei automatischer Senderichtung „Nicht verwendet“ wählen. |

### Prognosebasiertes Laden

| Feld | Wirkung |
|---|---|
| Betriebsart | **Prognose aktiv** hebt Max-SOC schrittweise an. **Bypass aktiv** schreibt den eingestellten Max-SOC einmal, liest ihn zurück und sendet ihn danach nicht regelmäßig erneut. |
| Ladestrategie | **Ideal laden** behält den bisherigen energie- und lastabhängigen Fahrplan bei. **Vorausladen** setzt zusätzlich eine vorsichtige Zeituntergrenze: mindestens 80 % bis zur Mitte zwischen Sonnenaufgang und geplantem Ladeende, danach langsamer Anstieg bis zum Max-SOC. Sinnvoll bei stark wechselnden oder noch schlecht gelernten Lasten. Wirkt nur bei aktiver Prognose. |
| Breitengrad / Längengrad | Standort für Open-Meteo sowie Sonnenauf- und -untergang. |
| Max-SOC | Einziger SOC-Zielwert, zulässig 50 bis 100 %. Im Bypass sofortige, in der Prognose schrittweise Freigabe. |
| Fertig vor Sonnenuntergang | Anzahl Minuten, um die der Akku vor dem berechneten Sonnenuntergang fertig sein soll. |
| Prognose-Sicherheitsfaktor | Energiereserve für Prognosefehler und ungeplante Verbraucher. Bei 80 % erreicht der Fahrplan sein Ziel bereits nach rund 80 % der erwarteten nutzbaren Energie; die übrigen 20 % bleiben als Reserve. Kleiner bedeutet vorsichtiger und frühere Freigabe. |
| PV-Systemwirkungsgrad | Pauschaler Anlagenfaktor für Temperatur, Kabel, Wechselrichter, Verschmutzung, Mismatch und kleinere Verschattung. Nicht mit dem Modulwirkungsgrad verwechseln. Standard: 95 %. |
| Batterie-Ladewirkungsgrad | Anteil des PV-Überschusses, der als im Akku angekommen gerechnet wird. Standard: 95 %. |
| Batterie-Entladewirkungsgrad | Berücksichtigt Speicherverluste, wenn eine wiederkehrende Last größer als die erwartete PV-Leistung ist. Standard: 95 %. |
| SOC-Schrittweite | Raster der Max-SOC-Freigaben, ausgehend von 50 %. Standard sind 10 %, um die Zahl der persistenten Wechselrichter-Schreibzugriffe zu reduzieren. Bei 3 % entstehen beispielsweise 50, 53, 56 … 100 %. Werte unter 3 % erhöhen die Schreibzahl besonders stark. |
| Mindestabstand Schreibzugriffe | Mindestzeit zwischen normalen prognosebedingten Änderungen. |
| Gewünschte Schreibobergrenze pro Tag | Tagesgrenze normaler Regelwrites. Reicht sie nicht aus, um den Bereich von 50 % bis Max-SOC in der gewählten Schrittweite abzudecken, wird sie automatisch auf die notwendige Zahl angehoben. Bypass-, Sicherheits- und abschließende Tagesfreigaben werden separat gezählt und bleiben möglich. Standard: 20. |
| Open-Meteo-Intervall | Zeit zwischen automatischen Wetterabrufen. |
| Prognose gilt als veraltet nach | Maximales Alter, nach dem Wetterdaten nicht mehr für eine normale Regelung verwendet werden. |
| Lernrate Lastprofil | Gewicht einer neuen Beobachtung gegenüber dem gespeicherten Wert. |
| Schwelle wiederkehrende Großlast | Mehrleistung über der Grundlast, ab der ein separates Ereignis wie regelmäßiges Autoladen gelernt wird. |

#### Wartungsarbeiten und Batterieerweiterung

Vor Wartungs-, Service-, Umbau- oder Inbetriebnahmearbeiten an Wechselrichter oder Batteriesystem muss die Betriebsart auf **Bypass aktiv** gestellt und mit **Speichern und neu starten** übernommen werden. Dadurch darf die Prognoseregelung keine wiederholten Max-SOC-Freigaben mehr ausführen, während ein Fachbetrieb Einstellungen ändert, Komponenten prüft oder herstellerspezifische Serviceabläufe durchführt.

Besonders wichtig ist dies beim Einbau oder bei der Inbetriebnahme zusätzlicher Batteriemodule. Das Batteriesystem kann für die vorgeschriebene Angleichung automatisch bis ungefähr 40 % laden oder entladen. Die Prognoseregelung darf diesen Vorgang nicht beeinflussen. Im Bypass schreibt der Solar Prognose Monitor den eingestellten Max-SOC einmalig und prüft ihn durch Rücklesen. Nach Abschluss dieses Einmalauftrags erfolgen keine regelmäßigen Max-SOC-Schreibzugriffe mehr. Während der Arbeiten weder den konfigurierten Max-SOC ändern noch erneut zwischen Prognose und Bypass umschalten, weil dadurch ein neuer Schreibauftrag entstehen kann. Erst wenn sämtliche Wartungs-, Erweiterungs-, Angleichungs- und Inbetriebnahmeprozeduren vollständig beendet und durch den Fachbetrieb freigegeben sind, darf wieder auf **Prognose aktiv** umgestellt werden. Herstelleranleitung und Vorgaben des ausführenden Fachbetriebs haben immer Vorrang.

### Dachflächen 1 bis 4

| Feld je Dachfläche | Wirkung |
|---|---|
| Aktiviert | Bezieht die Fläche in Wetterprognose und installierte Gesamtleistung der Rundsteuerung ein. |
| Name | Freie Bezeichnung, beispielsweise „Ost“, „West“ oder „Garage“. |
| Installierte Leistung | Nennleistung dieser Fläche in kWp. |
| Neigung | Modulneigung von 0 bis 90 Grad. |
| Ausrichtung | Open-Meteo-Konvention: 0° Süd, −90° Ost, +90° West, ±180° Nord. |

### Rundsteuerempfänger

| Feld | Wirkung |
|---|---|
| Einspeisebegrenzung … aktivieren | Schaltet die automatische Auswertung insgesamt ein. Beim späteren Ausschalten wird Adresse 13086 einmal mit `0x55` deaktiviert und zurückgelesen. |
| Signalart: 3 Kontakte | Genau drei Eingänge müssen aktiviert sein. Kein aktiver Eingang wird nach zwei Sekunden als 100 % ausgewertet. Typische Kontakte sind beispielsweise 60 %, 30 % und 0 %. |
| Signalart: 4 Kontakte | Genau vier Eingänge müssen aktiviert sein, darunter ein explizit auf 100 % eingestellter Eingang. Ohne aktiven Eingang verändert der ESP32 das zuletzt vorhandene Wechselrichterlimit nicht. |
| Eingang aktiviert | Bezieht diese Zeile in die Kontaktabfrage ein. Die Zahl aktivierter Eingänge muss zur Signalart passen. |
| GPIO | ESP32-Pin, an dem das Signal anliegt. Er darf nicht gleichzeitig für RS485 oder eine aktive GPIO-Überschuss-Stufe verwendet werden. |
| Einspeisegrenze (%) | Prozentanteil der installierten Gesamtleistung für diesen Kontakt. |
| Aktiver Pegel LOW | Eingang ist aktiv, wenn er auf GND gezogen wird. Geeignet für viele potentialfreie Kontakte und Open-Collector-Optokoppler; an GPIO unter 34 wird der interne Pull-up verwendet. |
| Aktiver Pegel HIGH | Eingang ist aktiv, wenn 3,3 V anliegen; an GPIO unter 34 wird der interne Pull-down verwendet. |

Bei gleichzeitig aktiven Eingängen gilt immer der kleinste Prozentwert. Erst wenn die Eingangslage zwei Sekunden stabil ist, wird sie übernommen. Der berechnete Wattwert wird an Holding-Adresse 13073 geschrieben, mit Rücklesen geprüft und die Begrenzung an 13086 mit `0xAA` aktiviert.

### Überschuss-Stufen 1 bis 5

| Feld je Stufe | Wirkung |
|---|---|
| Aktiviert | Nur aktivierte Stufen nehmen an der kumulativen Berechnung teil. |
| Leistung | Leistungsbedarf dieser Last in W. Er dient zugleich als Anteil an der kumulativen Schaltschwelle. |
| Typ | **GPIO** schaltet einen Hardwareausgang; **Web-API** sendet HTTP-Befehle. |
| Einschaltverzögerung | Einschaltbedingung muss für diese Sekunden ohne Unterbrechung bestehen. |
| Ausschaltverzögerung | Ausschaltbedingung muss für diese Sekunden ohne Unterbrechung bestehen. |
| GPIO | Bei Typ GPIO verwendeter Ausgang. HIGH bedeutet EIN, LOW bedeutet AUS. |
| Methode | Bei Web-API GET, PUT oder POST. |
| EIN-URL / AUS-URL | Getrennte lokale `http://`-Adresse für beide Zustände. |
| EIN-JSON / AUS-JSON | Optionaler Request-Inhalt für PUT oder POST; bei GET ausgeblendet und ignoriert. |

Nur aktivierte Stufen werden in Nummernreihenfolge addiert. Sind nur Stufe 1 mit 1.000 W und Stufe 5 mit 800 W aktiv, lauten die Schwellen 1.000 W und 1.800 W. Zusätzlich gelten 150 W Leistungshysterese und die getrennten Zeitverzögerungen. Fehlt der aktuelle Einspeisewert, werden alle Stufen sicher ausgeschaltet. Fehlgeschlagene API-Aufrufe werden frühestens nach zehn Sekunden erneut versucht.

### Dynamischer Stromtarif

| Feld | Wirkung |
|---|---|
| Preis- und Netzladeplan berechnen | Aktiviert Abruf und Vorschau. Alle Wechselrichter-Schreibzugriffe der Tarifautomatik bleiben aus. |
| Automatische Netzladung ausdrücklich freigeben | Zweite, unabhängige Freigabe. Nur zusammen mit der Planung aktiv. |
| Anbieter | Tibber, aWATTar Deutschland, Octopus Energy oder eigene REST-API. |
| API-URL | Bei Octopus vollständige `standard-unit-rates`-URL; bei eigener API deren Endpunkt. Tibber und aWATTar besitzen feste Endpunkte. |
| API-Token | Für Tibber erforderlich, für eine eigene API optional als Bearer-Token. Leer behält einen gespeicherten Token. |
| Höchster Ladepreis | Nur Intervalle bis einschließlich dieser Preisgrenze werden berücksichtigt. Bei Octopus sind Preis und Grenzwerte in Pence/kWh, sonst in ct/kWh. |
| Vergleichspreis Netzbezug | Preis, mit dem ein späterer normaler Netzbezug verglichen wird. |
| Mindestersparnis | Nach Berücksichtigung des Ladewirkungsgrads muss mindestens diese Differenz zum Vergleichspreis verbleiben. |
| aWATTar-Aufschlag / Umsatzsteuer | Wandelt den Börsenpreis von EUR/MWh in einen näherungsweisen persönlichen Endpreis um. Andere Anbieter liefern bereits Endpreiswerte. |
| Maximaler SOC durch Netzladung | Obergrenze, bis zu der die Tarifautomatik aus dem Netz laden darf. |
| Planungsreserve | Zusätzliche vorsichtige Energiemenge in Prozent der Batteriekapazität; weiterhin durch den maximalen Netzlade-SOC begrenzt. |

Die eigene API muss `{"prices":[{"start":UNIX-Sekunden,"end":UNIX-Sekunden,"price":ct/kWh}]}` liefern. Native Anbieter werden per HTTPS mit Zertifikatsprüfung abgerufen. Eine benutzerdefinierte HTTPS-API muss einen unterstützten öffentlichen Zertifikatsweg verwenden; eine lokale unverschlüsselte API ist nur über `http://` vorgesehen.

### Pushover-Benachrichtigungen

| Feld | Wirkung |
|---|---|
| Meldungen aktivieren | Schaltet automatische Pushover-Nachrichten ein. Token und User-Key müssen gültig gespeichert sein. |
| Application/API Token | 30-stelliger Schlüssel einer selbst angelegten Pushover-Anwendung. Ein leeres Feld behält den gespeicherten Wert. Er wird nach dem Speichern nicht mehr angezeigt. |
| User-/Group-Key | 30-stelliger Empfängerschlüssel des eigenen Pushover-Kontos oder einer Gruppe. Ein leeres Feld behält den gespeicherten Wert. |
| Gerät | Optionaler Pushover-Gerätename. Leer sendet an alle Geräte des Empfängers. |
| Nachrichten auswählen | Ausklappbereich mit einzelnen Schaltern für Start, Modbus, Netzstatus, neue Firmware sowie beide Werte des Tagesberichts. |
| Start oder Neustart | Meldet den erfolgreichen Start, sobald WLAN, Internet und gültige Uhrzeit verfügbar sind. |
| Modbus-Ausfall und Wiederherstellung | Meldet eine anhaltend fehlende Wechselrichterantwort und die erste erfolgreiche Abfrage danach. |
| Stromnetzausfall und Netzwiederkehr | Wertet `Grid state` 13030 aus: `0xAA` gilt als Inselbetrieb/Netzausfall, `0x55` als Netzbetrieb. |
| PV-Strom des Tages | Nimmt den PV-Tagesertrag 13001 in den einmaligen Sonnenuntergangsbericht auf. |
| Ladestufe des Speichers | Nimmt den absoluten Batteriestand 10743 bei Sonnenuntergang in den Tagesbericht auf. |
| Neue Firmwareversion | Prüft höchstens einmal täglich das neueste veröffentlichte GitHub-Release und meldet eine numerisch höhere Version genau einmal. |
| Ausfall melden nach | Ein Modbusfehler oder gemeldeter Netzausfall muss zwischen 10 und 3.600 Sekunden ununterbrochen bestehen, bevor einmalig eine Ausfallmeldung eingeplant wird. Kurze Störungen lösen dadurch keine Meldung aus. |
| Wiederholungsversuch nach Sendefehler | Abstand von 1 bis 1.440 Minuten, bevor eine nicht zugestellte Nachricht erneut versucht wird. Erfolgreich gemeldete Zustandswechsel werden nicht laufend wiederholt. |
| Zugangsdaten löschen | Deaktiviert Pushover und entfernt Token, User-Key sowie Gerätename dauerhaft. |
| Testnachricht senden | Verwendet neu eingegebene Werte sofort, ohne sie zu speichern. Leere Token-/User-Felder greifen auf bereits gespeicherte Werte zurück. |

Je nach Auswahl werden gemeldet:

- Start oder Neustart des ESP32, sobald Heim-WLAN, Internet und eine per NTP gültige Uhrzeit verfügbar sind,
- ein länger als die eingestellte Verzögerung bestehender Ausfall der Wechselrichter-Modbusabfrage,
- die erste erfolgreiche Modbusabfrage nach einem bereits gemeldeten Ausfall,
- Inselbetrieb/Netzausfall und die Rückkehr in den Netzbetrieb über Sungrow `Grid state`,
- einmal nach dem von Open-Meteo gelieferten Sonnenuntergang ein gemeinsamer Tagesbericht mit den ausgewählten Werten,
- eine neu veröffentlichte stabile Firmwareversion mit installierter Version, neuer Version und Link zum GitHub-Release.

Das Datum eines erfolgreich übertragenen Tagesberichts wird im dauerhaften Einstellungsspeicher abgelegt. Dadurch führt ein ESP32-Neustart am selben Abend nicht zu einem zweiten Bericht. Fehlt ein ausgewählter Messwert zum Sendezeitpunkt, nennt der Bericht ihn als „nicht verfügbar“. Unterstützt der Wechselrichter `Grid state` nicht, wird nur dieser optionale Block übersprungen; die übrige Modbusabfrage bleibt funktionsfähig.

Die Versionsprüfung verwendet den öffentlichen GitHub-Endpunkt für das neueste Release und benötigt deshalb keinen im ESP32 gespeicherten GitHub-Token. Die Versionsbestandteile werden numerisch verglichen. Die zuletzt erfolgreich gemeldete neue Version bleibt im NVS gespeichert, damit Neustarts keine Wiederholungsmeldung erzeugen. GitHub- oder Internetfehler lösen keine falsche Update-Meldung aus; die automatische Prüfung wird später wiederholt. Es findet niemals eine automatische Installation statt.

Die Verbindung zum offiziellen Endpunkt `https://api.pushover.net/1/messages.json` wird per HTTPS mit Zertifikatsprüfung aufgebaut. Die Schlüssel werden lokal im ESP32 gespeichert, aber Pushover ist ein externer Dienst. Für jede Installation sollte eine eigene Pushover-Anwendung verwendet werden. Ist nur die direkte Modbusverbindung gestört, kann die Ausfallmeldung gesendet werden. Für eine Nachricht während des nahtlosen Inselbetriebs müssen ESP32, Router und Internetzugang weiterhin versorgt werden; insbesondere Router und vorgeschaltete Netztechnik müssen dafür am Ersatzstrom hängen. Sind diese Geräte oder der Internetzugang ausgefallen, ist keine sofortige Nachricht möglich. Nach der Wiederkehr meldet der ESP32 seinen Neustart. Pushover ersetzt keine zertifizierte Alarm- oder Netzüberwachung.

### Informationsblock unten

| Zeile | Bedeutung |
|---|---|
| Access Point | Der aktuell erzeugte AP-Name samt Standard-IP. |
| WLAN-IP | Lokale Adresse im Heimnetz oder „nicht verbunden“. |
| mDNS | Bequemer lokaler Name `http://solar-prognose-monitor.local`. |

## 10. Tab „Firmware“

| Zeile/Bedienung | Bedeutung |
|---|---|
| Dateiauswahl | Erwartet die fertig kompilierte `.ino.bin`, passend zum ESP32 und zu dessen vorhandener 4-/8-MB-Partitionstabelle. |
| Notfallfreigabe | Standardmäßig aus. Erlaubt diese eine Aktualisierung auch dann, wenn Lernprofil oder Verlauf nach dem Speichern nicht vollständig aus dem Flash zurückgelesen und bestätigt werden können. Dabei können diese Daten verloren gehen. |
| Firmware hochladen | Fragt noch einmal nach, beendet eine aktive Tarif-Netzladung sicher, übernimmt den laufenden Lernblock, speichert Lernprofil und Verlauf und prüft beide Sicherungen durch Rücklesen. Nur bei erfolgreicher Prüfung – oder bewusst aktivierter Notfallfreigabe – werden die Überschuss-Ausgänge ausgeschaltet und die Firmware installiert. Kann die Netzladung nicht sicher gestoppt und der EMS-Modus nicht wiederhergestellt werden, wird das Update unabhängig von der Notfallfreigabe abgebrochen. |
| Fortschrittsbalken | Zeigt den übertragenen Anteil. |
| Ergebnis | Meldet Erfolg oder Upload-/Schreibfehler. Nach Erfolg startet der ESP32 neu. |
| Hinweis zur Partitionierung | Erklärt die einmalig erforderliche USB-Installation der Partitionstabelle sowie die automatische Datenmigration bei deaktiviertem vollständigem Löschen. |

ArduinoOTA ist nicht enthalten. Das Browserupdate bleibt nach der einmaligen USB-Erstinstallation der vorgesehene Netzwerk-Updateweg. Version 1.0.0 darf nicht zur Erstinstallation auf einem frischen ESP32 verwendet werden: Ihre Partitionstabelle erwartete die App bei `0x20000`, der ESP32 Arduino Core 3.3.8 schrieb sie jedoch bei `0x10000`. Ab Version 1.0.1 stimmen Upload- und Partitionsadresse überein. Ein bereits laufendes Gerät wird mit der passenden normalen `Update.bin` über den Browser aktualisiert. Ein Update ersetzt das Programm, nicht die normal gespeicherten Einstellungen, Lastprofildaten und Verlaufsdateien. Vor jedem Update werden Lernprofil und Verlauf geprüft. Schlägt das fehl, bricht das Update im Normalfall ab. Die Notfallfreigabe verhindert einen Wartungs-Lockout, kann aber den Verlust der nicht bestätigten Daten nicht verhindern. 4- und 8-MB-Firmware sowie unterschiedliche Partitionsschemata dürfen nicht vertauscht werden.

## 11. Tab „About“

| Zeile/Bedienung | Bedeutung |
|---|---|
| Produkt | Name der Anwendung. |
| Firmware-Version | Tatsächlich installierte Versionsnummer; wichtig zur Kontrolle nach einem Update. |
| Hersteller | Projekt-/Herstellerangabe. |
| Lizenz | PolyForm Noncommercial License 1.0.0 (`PolyForm-Noncommercial-1.0.0`): private und sonstige erlaubte nichtkommerzielle Nutzung; kommerzielle Nutzung nur nach separater schriftlicher Genehmigung. |
| Chip | Automatisch erkanntes ESP32-Chipmodell und Anzahl der Kerne. |
| Flash-Speicher | Vom ESP32 erkannte physische Flashgröße. |
| Aktiver OTA-Slot | Name und Größe der momentan gestarteten App-Partition. |
| System-NVS | Tatsächlich erkannte Größe des dauerhaften Einstellungsspeichers. |
| Profil-NVS | Größe der separaten Partition für das gelernte Lastprofil. |
| Verlaufsspeicher | Größe und aktuelle Belegung der LittleFS-Partition. |
| Quellcode / Projekt | Link zum vollständigen Quellcode im GitHub-Repository. |
| Firmware-Version prüfen | Zeigt installierte und zuletzt auf GitHub gefundene Version, Zeitpunkt und Ergebnis der letzten erfolgreichen Prüfung. |
| Jetzt prüfen | Ruft das neueste öffentliche GitHub-Release sofort per HTTPS ab. Die Schaltfläche installiert keine Firmware. |
| Lizenz und Haftung | Erklärt die nichtkommerzielle PolyForm-Lizenz, die gesonderte kommerzielle Lizenzierung, fehlende Gewährleistung, Nutzung auf eigene Gefahr und die Unabhängigkeit von Sungrow. |
| Programmmeldungen im RAM puffern | Aktiviert Web-Debug. Nur danach erzeugte Meldungen werden zusätzlich im RAM gesammelt. |
| Puffer leeren | Entfernt den aktuellen Debugtext, ändert aber keine Einstellungen. |
| Debugfeld | Aktualisiert sich alle zwei Sekunden und zeigt maximal 12.000 Zeichen. Sehr frühe Boot-ROM- und Absturzmeldungen vor Programmstart können nicht erfasst werden. |

Web-Debug kostet nur bei Aktivierung zusätzlichen RAM für den Textpuffer. Die Einstellung dient der Diagnose und ist für den Normalbetrieb nicht erforderlich.

## 12. Berechnungen und interne Abläufe

### 12.1 Skalierung wichtiger Modbuswerte

```text
Absoluter Batteriestand in % = Rohwert Adresse 10743 × 0,1
Max-SOC in %               = Rohwert Adresse 13057 × 0,1
zu schreibender Rohwert    = gewünschter Max-SOC in % × 10
```

Der Max-SOC wird auf Unit-ID 1 per Holding-Register geschrieben und sofort mit einem Holding-Read kontrolliert. Die Software schreibt nur 50 bis 100 %. Sie begrenzt die Freigabe zusätzlich auf mindestens den aktuellen direkten SOC sowie auf den zurückgelesenen Min-SOC.

### 12.2 Open-Meteo und PV-Energie

Für jede Open-Meteo-Stunde und jede aktivierte Dachfläche wird zunächst die PV-Leistung bestimmt. Anschließend wird die Stunde in vier Viertelstunden zerlegt:

```text
PV-Leistung Fläche [kW]
  = installierte Leistung [kWp]
    × Global Tilted Irradiance [W/m²] / 1000
    × PV-Systemwirkungsgrad / 100

PV-Leistung gesamt [kW]
  = Summe aller aktivierten Dachflächen

Leistungsbilanz [kW]
  = PV-Leistung gesamt − erwartete Hauslast

bei positiver Leistungsbilanz:
Ladeenergie der Viertelstunde [kWh]
  = Leistungsbilanz [kW] × 0,25 h
    × Batterie-Ladewirkungsgrad / 100
    × Prognose-Sicherheitsfaktor / 100

bei negativer Leistungsbilanz:
Entladeenergie der Viertelstunde [kWh]
  = Betrag der Leistungsbilanz [kW] × 0,25 h
    ÷ (Batterie-Entladewirkungsgrad / 100)
    ÷ (Prognose-Sicherheitsfaktor / 100)

Netto-Batterieenergie
  = Ladeenergie − Entladeenergie
```

Für `:00`, `:15`, `:30` und `:45` wird jeweils der dazugehörige Lastprofilwert eingesetzt. Die Energie der vollständigen Wetterstunde ist die Summe dieser vier Ergebnisse. Übersteigt die gelernte Last die PV-Leistung, wird die resultierende Versorgungslücke als erwartete Batterieentladung geführt. Der rückwärts berechnete Mindest-SOC kann dadurch bereits vor einem regelmäßig auftretenden Großverbraucher steigen. Dabei wird konservativ angenommen, dass die Batterie die Lücke versorgt; ob real Batterie oder Netz einspringt, bestimmen Min-SOC, Betriebsart und Wechselrichtereinstellungen. Der betrachtete Zeitraum reicht vom Abrufzeitpunkt der aktuellen Prognose bis `Sonnenuntergang − Fertig-vor-Sonnenuntergang`. Angebrochene erste, letzte und laufende Viertelstunden werden nur mit ihrem tatsächlichen Zeitanteil berücksichtigt. Die SOC-Regelung bewertet den Plan an den Viertelstundengrenzen neu und prüft das konfigurierte Ladeende zusätzlich exakt. Schlägt eine Modbusabfrage fehl, wird bereits nach fünf Minuten erneut versucht.

### 12.3 SOC-Fahrplan

Beim Erstellen der Prognose wird der direkte Batterie-SOC als Start-SOC festgehalten.

```text
Planfortschritt
  = bisher angefallene Batterieenergie vor Sicherheitsabschlag
    / (gesamte erwartete Batterieenergie vor Sicherheitsabschlag
       × Prognose-Sicherheitsfaktor)

Plan-SOC
  = Start-SOC + (Max-SOC − Start-SOC) × begrenzter Planfortschritt

Mindest-SOC zum Erreichen des Max-SOC
  = Max-SOC
    − restliche Netto-Batterieenergie [kWh]
      / Batteriekapazität [kWh] × 100

vorläufiger Soll-SOC bei Ideal laden
  = max(Plan-SOC, Mindest-SOC zum Erreichen des Max-SOC)
```

Der Mindest-SOC zum Erreichen des Max-SOC wird auf 0 % bis Max-SOC begrenzt. Ein rechnerisch negativer Rohwert zeigt nur an, dass die verbleibende prognostizierte Energie selbst bei leerem Akku genügen würde; ausgegeben wird dann 0 %.

Bei **Vorausladen** kommt eine dritte Untergrenze hinzu. Vom Sonnenaufgang bis zur Mitte des nutzbaren PV-Zeitraums steigt sie linear von 50 % auf mindestens 80 %. Von dort bis zum geplanten Ladeende steigt sie linear von 80 % auf den eingestellten Max-SOC. Liegt der Max-SOC unter 80 %, wird bereits dieser Wert bis zur Mitte freigegeben und anschließend gehalten.

```text
Mitte = Sonnenaufgang + (Ladeende − Sonnenaufgang) / 2

vorläufiger Soll-SOC bei Vorausladen
  = max(Plan-SOC,
        Mindest-SOC zum Erreichen des Max-SOC,
        zeitabhängiger Vorauslade-SOC)
```

Die übrige Prognose bleibt vollständig aktiv. Erkennt sie wegen schlechterem Wetter oder einer wiederkehrenden Last einen noch höheren erforderlichen SOC, hat dieser höhere Wert Vorrang. Die Strategie gibt nur eine SOC-Obergrenze frei; sie kann weder eine nicht vorhandene PV-Leistung ersetzen noch eine feste Ladeleistung erzwingen.

Der vorläufige Fahrplanwert wird danach:

1. auf den Bereich von 50 % bis zum eingestellten Max-SOC begrenzt,
2. ausgehend von 50 % auf die konfigurierte SOC-Schrittweite aufgerundet,
3. erst danach mindestens auf den aktuellen direkten SOC angehoben, ohne den Istwert erneut auf die nächste Stufe aufzurunden,
4. zusätzlich auf mindestens den zurückgelesenen Min-SOC begrenzt,
5. nur unter Beachtung von Mindestabstand und der automatisch ausreichenden Tagesgrenze geschrieben.

Damit bestimmt ausschließlich der zeitliche Prognosefahrplan den nächsten Ladeschritt. Erreicht die Batterie die aktuell freigegebene Grenze, bleibt diese bestehen und es entsteht eine Ladepause. Erst wenn der Prognosefahrplan die nächste Rasterstufe erreicht, wird weiter freigegeben. Ein leichtes Überschwingen des realen SOC kann deshalb keine Kette weiterer 3-%-Freigaben mehr auslösen.

Das Raster ist kein Zwang, jede Stufe tatsächlich nacheinander zu schreiben. Hat der Fahrplan beispielsweise inzwischen 69 % erreicht, während wegen Bewölkung weder der Akku noch der zuletzt geschriebene Wert die frühere 66-%-Stufe erreicht haben, wird direkt 69 % freigegeben. Die Batterie erhält dadurch den nötigen Spielraum zum Aufholen. Der tatsächliche SOC bestimmt nicht, wann die nächste Stufe fällig wird.

Innerhalb einer freigegebenen Stufe begrenzt diese Funktion nicht die Ladeleistung. Der Wechselrichter kann daher mit der aktuell verfügbaren beziehungsweise anderweitig eingestellten Leistung bis zur SOC-Grenze laden und pausiert anschließend. Die Software erzeugt somit zeitlich verteilte Ladeblöcke; eine vollkommen gleichmäßige, niedrige Dauerladeleistung wäre nur über eine zusätzliche Ladeleistungsregelung möglich.

Die wirksame Tagesgrenze ist mindestens:

```text
notwendige Regelwrites
  = aufrunden((Max-SOC − 50 %) / SOC-Schrittweite)

wirksame Tagesgrenze
  = max(eingestellte Schreibobergrenze, notwendige Regelwrites)
```

Bei 100 % Max-SOC und 3 % Schrittweite sind somit mindestens 17 normale Regelwrites möglich. Die abschließende Freigabe auf exakt 100 % ist weiterhin ein separat gezählter Sonderwrite.

Werte unter 3 % bleiben für besondere Anlagenkonfigurationen auswählbar. Da der Max-SOC auch nach einem Neustart des Wechselrichters erhalten bleibt, kann nicht ausgeschlossen werden, dass sehr häufige Änderungen dessen nichtflüchtigen Speicher stärker belasten. Der interne Speichertyp und seine garantierte Schreibfestigkeit sind nicht dokumentiert; kleinere Schrittweiten werden deshalb nur auf eigene Verantwortung empfohlen.

Zum geplanten Ladeende wird der konfigurierte Max-SOC als Sonderfreigabe geschrieben. Fehlen direkte SOC- oder frische Wetterdaten, wird der Max-SOC einmalig sicher freigegeben. Diese Sonderfälle werden getrennt von normalen Regelwrites gezählt.

### 12.4 Lernendes 15-Minuten-Lastprofil

Der Tag ist in 96 Viertelstunden unterteilt. Pro Wochentag werden Grundlast, Ereignisleistung und Ereigniswahrscheinlichkeit gespeichert. Eigene aktive Überschusslasten werden vor dem Lernen abgezogen. Diese 15-Minuten-Auflösung wird nicht nur angezeigt und gespeichert, sondern ab Version 1.2.0 vollständig in der Energie- und SOC-Prognose verwendet.

Vereinfacht wird ein neuer Messwert mit einer exponentiellen Gewichtung eingemischt:

```text
neuer Profilwert
  = alter Profilwert × (1 − Lernrate)
    + neue Beobachtung × Lernrate

erwartete Last
  = gelernte Grundlast
    + gelernte Ereignisleistung × Ereigniswahrscheinlichkeit / 100
```

Liegt die Beobachtung deutlich über der Grundlast und über der eingestellten Großlastschwelle, wird der Mehrverbrauch als Ereignis gelernt. Dadurch kann etwa eine regelmäßig nachmittags stattfindende Autoladung nach mehreren passenden Tagen mit ihrer bisherigen Wahrscheinlichkeit berücksichtigt werden. Überschreitet diese erwartete Last später die prognostizierte PV-Leistung, fließt die Differenz als negative Batterieenergie in den Fahrplan ein. Spontane, zuvor nicht gelernte Verbraucher können dagegen nicht vorausgesagt werden.

### 12.5 Kumulative Überschuss-Stufen

```text
verfügbarer Überschuss
  = gemessene Netzeinspeisung
    + Summe der Leistung bereits eingeschalteter Stufen
    − gegebenenfalls für den SOC-Fahrplan reservierte Batterieleistung

Einschaltschwelle Stufe n
  = Summe der Leistungen aller aktivierten Stufen bis einschließlich n

Ausschaltschwelle
  = Einschaltschwelle − 150 W
```

Eine Stufe schaltet erst, wenn die entsprechende Bedingung zusätzlich für ihre Ein- oder Ausschaltverzögerung stabil war. Deaktivierte Nummern werden übersprungen und tragen nichts zur Summe bei.

### 12.6 Rundsteuerempfänger und Exportlimit

```text
installierte Gesamtleistung [W]
  = Summe der kWp aller aktivierten Dachflächen × 1000

Exportlimit [W]
  = installierte Gesamtleistung [W]
    × ausgewählte Prozentstufe / 100
```

Beispiel: `10 kWp × 1000 × 60 / 100 = 6000 W`. Der Rohwert `6000` wird direkt an Adresse 13073 geschrieben. Das Register ist 16 Bit breit; deshalb darf die aktive installierte Gesamtleistung 65.535 W nicht überschreiten. Anschließend wird Adresse 13086 auf `0xAA` gesetzt. Beide Werte werden zurückgelesen.

Im 3-Kontakt-Modus gilt:

```text
kein Eingang aktiv → 100 %
ein oder mehrere Eingänge aktiv → kleinster zugeordneter Prozentwert
```

Im 4-Kontakt-Modus gilt:

```text
100 % → eigener aktivierter und auf 100 % eingestellter Eingang
ein oder mehrere Eingänge aktiv → kleinster zugeordneter Prozentwert
kein Eingang aktiv → kein neuer Schreibzugriff; letzter Wechselrichterwert bleibt bestehen
```

Jede neue Kontaktlage muss zwei Sekunden stabil bleiben. Dadurch erzeugen kurzzeitige Überschneidungen beim mechanischen Umschalten keine unkontrollierten Schreibfolgen; falls Kontakte tatsächlich gleichzeitig aktiv bleiben, gewinnt aus Sicherheitsgründen die strengste Begrenzung.

### 12.7 Anlagenverlauf

Leistungen werden im gespeicherten Ringpuffer platzsparend in 10-W-Schritten abgelegt, der SOC in 0,1-%-Schritten. Für die Anzeige werden sie wieder zurückgerechnet:

```text
Leistung [W] = gespeicherter Wert × 10
SOC [%]      = gespeicherter Wert × 0,1
```

Beim Netzanschlusspunkt wird ein positiver Wert der Einspeisung und der Betrag eines negativen Werts dem Netzbezug zugeordnet. Der jeweils andere Anteil wird als null dargestellt.

### 12.8 Dynamischer Stromtarif

Ausgehend vom aktuellen absoluten SOC wird jede prognostizierte Viertelstunde bis zum nächsten geplanten Ladeende verrechnet:

```text
erwartete gespeicherte Energie am nächsten Ladeende
  = aktuelle gespeicherte Energie
    + Summe erwarteter PV-Ladung
    − Summe erwarteter Lastentladung

prognostizierter End-SOC
  = erwartete gespeicherte Energie / Batteriekapazität × 100

fehlende gespeicherte Energie
  = Batteriekapazität × (eingestellter Max-SOC − End-SOC) / 100

benötigte Netzenergie
  = (fehlende gespeicherte Energie + Reserve) / Ladewirkungsgrad
```

Die Energie wird auf den noch freien Bereich bis zum maximalen Netzlade-SOC begrenzt. Geeignete Intervalle müssen vor dem nächsten Sonnenaufgang liegen, die Preisobergrenze einhalten und nach Wirkungsgrad mindestens die geforderte Ersparnis gegenüber dem Vergleichspreis liefern. Danach werden die günstigsten Intervalle gewählt; bei gleichem Preis wird das spätere bevorzugt.

Für die Dauer gilt ausschließlich die vom Wechselrichter gelesene, zuvor fachgerecht konfigurierte Zwangsladeleistung:

```text
benötigte Zeit [h] = benötigte Netzenergie [kWh] / gelesene Zwangsladeleistung [kW]
```

Verwendet werden nullbasiert Holding 13049 (EMS-Modus), 13050 (Laden/Stop), 13051 (Zwangsladeleistung, nur lesen), 13057 (SOC-Obergrenze) und 33046 (maximale Ladeleistung, nur lesen; Rohwert × 10 W). Zwangsladeleistung und maximale Ladeleistung werden niemals geschrieben. Ist die Vorgabe null, fehlt sie oder liegt sie oberhalb der gelesenen Maximalleistung, bleibt die Automatik gesperrt.
