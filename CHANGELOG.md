# Versionsverlauf

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
