# Solar Prognose Monitor

Der Solar Prognose Monitor ist eine serverlose Steuerung und Überwachung für Photovoltaikanlagen mit Batteriespeicher. Die erste Geräteintegration unterstützt Sungrow-Wechselrichter über Modbus TCP und/oder RS485. Ein ESP32 zeigt die Anlagendaten in einer lokalen Weboberfläche an und plant das Laden des Batteriespeichers anhand der Wetterprognose und des tatsächlichen Verbrauchs. Die herstellerneutrale Projektbezeichnung lässt Raum für weitere Geräteintegrationen in späteren Versionen.

Ohne eine solche Planung kann ein großer PV-Überschuss den Speicher bereits früh am Morgen vollständig laden. Anschließend bleibt er möglicherweise viele Stunden bei einem sehr hohen Ladezustand, obwohl die Energie erst am Abend benötigt wird. Ein dauerhaft sehr hoher Ladezustand kann die Alterung von Lithium-Batterien beschleunigen; ein unnötig leerer Speicher, fehlende Energiereserve oder eine tiefe Entladung sind ebenfalls nicht erstrebenswert. Ziel dieser Software ist deshalb nicht, den Akku möglichst früh auf 100 Prozent zu bringen, sondern ihn über den verfügbaren PV-Zeitraum kontrolliert zu laden und den gewünschten Ladezustand möglichst erst in der Nähe des geplanten Ladeendes zu erreichen.

Dafür kombiniert die Software mehrere Informationen:

- die stündliche Open-Meteo-Prognose für bis zu vier unterschiedlich ausgerichtete Dachflächen,
- installierte PV-Leistung, Speichergröße und einstellbare Wirkungsgrade,
- den aktuellen absoluten Batteriestand,
- die noch erwartete PV-Energie bis zum geplanten Ladeende,
- sowie ein lokal erlerntes Verbrauchsprofil.

Der Hausverbrauch wird für jeden Wochentag in 15-Minuten-Blöcken gelernt. Wiederkehrende Lasten – beispielsweise ein regelmäßig am Nachmittag geladenes Elektroauto – fließen dadurch zunehmend in die Planung ein. Berechnung, Lernprofil und Messverlauf bleiben auf dem ESP32; lediglich die Wettervorhersage wird von Open-Meteo abgerufen. Ein eigener Cloud- oder Herstellerserver ist nicht erforderlich.

Die Steuerung gibt den zulässigen Ladezielwert schrittweise frei. Bei einer unerwarteten Wolkenphase wartet sie nicht auf eine verpasste Zwischenstufe, sondern wechselt auf den zur aktuellen Uhrzeit vorgesehenen Wert. Zusätzlich prüft sie, welcher Batteriestand mindestens freigegeben werden muss, damit das Tagesziel mit der noch erwarteten Energie erreichbar bleibt. Sicherheitsreserve, gewünschtes Ladeende und Schrittweite lassen sich einstellen. Da Wetter- und Verbrauchsprognosen nie vollkommen exakt sind, ersetzt das System keine Anlagenüberwachung und sollte bei der ersten Inbetriebnahme kontrolliert werden.

Aktuelle Version: **1.1.1**

**Hardwarestand:** Die aktuelle Firmware ist für ein klassisches **ESP32 DevKit mit ESP32-WROOM-Modul** ausgelegt. Weitere ESP32-Varianten wie der **ESP32-C3** werden derzeit getestet; Pinbelegung, Partitionierung und Programmcode werden dafür schrittweise angepasst. Bis eine Variante ausdrücklich als unterstützt gekennzeichnet ist, sollte dafür nicht ungeprüft die DevKit-Firmware verwendet werden.

## Funktionen

