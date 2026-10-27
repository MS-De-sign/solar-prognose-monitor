# MQTT-Datenweitergabe

Stand: Firmware **1.7.0**

Der Solar Prognose Monitor kann seine gelesenen Anlagenwerte an einen
MQTT-Broker im lokalen Netzwerk senden. Die Schnittstelle ist absichtlich
ausschließlich sendend aufgebaut: Der ESP32 führt kein MQTT-Abonnement aus und
nimmt deshalb über MQTT keine Befehle, Sollwerte oder Modbus-Schreibaufträge an.

Geeignete Empfänger sind unter anderem Home Assistant, ioBroker, Node-RED und
Telegraf. Telegraf kann die MQTT-Daten anschließend in eine InfluxDB schreiben;
Grafana greift dann wie üblich auf InfluxDB zu.

## Einrichtung

Unter **Einstellungen → Externe Datenweitergabe über MQTT** werden eingetragen:

- Broker-IP oder lokaler Hostname,
- MQTT-Port, normalerweise `1883`,
- optional Benutzername und Passwort,
- Basistopic,
- Sendeintervall zwischen 5 und 3.600 Sekunden,
- Retain für den letzten bekannten Messwert,
- optional Home-Assistant-Discovery.

Das voreingestellte Basistopic enthält die letzten sechs Stellen der eindeutigen
ESP32-Chip-ID, beispielsweise:

```text
solar-prognose-monitor/A1B2C3
```

Nach dem Speichern startet der ESP32 neu. Der Tab **Daten** zeigt Broker,
Verbindungsstatus, letzte erfolgreiche Veröffentlichung und konkrete Topics.
Mit **Messwerte jetzt senden** kann eine zusätzliche Veröffentlichung ausgelöst
werden.

## Topic-Struktur

| Topic | Inhalt |
|---|---|
| `<Basis>/availability` | `online` oder durch MQTT Last Will `offline`; retained |
| `<Basis>/state` | gemeinsames JSON mit Gerätestatus und allen aktuell gültigen Registern |
| `<Basis>/metrics/<name>` | einfache, stabile Namen wichtiger Messwerte |
| `<Basis>/register/<quelle>/<adresse>` | numerischer Wert eines einzelnen Registers |
| `<Basis>/register/<quelle>/<adresse>/meta` | Beschreibung, Einheit und Nachkommastellen |

Mögliche Quellen sind `inverter`, `battery` und `meter`. Beispiele:

```text
solar-prognose-monitor/A1B2C3/metrics/pv_power_w
solar-prognose-monitor/A1B2C3/metrics/battery_soc_percent
solar-prognose-monitor/A1B2C3/register/inverter/13009
solar-prognose-monitor/A1B2C3/register/battery/10743
```

Die vereinfachten Messwerte sind:

| Name | Bedeutung |
|---|---|
| `pv_power_w` | aktuelle PV-Leistung |
| `load_power_w` | aktuelle Haus-/Lastleistung |
| `grid_power_w` | Leistung am Netzanschlusspunkt; Vorzeichen wie im Modbuswert |
| `battery_power_w` | Batterieleistungsabgabe, wie im Webinterface beschriftet |
| `battery_soc_percent` | absoluter Batterie-SOC aus Batterie-ID 2 beziehungsweise 200 |
| `battery_soh_percent` | SOH aus den direkten Batteriedaten |
| `grid_frequency_hz` | Netzfrequenz |
| `pv_energy_today_kwh` | PV-Erzeugung des laufenden Tages |
| `pv_energy_total_kwh` | gesamte PV-Erzeugung |
| `grid_import_total_kwh` | Smart-Meter-Netzbezug gesamt |
| `grid_export_total_kwh` | Smart-Meter-Netzeinspeisung gesamt |

