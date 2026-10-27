# Prometheus- und Grafana-Anbindung

Stand: Firmware **1.7.0**

Der Solar Prognose Monitor stellt aktuelle Anlagen- und Gerätemesswerte unter
folgendem HTTP-Endpunkt im Prometheus-Textformat bereit:

```text
http://ESP-IP/metrics
```

Der Endpunkt ist ausschließlich lesend. Er nimmt keine Steuerbefehle,
Einstellungen oder Modbus-Schreibaufträge entgegen. Er ist für ein
vertrauenswürdiges lokales Netzwerk oder ein VPN vorgesehen und besitzt keine
eigene Benutzeranmeldung oder TLS-Verschlüsselung.

## Datenweg

```text
ESP32 /metrics → Prometheus → Grafana
```

Prometheus ruft den ESP32 regelmäßig ab und speichert die Zeitreihen. Grafana
wird danach mit Prometheus als Datenquelle verbunden. Grafana sollte nicht
direkt auf `/metrics` zugreifen.

## Prometheus konfigurieren

In `prometheus.yml` kann ein Ziel beispielsweise so ergänzt werden:

```yaml
scrape_configs:
  - job_name: solar_prognose_monitor
    scrape_interval: 15s
    static_configs:
      - targets:
          - 192.168.0.50
```

Prometheus verwendet standardmäßig den Pfad `/metrics`. Bei mehreren Geräten
können weitere IP-Adressen unter `targets` ergänzt werden. Dem ESP32 sollte im
Router eine feste DHCP-Zuordnung gegeben werden, damit seine Adresse stabil
bleibt.

Nach einem Neustart oder Neuladen von Prometheus lässt sich unter **Status →
Targets** prüfen, ob das Ziel den Zustand `UP` erreicht.

## Bereitgestellte Metriken

Zu den direkt benannten Metriken gehören unter anderem:

| Metrik | Bedeutung |
|---|---|
| `solar_prognose_monitor_info` | Firmwareversion und Gerätekennung |
| `solar_prognose_monitor_uptime_seconds` | Laufzeit des ESP32 |
| `solar_prognose_monitor_wifi_rssi_dbm` | WLAN-Signalstärke |
| `solar_prognose_monitor_inverter_available` | Erreichbarkeit des Wechselrichters |
| `solar_prognose_monitor_battery_available` | Erreichbarkeit der direkten Batteriedaten |
| `solar_prognose_monitor_mqtt_connected` | Zustand der MQTT-Verbindung |
| `solar_prognose_monitor_pv_power_watts` | aktuelle PV-Leistung |
| `solar_prognose_monitor_load_power_watts` | aktueller Hausverbrauch |
| `solar_prognose_monitor_grid_power_watts` | Leistung am Netzanschlusspunkt |
| `solar_prognose_monitor_battery_power_watts` | aktuelle Batterieleistung |
| `solar_prognose_monitor_battery_soc_percent` | absoluter Batteriestand |
| `solar_prognose_monitor_battery_soh_percent` | Batterie-SOH |
| `solar_prognose_monitor_grid_frequency_hertz` | Netzfrequenz |

Zusätzlich werden alle Register über eine gemeinsame Metrik ausgegeben:

```text
solar_prognose_monitor_modbus_register_value{
  source="inverter",
  address="5016",
  name="PV-Leistung aktuell",
  unit="W"
}
```

`solar_prognose_monitor_modbus_register_valid` zeigt für jedes konfigurierte
Register mit `1` oder `0`, ob ein gültiger Wert vorliegt.
`solar_prognose_monitor_modbus_register_age_seconds` nennt das Alter des letzten
gültigen Werts. Ungültige Register erhalten keinen Zahlenwert, damit alte oder
nicht unterstützte Daten nicht als aktuelle Messung erscheinen.

## Grafana verbinden

In Grafana wird unter **Connections → Data sources** eine
**Prometheus**-Datenquelle angelegt. Als URL wird die Adresse des
Prometheus-Servers eingetragen, beispielsweise:

```text
http://192.168.0.20:9090
```

Danach können Panels beispielsweise diese Abfragen verwenden:

```promql
solar_prognose_monitor_pv_power_watts
```

```promql
solar_prognose_monitor_battery_soc_percent
```

Bei mehreren ESP32-Geräten kann über das Label `device_id` der
`solar_prognose_monitor_info`-Metrik kontrolliert werden, welches Gerät aktiv
ist. Für die Messwertreihen empfiehlt sich pro Gerät ein eigenes Prometheus-Ziel
beziehungsweise ein zusätzliches Ziel-Label in `prometheus.yml`.

## Sicherheit und Belastung

- `/metrics` ist nur lesend und verändert weder Wechselrichter noch Batterie.
- Der Endpunkt sollte nicht direkt aus dem Internet erreichbar sein.
- Ein Abrufintervall von 15 Sekunden ist für die übliche Modbus-Abfrage
  ausreichend. Sehr kurze Intervalle erzeugen unnötige Netzwerk- und
  Speicherlast, liefern aber keine schnelleren Modbuswerte.
- Ein Ausfall von Prometheus oder Grafana beeinflusst Modbusabfrage,
  Batteriespeicher-Steuerung, Rundsteuerung und Überschuss-Stufen nicht.