- Modbus TCP, Modbus RTU/RS485 oder beide Transportwege
- absoluter Batteriestand über TCP-ID 2 oder RS485-ID 200; kein relativer Wechselrichter-SOC als Regelungsersatz
- getrennte Webansichten für Wechselrichter und Batterie
- eigener WLAN-Access-Point mit dauerhaft gespeicherten WLAN-Einstellungen
- Open-Meteo-Prognose für bis zu vier unterschiedlich ausgerichtete Dachflächen
- lokales, je Wochentag erlerntes 15-Minuten-Lastprofil mit stündlicher und updatefester Speicherung
- fünf kumulative Überschuss-Stufen über GPIO oder HTTP-API
- wahlweise drei oder vier frei belegbare Eingänge für einen Rundsteuerempfänger mit automatischer Einspeisebegrenzung
- 3-Kontakt-Modus mit 100 % bei ruhenden Eingängen oder 4-Kontakt-Modus mit eigenem 100-%-Signal
- aktiver HIGH- oder LOW-Pegel für jeden Rundsteuer-Eingang separat auswählbar
- Browser-Firmwareupdate mit verifizierter Sicherung von Lernprofil und Verlauf; bewusst bestätigbare Notfallfreigabe bei Speicherproblemen
- About-Seite mit Firmware-Version, Hersteller, Lizenzstatus und Flashgröße; Web-Debug ist dort integriert
- einzeln auswählbare Pushover-Meldungen bei ESP32-Start, Modbus-Ausfall, Netzausfall/Inselbetrieb und Wiederherstellung sowie ein täglicher Sonnenuntergangsbericht
- kompatibel mit ESP32 Arduino Core 3.3.8
- eigene Partitionstabellen für 4 MB und 8 MB: bestehende 20-KB-System-NVS, zusätzliche 64-KB-Profil-NVS, zwei große OTA-Slots und eine separate LittleFS-Verlaufspartition
- optionale Smart-Meter-Zählerstände 5746 und 5748: nicht unterstützte Register beeinträchtigen die übrige Modbus-Abfrage nicht
- Datum und Uhrzeit an allen Verlaufsgrafiken sowie Jetzt-/Zielmarken im Ladefahrplan
- Tab **Verlauf** mit dauerhaftem 31-Tage-Speicher: Tagesansicht in 5-, Wochenansicht in 15- und Monatsansicht in 60-Minuten-Auflösung

## Batteriespeicher-Steuerung

Es gibt genau einen konfigurierbaren Ladezielwert im Bereich von 50 bis 100 Prozent. Im Wechselrichter wird dieser technisch über den sogenannten Max-SOC vorgegeben.

- **Prognose:** Der ESP32 verteilt die Ladefreigaben vorwärts über den verbleibenden PV-Zeitraum. Stündliche Wetterwerte werden innerhalb der Stunde zeitanteilig geglättet. Das Erreichen einer freigegebenen Stufe löst nicht sofort die nächste Stufe aus. Ist eine Stufe wegen Bewölkung noch nicht erreicht, gilt trotzdem der zur aktuellen Uhrzeit vorgesehene höhere Fahrplanwert. Dadurch kann die Batterie später wieder aufholen.
- **Bypass:** Der eingestellte Ladezielwert wird einmal geschrieben und durch Rücklesen geprüft. Danach greift der ESP32 nicht mehr regelmäßig ein. Änderungen über die Sungrow-App bleiben somit möglich. Ein neuer Schreibauftrag entsteht erst beim erneuten Wechsel auf Bypass oder nach einer Änderung des Ladezielwerts.

**Wichtig bei Wartung und Inbetriebnahme:** Vor Wartungs-, Service-, Umbau- oder Inbetriebnahmearbeiten an Wechselrichter oder Batteriesystem muss die Batteriespeicher-Steuerung unter **Einstellungen** auf **Bypass** gestellt und gespeichert werden. Dadurch erfolgen nach dem einmaligen, geprüften Bypass-Auftrag keine regelmäßigen Max-SOC-Eingriffe mehr. Besonders wichtig ist dies bei einer Batterieerweiterung: Sungrow kann den Speicher während der Angleichung neuer Batteriemodule automatisch bis ungefähr 40 % laden oder entladen. Im Prognosebetrieb könnten wiederholte Max-SOC-Freigaben diesen Ablauf beeinflussen. Erst nach vollständig abgeschlossenen Arbeiten und Freigabe durch den Fachbetrieb darf wieder auf Prognose umgestellt werden. Hersteller- und Fachbetriebsvorgaben haben immer Vorrang.

Die technische Umsetzung verwendet Modbus Unit-ID 1 und das Holding-Register für den Max-SOC. Jeder Schreibvorgang wird zurückgelesen und nur bei passendem Ergebnis als erfolgreich gewertet. Register, Skalierung und vollständige Berechnung sind in der [Dokumentation](docs/Webinterface-und-Prognoseberechnung.md) beschrieben.

## Rundsteuerempfänger

Die Auswertung kann an zwei Empfängerarten angepasst werden. Im **3-Kontakt-Modus** müssen genau drei Eingänge aktiv sein; liegt kein Signal an, gilt dies als 100 %. Im **4-Kontakt-Modus** müssen genau vier Eingänge aktiv sein und einer davon muss ausdrücklich 100 % melden. Ohne Signal bleibt in diesem Modus die letzte Wechselrichtereinstellung unverändert. Sind mehrere Kontakte aktiv, gilt nach zwei Sekunden Entprellzeit der niedrigste Prozentwert.

