# Versionsverlauf

## Unveröffentlicht

## 1.2.0 – 2026-09-21

- ergänzt unter Einstellungen die Ladestrategien **Ideal laden** und **Vorausladen**
- lässt **Ideal laden** unverändert energie- und lastabhängig arbeiten; **Vorausladen** gibt bis zur zeitlichen Mitte des nutzbaren PV-Fensters mindestens 80 % und anschließend den Rest bis zum geplanten Ladeende frei
- kombiniert den vorsichtigeren Vorauslade-Zeitplan mit den bestehenden Prognose-, Lastprofil- und Erreichbarkeitsberechnungen, wobei stets die höhere sichere SOC-Freigabe gilt
- zerlegt jede stündliche Open-Meteo-Prognose intern in vier Viertelstunden
- verrechnet in jeder Viertelstunde den tatsächlich zu diesem Zeitpunkt gelernten Lastprofilwert statt nur den Wert am Stundenanfang
- berücksichtigt dadurch Lastsprünge um `:15`, `:30` und `:45` vollständig in PV-, Last-, Batterieenergie- und SOC-Fahrplan
- behandelt wiederkehrende Lasten oberhalb der erwarteten PV-Leistung als negative Batterieenergie und hebt den rückwärts gerechneten Mindest-SOC bei Bedarf bereits vor dem Verbraucher an
- ergänzt einen einstellbaren Batterie-Entladewirkungsgrad mit 95 % als abwärtskompatiblem Standardwert
- zeigt erwartete Ladung, erwartete Entladung und die Netto-Batterieenergie getrennt sowie negative Viertelstundenwerte in Diagramm und Tabelle
- zeigt Prognosekurven und Wertetabelle künftig in 15-Minuten-Auflösung
- richtet die SOC-Regelprüfung an den Viertelstundengrenzen aus und prüft zusätzlich exakt zum konfigurierten Ladeende; Modbusfehler werden weiterhin nach fünf Minuten erneut versucht

## 1.1.2 – 2026-09-20

- ergänzt als abschließende Pushover-Funktion eine auswählbare Meldung über neue Firmware-Releases
- prüft höchstens einmal täglich das neueste öffentliche GitHub-Release ohne im ESP32 gespeicherten GitHub-Token
- vergleicht Major-, Minor- und Patchnummer numerisch und meldet jede neue Version dank dauerhaft gespeicherter Entdoppelung genau einmal
- zeigt im About-Tab installierte und zuletzt gefundene Version, Prüfzeitpunkt und Status und bietet dort eine manuelle Prüfung
- installiert Updates bewusst nicht automatisch; Flashgröße, Partitionsschema und Datensicherung bleiben eine kontrollierte Benutzerentscheidung

## 1.1.1 – 2026-09-20

- macht Start-, Modbus- und Netzstatusmeldungen sowie die beiden Inhalte des täglichen Pushover-Berichts einzeln auswählbar
- liest Sungrow `Grid state` über das optionale Herstellerregister 13030 (nullbasiert Adresse 13029) und meldet `0xAA` als Inselbetrieb/Netzausfall sowie `0x55` als Netzwiederkehr
- versendet nach Sonnenuntergang auf Wunsch einen gemeinsamen Tagesbericht mit PV-Tagesertrag und absolutem Batteriestand
- speichert das Datum eines erfolgreich versendeten Tagesberichts dauerhaft und verhindert dadurch doppelte Berichte nach einem Neustart
- setzt die Standard-SOC-Schrittweite für neue Konfigurationen auf 10 %, um die Zahl der Wechselrichter-Schreibvorgänge zu reduzieren; gespeicherte Werte bleiben unverändert
- dokumentiert, dass eine Netzausfallmeldung im Inselbetrieb nur bei weiter versorgtem ESP32, Router und Internetzugang sofort versendet werden kann

## 1.1.0 – 2026-09-20

- ergänzt einen deutlich sichtbaren Hinweis, vor Wartungs-, Service- und Inbetriebnahmearbeiten auf Bypass umzuschalten; Batterieerweiterungen und deren automatische Lade-/Entladeprozedur werden als besonders wichtiges Beispiel genannt
- ergänzt optionale Pushover-Benachrichtigungen bei Start/Neustart, verzögert erkanntem Modbus-Ausfall und Wiederherstellung
- speichert Pushover-Zugangsdaten verdeckt im ESP32, bietet eine Testnachricht und wiederholt fehlgeschlagene Zustellungen mit einstellbarem Abstand
- prüft die TLS-Verbindung zu Pushover gegen den eingebetteten DigiCert-Vertrauensanker
- stellt klar, dass die aktuelle Firmware für das klassische ESP32 DevKit/WROOM ausgelegt ist und weitere Varianten wie der ESP32-C3 noch getestet und angepasst werden

## 1.0.1 – 2026-09-18

- korrigiert die USB-Erstinstallation auf einem frischen ESP32: Die App-Partition beginnt nun passend zum ESP32 Arduino Core 3.3.8 bei `0x10000`
- verschiebt die separate Profil-NVS hinter die beiden OTA-Slots, ohne deren Größe oder den Verlaufsspeicher zu verkleinern
- ergänzt klare Installationshinweise für Neuinstallationen und bereits laufende Geräte
- Version 1.0.0 kann auf einem frischen ESP32 nicht starten, weil ihre Partitionstabelle die App bei `0x20000` erwartete, während der Core sie bei `0x10000` schrieb; für Neuinstallationen muss Version 1.0.1 oder neuer verwendet werden

## 1.0.0 – 2026-09-17

Erste Veröffentlichung des **Solar Prognose Monitors**.

- serverlose Überwachung und prognosebasierte Batteriespeicher-Steuerung auf einem ESP32
- erste Geräteintegration für Sungrow über Modbus TCP und Modbus RTU/RS485
- direkter Batterie-SOC über TCP-Unit-ID 2 oder RS485-Unit-ID 200
- Open-Meteo-Prognose für bis zu vier unterschiedlich ausgerichtete PV-Flächen
- lokal erlerntes 15-Minuten-Lastprofil je Wochentag
- zeitlich verteilter SOC-Fahrplan mit Sicherheitsreserve, Aufholverhalten und geprüftem Rücklesen
- fünf frei konfigurierbare kumulative Überschuss-Stufen über GPIO oder HTTP-API
- Unterstützung von Rundsteuerempfängern mit drei oder vier Kontakten
- lokale Weboberfläche für Wechselrichter, Batterie, Prognose, Lastprofil, Verlauf, Einstellungen, Firmware und About
- dauerhaft gespeicherter 31-Tage-Anlagenverlauf mit Tages-, Wochen- und Monatsansicht
- Browser-Firmwareupdate mit verifizierter Sicherung von Lernprofil und Verlauf
- eigene Partitionstabellen und Release-Dateien für ESP32-Module mit 4 MB oder 8 MB Flash
- source-available unter der PolyForm Noncommercial License 1.0.0