Fehlt ein Modbuswert oder ist das Register nicht verfügbar, wird dessen
Einzelwert in diesem Sendezyklus nicht veröffentlicht. Der bisher retained im
Broker vorhandene Wert bleibt erhalten; den aktuellen Anlagenzustand und die
Erreichbarkeit zeigt zusätzlich das gemeinsame `state`-Objekt.

Beispiel für `<Basis>/state`:

```json
{
  "version": "1.7.0",
  "device_id": "A1B2C3",
  "timestamp": 1792681200,
  "uptime_seconds": 3600,
  "wifi_rssi_dbm": -58,
  "inverter_available": true,
  "battery_available": true,
  "modbus_transport": "Modbus TCP",
  "values": {
    "inverter_5016": 4280,
    "inverter_13009": 1250,
    "battery_10743": 64.5
  }
}
```

## Home Assistant

Ist **Home-Assistant-MQTT-Discovery** aktiviert, veröffentlicht der ESP32 für
jedes der 22 gelesenen Register eine retained Sensorkonfiguration unter:

```text
homeassistant/sensor/spm_A1B2C3/<quelle_adresse>/config
```

Das Discovery-Präfix ist veränderbar. Home Assistant benötigt einen korrekt
eingerichteten MQTT-Broker und die MQTT-Integration. Die Sensoren erscheinen als
gemeinsames Gerät „Solar Prognose Monitor A1B2C3“. Strom-, Energie-, Temperatur-,
Frequenz- und Batterieeinheiten werden soweit möglich als passende Geräteklassen
gemeldet.

Beim Ausschalten der Discovery oder beim Wechsel des Präfixes merkt sich der
ESP32 die alten Discovery-Topics und entfernt deren retained Konfigurationen bei
der nächsten erfolgreichen Brokerverbindung. Wird gleichzeitig MQTT vollständig
deaktiviert, bleibt dieser Aufräumauftrag gespeichert und wird beim späteren
erneuten Aktivieren ausgeführt.

## ioBroker und Node-RED

Beide Systeme können das vollständige Basistopic abonnieren:

```text
solar-prognose-monitor/A1B2C3/#
```

Für eigene Logik sind die einfachen `metrics`-Topics meist übersichtlicher. Die
Registertopics bleiben verfügbar, wenn die exakte Sungrow-Adresse benötigt wird.
Es existiert bewusst kein Gegenstück wie `/set`, `/command` oder `/write`.

## Telegraf und InfluxDB

Eine Telegraf-Konfiguration kann beispielsweise alle einfachen Messwerte lesen:

```toml
[[inputs.mqtt_consumer]]
  servers = ["tcp://192.168.0.10:1883"]
  topics = ["solar-prognose-monitor/+/metrics/+"]
  username = "mqtt-benutzer"
  password = "mqtt-passwort"
  data_format = "value"
  data_type = "float"
```

Die konkrete Zuordnung des Topic-Namens zu InfluxDB-Tags oder Messreihen wird in
Telegraf vorgenommen. Grafana verbindet sich anschließend mit InfluxDB, nicht
direkt mit dem ESP32.

## Sicherheit und Ausfallverhalten

- Der ESP32 verwendet MQTT 3.1.1 mit QoS 0.
- Die Verbindung unterstützt Benutzername und Passwort, derzeit aber kein TLS.
- Broker-Port 1883 niemals direkt aus dem Internet freigeben. Für entfernte
  Standorte ein VPN oder einen gesicherten lokalen Tunnel einsetzen.
- Der ESP32 abonniert keine Topics und verarbeitet keine MQTT-Steuerbefehle.
- Bei unerwartetem Verbindungsabbruch veröffentlicht der Broker über Last Will
  retained `offline`.
- Nach einer Wiederverbindung werden `online`, Metadaten, Discovery und aktuelle
  Messwerte erneut gesendet.
- Ein MQTT-Ausfall verändert weder Modbusabfrage noch Batteriespeicher-Steuerung,
  Rundsteuerung, Überschuss-Stufen oder dynamische Tarifsteuerung.