Aus der aktiven Kontaktstufe und der installierten PV-Leistung ermittelt der ESP32 automatisch die zulässige Einspeiseleistung und überträgt sie geprüft an den Wechselrichter. Die genaue Berechnung, verwendeten Register und Sicherheitslogik stehen in der [vollständigen Bedienungsanleitung](docs/Vollstaendige-Bedienungsanleitung.md).

## Pushover-Benachrichtigungen

Unter **Einstellungen** können optional ein eigener Pushover-Application/API-Token und ein User-/Group-Key hinterlegt werden. In einem Ausklappbereich sind Start/Neustart, Modbus-Ausfall mit Wiederherstellung, Netzausfall/Inselbetrieb mit Netzwiederkehr sowie die Inhalte des täglichen Sonnenuntergangsberichts einzeln auswählbar. Der Tagesbericht kann den PV-Tagesertrag aus Register 13001 und den absoluten Batteriestand aus Register 10743 enthalten. Er wird erst nach erfolgreicher Übergabe dauerhaft für das betreffende Datum als versendet markiert und deshalb nach einem Neustart nicht doppelt verschickt.

Für den Netzstatus wird das Sungrow-Herstellerregister **13030 „Grid state“** verwendet; im nullbasierten Sketch ist dies Adresse **13029**. `0x55` bedeutet Netzbetrieb, `0xAA` Inselbetrieb beziehungsweise Netzausfall. Die Abfrage ist optional: Weist ein Wechselrichter das Register zurück, laufen alle übrigen Modbuswerte weiter und lediglich diese Meldungsart steht nicht zur Verfügung. Ein Testknopf prüft die Pushover-Konfiguration vorab. Fehlgeschlagene Übertragungen werden mit einstellbarem Abstand erneut versucht; Statuswechsel erzeugen jeweils nur eine Meldung.

Die Zugangsdaten bleiben im Einstellungsspeicher des ESP32 und werden nach dem Speichern nicht mehr an den Browser zurückgegeben. Die Übertragung zu `api.pushover.net` erfolgt über HTTPS mit Zertifikatsprüfung. Pushover ist ein externer Dienst; es gelten dessen Konto-, Datenschutz- und Nutzungsvorgaben. Für jede Installation sollte eine eigene Pushover-Anwendung und damit ein eigener Application/API-Token verwendet werden.

**Voraussetzung für die Meldung im Inselbetrieb:** Eine sofortige Stromausfallmeldung ist technisch nur möglich, solange ESP32, Router und Internetzugang trotz ausgefallenem öffentlichen Netz weiter versorgt werden. Bei nahtlosem Inselbetrieb muss daher neben dem ESP32 insbesondere auch die Netzwerk- und Internetanbindung am Ersatzstrom hängen. Nach einem vollständigen Stromausfall meldet sich der ESP32 erst nach Rückkehr von Strom, WLAN, Internet und gültiger Systemzeit mit der Startmeldung. Die Funktion ersetzt keine zertifizierte Alarm- oder Netzüberwachung.

## Dateien

- Arduino-Einstieg: [`src/Solar_Prognose_Monitor/Solar_Prognose_Monitor.ino`](src/Solar_Prognose_Monitor/Solar_Prognose_Monitor.ino)
- Programmschnittstelle: [`src/Solar_Prognose_Monitor/SolarPrognoseMonitor.h`](src/Solar_Prognose_Monitor/SolarPrognoseMonitor.h)
- Hauptprogramm: [`src/Solar_Prognose_Monitor/SolarPrognoseMonitor.cpp`](src/Solar_Prognose_Monitor/SolarPrognoseMonitor.cpp)
- Vollständige Bedienungsanleitung, Tab für Tab und Feld für Feld: [`docs/Vollstaendige-Bedienungsanleitung.md`](docs/Vollstaendige-Bedienungsanleitung.md)
- Bedienung und exakte Prognoseberechnung: [`docs/Webinterface-und-Prognoseberechnung.md`](docs/Webinterface-und-Prognoseberechnung.md)
- Versionsverlauf: [`CHANGELOG.md`](CHANGELOG.md)
- Konzept: [`docs/Sungrow_Prognosebasiertes_Laden_Konzept.pdf`](docs/Sungrow_Prognosebasiertes_Laden_Konzept.pdf)
- kompilierte 4-/8-MB-Firmware: unter **Releases** auf GitHub
- reproduzierbarer lokaler Build für beide Flashgrößen: [`scripts/build-release.ps1`](scripts/build-release.ps1)

