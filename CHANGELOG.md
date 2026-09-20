# Versionsverlauf

## Unveröffentlicht

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