## Arduino-IDE-Einstellungen

- Board: passend zum verwendeten klassischen ESP32/WROOM-Board
- ESP32 Arduino Core: **3.3.8**
- Flash Size: passend zum Modul, **4 MB** oder **8 MB**; 8 MB wird empfohlen
- Partition Scheme: **Custom** (dadurch wird die sketch-eigene `partitions.csv` verwendet)
- Partitionierung: Die sketch-eigene `partitions.csv` enthält das 4-MB-Schema; `partitions_8MB.csv` enthält das 8-MB-Schema
- Upload Speed: zunächst 460800, bei Problemen 115200

Der Sketch verwendet ausschließlich Bibliotheken aus dem ESP32-Core.

Für einen reproduzierbaren Build beider Varianten im Projektordner ausführen:

```powershell
.\scripts\build-release.ps1 -Version 1.1.1
```

Das Skript setzt die maximal zulässige App-Größe passend zu den eigenen Partitionstabellen und erzeugt getrennte Update- sowie vollständige USB-Dateien unter `dist/v1.1.1`. Wer direkt in der Arduino IDE baut, wählt **Partition Scheme: Custom** und verwendet für 4 MB die mitgelieferte `partitions.csv`. Für 8 MB muss vor dem Kompilieren deren Inhalt durch `partitions_8MB.csv` ersetzt werden. Die Flashgröße muss immer zum real verbauten Modul passen. Die IDE zeigt beim Custom-Schema eine großzügige allgemeine Obergrenze an; maßgeblich sind dennoch 1.835.008 Byte bei 4 MB und 3.670.016 Byte bei 8 MB.

### Erstinstallation der Partitionstabelle

**Wichtig:** Version 1.0.0 ist für die Erstinstallation auf einem frischen ESP32 nicht verwendbar. Ihre Partitionstabelle erwartete das Programm bei `0x20000`, während ESP32 Arduino Core 3.3.8 es per USB bei `0x10000` schrieb. Der ESP32 konnte deshalb nicht booten und keinen Access Point starten. Dieser Fehler ist ab Version 1.0.1 behoben.

Für einen frischen ESP32 das vollständige Repository als ZIP herunterladen, entpacken und in der Arduino IDE `src/Solar_Prognose_Monitor/Solar_Prognose_Monitor.ino` öffnen. Die Dateien `.ino`, `.cpp`, `.h` und `partitions.csv` müssen gemeinsam im Sketchordner bleiben. Die erstmalige Übernahme der mitgelieferten Partitionstabelle muss per USB erfolgen; ein Browserupdate allein kann keine Partitionstabelle ersetzen. Alternativ kann die zur Flashgröße passende `USB-Komplett`-Datei verwendet werden. Sie ist nur für Neuinstallationen bestimmt und löscht vorhandene Daten.

Ein Gerät, auf dem Version 1.0.0 bereits läuft, sollte mit der passenden normalen `Update.bin` über den Browser aktualisiert werden. Dabei bleibt seine vorhandene Partitionstabelle aktiv; Einstellungen, Lernprofil und Verlauf werden nicht allein durch das Programmupdate gelöscht. Die 4-MB- und 8-MB-Varianten dürfen nicht vertauscht werden.

Nach der USB-Erstinstallation funktionieren Browserupdates innerhalb derselben 4-/8-MB-Variante normal. Die 4-MB-Partition bietet zwei OTA-Slots mit je 1,75 MB und 320 KB LittleFS für 31 Tage Verlauf. Die 8-MB-Variante bietet zwei OTA-Slots mit je 3,5 MB und 832 KB LittleFS.

## Erster Start

1. Sketch über USB aufspielen.
2. Mit dem WLAN `Solar-Prognose-XXXXXX` verbinden; Standardpasswort: `solar123`.
3. `http://192.168.4.1/settings` öffnen.
4. WLAN, Modbus-Transport und Anlagendaten konfigurieren.
5. Zunächst Bypass auswählen und den einmalig geschriebenen sowie rückgelesenen Ladezielwert kontrollieren.
6. Anschließend bei Bedarf auf Prognose umschalten.

## Sicherheit

Die Schreibzugriffe der Batteriespeicher-Steuerung werden zurückgelesen und geprüft. Sungrow-Firmwarestände und Anlagenkonfigurationen können sich dennoch unterscheiden; die erste Inbetriebnahme sollte deshalb unter Beobachtung erfolgen.

ESP32-GPIOs dürfen keine Heizpatrone oder andere Netzlast direkt schalten. Dafür sind passend dimensionierte Relais, SSRs oder Schütze sowie ein fachgerechter und abgesicherter Aufbau erforderlich.

An die Rundsteuer-Eingänge dürfen ausschließlich potentialfreie Kontakte gegen GND oder saubere 3,3-V-Logik angeschlossen werden. Netzspannung und 5 V dürfen niemals direkt an einem ESP32-GPIO anliegen. GPIO 34 bis 39 benötigen externe Pull-Widerstände.

Installation, Konfiguration und Nutzung dieser Software erfolgen vollständig auf eigene Gefahr. Die Software wird ohne jegliche ausdrückliche oder stillschweigende Gewährleistung bereitgestellt. Insbesondere wird keine Gewähr für Eignung, Fehlerfreiheit, Anlagenkompatibilität, Betriebssicherheit oder das Ausbleiben von Schäden übernommen. Arbeiten an Netzspannung und leistungsführenden Anlagenteilen dürfen nur durch entsprechend qualifizierte Fachkräfte erfolgen.

## Projekt unterstützen ☕

Wenn dir der Solar Prognose Monitor hilft und du die weitere Entwicklung freiwillig unterstützen möchtest, kannst du über PayPal einen Kaffee ausgeben:

[**☕ Über PayPal einen Kaffee ausgeben**](https://www.paypal.com/ncp/payment/MZDYMZH5EHY2Y)


Die Unterstützung ist freiwillig und begründet keinen Anspruch auf Gegenleistung, Support, zusätzliche Funktionen, Lizenzrechte oder bevorzugte Bearbeitung. Es handelt sich nicht um eine steuerlich abzugsfähige Spende; eine Zuwendungsbestätigung wird nicht ausgestellt.

## Licensing

Copyright (C) 2026 Marcus Sonntag / [MS-De-sign](https://github.com/MS-De-sign).

Die aktuelle Fassung des Solar Prognose Monitors ist **source-available** unter der [PolyForm Noncommercial License 1.0.0](LICENSE) (`PolyForm-Noncommercial-1.0.0`). Sie darf für die von dieser Lizenz erlaubten privaten und sonstigen nichtkommerziellen Zwecke kostenlos angesehen, verwendet, verändert und weitergegeben werden. Da kommerzielle Nutzung ausgeschlossen ist, handelt es sich nicht um eine Open-Source-Lizenz im Sinne der Open Source Definition.

Kommerzielle oder gewerbliche Nutzung benötigt vorab eine separate schriftliche Genehmigung oder Lizenzvereinbarung. Dazu gehören insbesondere der Verkauf von Hardware mit vorinstallierter Software, die Integration in kommerzielle Produkte und kostenpflichtige Dienstleistungen auf Grundlage der Software. Individuelle Genehmigungen – auch ohne Lizenzgebühr – bleiben möglich. Weitere Hinweise stehen unter [Commercial Licensing](COMMERCIAL-LICENSE.md).

Bei jeder Weitergabe müssen der Lizenztext und die in [`NOTICE`](NOTICE) enthaltene `Required Notice` erhalten bleiben. Die Softwarelizenz gewährt keine Rechte an Projektname, Logo, Grafiken oder anderem Branding, soweit solche Rechte nicht ausdrücklich separat eingeräumt wurden.

### Disclaimer

Dieses Projekt ist ein unabhängiges, source-available Projekt und steht in keiner Verbindung zu Sungrow Power Supply Co., Ltd. Es wird von Sungrow weder unterstützt noch gesponsert oder empfohlen.

Sungrow, Produktnamen, Logos und zugehörige Marken sind Eigentum ihrer jeweiligen Rechteinhaber. Die Software wird ohne Gewährleistung bereitgestellt. Installation und Nutzung erfolgen auf eigene Gefahr.

## Development

Dieses Projekt wurde mit Unterstützung von Werkzeugen der künstlichen Intelligenz entwickelt. KI wurde unter anderem für Codeerzeugung, Fehlersuche, Dokumentation, Optimierung und die Diskussion möglicher Umsetzungswege eingesetzt.

Projektarchitektur, Anforderungen, Tests, Integration und endgültige Implementierungsentscheidungen liegen in der Verantwortung des Projektbetreibers. KI-generierte Vorschläge wurden geprüft und angepasst, bevor sie in das Projekt übernommen wurden.
