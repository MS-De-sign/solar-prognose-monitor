/*
 * Solar Prognose Monitor
 * Copyright (C) 2026 Marcus Sonntag / MS-De-sign
 *
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * Required Notice: Copyright (C) 2026 Marcus Sonntag / MS-De-sign.
 *
 * Licensed under the PolyForm Noncommercial License 1.0.0 for permitted
 * noncommercial purposes. Commercial use requires separate written permission
 * or a commercial license agreement from the project maintainer.
 *
 * This software is provided without warranty or condition.
 * See the LICENSE file for details. Installation and use are at your own risk.
 *
 * This independent project is not affiliated with, endorsed by, or sponsored
 * by Sungrow Power Supply Co., Ltd. Sungrow trademarks belong to their owners.
 */

#include "SolarPrognoseMonitor.h"
#include <WiFi.h>
#include <WiFiClient.h>
#include <NetworkClientSecure.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <LittleFS.h>
#include <ESPmDNS.h>
#include <Update.h>
#include <esp_partition.h>
#include <esp_ota_ops.h>
#include <nvs_flash.h>
#include <stdarg.h>
#include <time.h>

/*
  Solar Prognose Monitor fuer ESP32 mit 4 oder 8 MB Flash

  - Liest eine kompakte Auswahl relevanter Sungrow-Werte mit mehreren Unit-IDs.
  - Wechselrichter/Smart Meter standardmaessig ueber Unit-ID 1.
  - Direkter Batterie-SOC/SOH ueber TCP-Unit-ID 2 oder RS485-Unit-ID 200.
  - Fuenf frei konfigurierbare kumulative GPIO-/HTTP-API-Stufen.
  - Browser-Firmwareupdate und gepufferter Web-Debug, kein ArduinoOTA.
  - Modbus TCP und Modbus RTU ueber ein RS485-Modul werden direkt gesprochen.
  - Betriebsarten: nur TCP, nur RS485 oder TCP plus RS485.
  - Serverlose PV-Prognose mit Open-Meteo und bis zu vier Dachflaechen.
  - Prognosebasiertes Laden ueber Holding-Register 13057 (Max. SOC).
  - Lokales 15-Minuten-Lastprofil je Wochentag mit stuendlicher NVS-Speicherung.
  - Dauerhafter 31-Tage-Anlagenverlauf in LittleFS mit Messpunkten im 5-Minuten-Takt.
  - Drei- oder Vier-Kontakt-Rundsteuerung mit verifizierten Exportlimit-Schreibzugriffen.
  - Optionale Pushover-Meldungen fuer Start, Modbus-Ausfall und Wiederherstellung.
  - Fuer einen RS485-Transceiver sind RX, TX und ein gemeinsamer DE-/RE-Pin konfigurierbar.
  - Es ist keine zusaetzliche Modbus-Bibliothek erforderlich.
  - Der ESP32 startet immer einen eigenen Access Point.
  - WLAN- und Modbus-Einstellungen werden dauerhaft in Preferences gespeichert.
  - Messwerte und Einstellungen sind ueber eine einfache Weboberflaeche erreichbar.

  Getestetes Ziel: ESP32 Arduino Core 3.x
  Standardwerte: Modbus TCP Port 502, Unit-ID 1, Input Register (Funktion 0x04)
*/

namespace ConfigDefaults {
constexpr char FIRMWARE_VERSION[] = "1.1.0";
constexpr char MANUFACTURER[] = "MS-De-sign / Marcus Sonntag";
constexpr char LICENSE_TEXT[] = "PolyForm Noncommercial License 1.0.0";
constexpr char PROJECT_URL[] = "https://github.com/MS-De-sign/solar-prognose-monitor";
constexpr uint16_t MODBUS_PORT = 502;
constexpr uint8_t MODBUS_UNIT = 1;
constexpr uint8_t BATTERY_TCP_UNIT = 2;
constexpr uint8_t BATTERY_RTU_UNIT = 200;
constexpr uint16_t POLL_SECONDS = 5;
constexpr char AP_PASSWORD[] = "solar123";  // mindestens 8 Zeichen
constexpr char MDNS_NAME[] = "solar-prognose-monitor";
constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 15000;
constexpr uint32_t WIFI_RETRY_MS = 30000;
constexpr uint32_t MODBUS_TIMEOUT_MS = 1200;
constexpr uint32_t RS485_BAUD = 9600;
constexpr uint8_t RS485_RX_PIN = 16;
constexpr uint8_t RS485_TX_PIN = 17;
constexpr uint8_t RS485_DE_PIN = 4;
constexpr uint8_t RS485_AUTO_DIRECTION = 255;
constexpr int32_t HEATER_HYSTERESIS_W = 150;
constexpr uint32_t API_RETRY_MS = 10000;
constexpr uint16_t MAX_SOC_HOLDING_ADDRESS = 13057;
constexpr uint16_t MIN_SOC_HOLDING_ADDRESS = 13058;
constexpr uint16_t EXPORT_LIMIT_WATTS_HOLDING_ADDRESS = 13073;
constexpr uint16_t EXPORT_LIMIT_ENABLE_HOLDING_ADDRESS = 13086;
constexpr uint16_t EXPORT_LIMIT_ENABLED_RAW = 0x00AA;
constexpr uint16_t EXPORT_LIMIT_DISABLED_RAW = 0x0055;
constexpr uint32_t RIPPLE_DEBOUNCE_MS = 2000;
constexpr uint32_t RIPPLE_RETRY_MS = 10000;
constexpr uint32_t RIPPLE_READ_INTERVAL_MS = 30000;
constexpr uint8_t MIN_MAX_SOC_PERCENT = 50;
constexpr uint32_t FORECAST_CONTROL_INTERVAL_MS = 5UL * 60UL * 1000UL;
constexpr uint32_t LOAD_SAMPLE_INTERVAL_MS = 5000;
constexpr uint32_t PROFILE_PERSIST_INTERVAL_MS = 60UL * 60UL * 1000UL;
constexpr uint32_t HISTORY_SAMPLE_SECONDS = 5UL * 60UL;
constexpr uint16_t HISTORY_RETENTION_DAYS = 31;
constexpr uint16_t PUSHOVER_FAILURE_DELAY_SECONDS = 60;
constexpr uint16_t PUSHOVER_RETRY_MINUTES = 15;
constexpr char PUSHOVER_ENDPOINT[] = "https://api.pushover.net/1/messages.json";
constexpr char HISTORY_PARTITION_LABEL[] = "history";
constexpr char HISTORY_DIRECTORY[] = "/history";
constexpr char PROFILE_PARTITION_LABEL[] = "profile";
}  // namespace ConfigDefaults

// Vertrauensanker fuer die verifizierte TLS-Verbindung zu api.pushover.net.
// Quelle: DigiCert Global Root G2, SHA-256
// CB:3C:CB:B7:60:31:E5:E0:13:8F:8D:D3:9A:23:F9:DE:
// 47:FF:C3:5E:43:C1:14:4C:EA:27:D4:6A:5A:B1:CB:5F
const char PUSHOVER_ROOT_CA[] PROGMEM = R"PEM(-----BEGIN CERTIFICATE-----
MIIDjjCCAnagAwIBAgIQAzrx5qcRqaC7KGSxHQn65TANBgkqhkiG9w0BAQsFADBh
MQswCQYDVQQGEwJVUzEVMBMGA1UEChMMRGlnaUNlcnQgSW5jMRkwFwYDVQQLExB3
d3cuZGlnaWNlcnQuY29tMSAwHgYDVQQDExdEaWdpQ2VydCBHbG9iYWwgUm9vdCBH
MjAeFw0xMzA4MDExMjAwMDBaFw0zODAxMTUxMjAwMDBaMGExCzAJBgNVBAYTAlVT
MRUwEwYDVQQKEwxEaWdpQ2VydCBJbmMxGTAXBgNVBAsTEHd3dy5kaWdpY2VydC5j
b20xIDAeBgNVBAMTF0RpZ2lDZXJ0IEdsb2JhbCBSb290IEcyMIIBIjANBgkqhkiG
9w0BAQEFAAOCAQ8AMIIBCgKCAQEAuzfNNNx7a8myaJCtSnX/RrohCgiN9RlUyfuI
2/Ou8jqJkTx65qsGGmvPrC3oXgkkRLpimn7Wo6h+4FR1IAWsULecYxpsMNzaHxmx
1x7e/dfgy5SDN67sH0NO3Xss0r0upS/kqbitOtSZpLYl6ZtrAGCSYP9PIUkY92eQ
q2EGnI/yuum06ZIya7XzV+hdG82MHauVBJVJ8zUtluNJbd134/tJS7SsVQepj5Wz
tCO7TG1F8PapspUwtP1MVYwnSlcUfIKdzXOS0xZKBgyMUNGPHgm+F6HmIcr9g+UQ
vIOlCsRnKPZzFBQ9RnbDhxSJITRNrw9FDKZJobq7nMWxM4MphQIDAQABo0IwQDAP
BgNVHRMBAf8EBTADAQH/MA4GA1UdDwEB/wQEAwIBhjAdBgNVHQ4EFgQUTiJUIBiV
5uNu5g/6+rkS7QYXjzkwDQYJKoZIhvcNAQELBQADggEBAGBnKJRvDkhj6zHd6mcY
1Yl9PMWLSn/pvtsrF9+wX3N3KjITOYFnQoQj8kVnNeyIv/iPsGEMNKSuIEyExtv4
NeF22d+mQrvHRAiGfzZ0JFrabA0UWTW98kndth/Jsw1HKj2ZL7tcu7XUIOGZX1NG
Fdtom/DzMNU+MeKNhJ7jitralj41E6Vf8PlwUHBHQRFXGU7Aj64GxJUTFy8bJZ91
8rGOmaFvE7FBcf6IKshPECBV1/MUReXgRPTqh5Uykw7+U0b6LJ3/iyK5S9kJRaTe
pLiaWN0bfVKfjllDiIGknibVb63dDcY3fe0Dkhvld1927jyNxF1WW6LZZm6zNTfl
MrY=
-----END CERTIFICATE-----
)PEM";

constexpr size_t SURPLUS_STAGE_COUNT = 5;
constexpr size_t RIPPLE_INPUT_COUNT = 4;
constexpr size_t DEBUG_BUFFER_MAX = 12000;
constexpr size_t PV_ARRAY_COUNT = 4;
constexpr size_t FORECAST_POINT_COUNT = 48;
constexpr size_t LOAD_PROFILE_DAYS = 7;
constexpr size_t LOAD_PROFILE_SLOTS = 96;
constexpr size_t HISTORY_POINT_COUNT = 288;
constexpr int16_t HISTORY_INVALID_POWER = INT16_MIN;
constexpr uint16_t HISTORY_INVALID_SOC = UINT16_MAX;
const uint8_t SAFE_OUTPUT_GPIOS[] = {4, 13, 14, 16, 17, 18, 19, 21, 22, 23, 25, 26, 27, 32, 33};
constexpr size_t SAFE_OUTPUT_GPIO_COUNT = sizeof(SAFE_OUTPUT_GPIOS) / sizeof(SAFE_OUTPUT_GPIOS[0]);
const uint8_t SAFE_RS485_RX_GPIOS[] = {4, 13, 14, 16, 17, 18, 19, 21, 22, 23, 25, 26, 27, 32, 33, 34, 35, 36, 39};
constexpr size_t SAFE_RS485_RX_GPIO_COUNT = sizeof(SAFE_RS485_RX_GPIOS) / sizeof(SAFE_RS485_RX_GPIOS[0]);

enum class ValueType : uint8_t {
  U16,
  S16,
  U32_WORD_SWAPPED,
  S32_WORD_SWAPPED
};

enum class SourceGroup : uint8_t {
  INVERTER,
  BATTERY
};

enum class WebView : uint8_t {
  INVERTER,
  BATTERY
};

struct RegisterDef {
  uint16_t address;
  SourceGroup source;
  WebView view;
  const char *name;
  const char *description;
  const char *unit;
  ValueType type;
  uint8_t length;
  float factor;
  float offset;
  uint8_t decimals;
  double value;
  bool valid;
  uint32_t updatedAt;
};

#define REG16(addr, en, de, unitText, valueType, scale, add, digits) \
  {addr, SourceGroup::INVERTER, WebView::INVERTER, en, de, unitText, valueType, 1, scale, add, digits, 0.0, false, 0}
#define REG32(addr, en, de, unitText, valueType, scale, add, digits) \
  {addr, SourceGroup::INVERTER, WebView::INVERTER, en, de, unitText, valueType, 2, scale, add, digits, 0.0, false, 0}
#define BREG16(addr, en, de, unitText, valueType, scale, add, digits) \
  {addr, SourceGroup::INVERTER, WebView::BATTERY, en, de, unitText, valueType, 1, scale, add, digits, 0.0, false, 0}
#define BREG32(addr, en, de, unitText, valueType, scale, add, digits) \
  {addr, SourceGroup::INVERTER, WebView::BATTERY, en, de, unitText, valueType, 2, scale, add, digits, 0.0, false, 0}
#define BAT200_16(addr, en, de, unitText, valueType, scale, add, digits) \
  {addr, SourceGroup::BATTERY, WebView::BATTERY, en, de, unitText, valueType, 1, scale, add, digits, 0.0, false, 0}
#define BAT200_32(addr, en, de, unitText, valueType, scale, add, digits) \
  {addr, SourceGroup::BATTERY, WebView::BATTERY, en, de, unitText, valueType, 2, scale, add, digits, 0.0, false, 0}

#if 0  // Vollstaendige historische Registerliste; zugunsten des Flashbedarfs bewusst deaktiviert.
RegisterDef registers[] = {
  REG16(4999, "Device type code", "Geräte-Typcode", "", ValueType::U16, 1.0f, 0.0f, 0),
  REG16(5000, "Nominal Output Power", "Installierte Leistung", "kW", ValueType::U16, 0.1f, 0.0f, 1),
  REG16(5002, "Daily Output Energy", "Eigene Energienutzung heute (PV & Akku)", "kWh", ValueType::U16, 0.1f, 0.0f, 1),
  REG32(5003, "Total Output Energy", "Eigene Energienutzung gesamt (PV & Akku)", "kWh", ValueType::U32_WORD_SWAPPED, 0.1f, 0.0f, 1),
  REG16(5007, "Inside Temperature", "Temperatur im Wechselrichter", "°C", ValueType::S16, 0.1f, 0.0f, 1),
  REG16(5010, "MPPT 1 Voltage", "MPPT1 Spannung", "V", ValueType::U16, 0.1f, 0.0f, 1),
  REG16(5011, "MPPT 1 Current", "MPPT1 Strom", "A", ValueType::U16, 0.1f, 0.0f, 1),
  REG16(5012, "MPPT 2 Voltage", "MPPT2 Spannung", "V", ValueType::U16, 0.1f, 0.0f, 1),
  REG16(5013, "MPPT 2 Current", "MPPT2 Strom", "A", ValueType::U16, 0.1f, 0.0f, 1),
  REG32(5016, "Total DC Power", "PV-Leistung aktuell", "W", ValueType::U32_WORD_SWAPPED, 1.0f, 0.0f, 0),
  REG16(5018, "Voltage phase A", "Spannung Phase A", "V", ValueType::U16, 0.1f, 0.0f, 1),
  REG16(5019, "Voltage phase B", "Spannung Phase B", "V", ValueType::U16, 0.1f, 0.0f, 1),
  REG16(5020, "Voltage phase C", "Spannung Phase C", "V", ValueType::U16, 0.1f, 0.0f, 1),
  REG32(5032, "Reactive Power", "Blindleistung", "var", ValueType::S32_WORD_SWAPPED, 1.0f, 0.0f, 0),
  REG16(5034, "Power Factor", "Leistungsfaktor", "", ValueType::S16, 0.001f, 0.0f, 3),
  REG16(5035, "Grid Frequency", "Netzfrequenz", "Hz", ValueType::U16, 0.1f, 0.0f, 1),

  REG32(5600, "DTSU666 total power", "Smart Meter Gesamtleistung", "W", ValueType::S32_WORD_SWAPPED, 1.0f, 0.0f, 0),
  REG32(5602, "DTSU666 phase L1 power", "Smart Meter Leistung L1", "W", ValueType::S32_WORD_SWAPPED, 1.0f, 0.0f, 0),
  REG32(5604, "DTSU666 phase L2 power", "Smart Meter Leistung L2", "W", ValueType::S32_WORD_SWAPPED, 1.0f, 0.0f, 0),
  REG32(5606, "DTSU666 phase L3 power", "Smart Meter Leistung L3", "W", ValueType::S32_WORD_SWAPPED, 1.0f, 0.0f, 0),
  REG16(5740, "DTSU666 phase A voltage", "Smart Meter Spannung Phase 1", "V", ValueType::U16, 0.1f, 0.0f, 1),
  REG16(5741, "DTSU666 phase B voltage", "Smart Meter Spannung Phase 2", "V", ValueType::U16, 0.1f, 0.0f, 1),
  REG16(5742, "DTSU666 phase C voltage", "Smart Meter Spannung Phase 3", "V", ValueType::U16, 0.1f, 0.0f, 1),
  REG16(5743, "DTSU666 phase A current", "Smart Meter Strom Phase 1", "A", ValueType::U16, 0.01f, 0.0f, 2),
  REG16(5744, "DTSU666 phase B current", "Smart Meter Strom Phase 2", "A", ValueType::U16, 0.01f, 0.0f, 2),
  REG16(5745, "DTSU666 phase C current", "Smart Meter Strom Phase 3", "A", ValueType::U16, 0.01f, 0.0f, 2),
  REG32(5746, "DTSU666 import energy", "Smart Meter Netzbezug", "kWh", ValueType::U32_WORD_SWAPPED, 0.01f, 0.0f, 2),
  REG32(5748, "DTSU666 export energy", "Smart Meter Netzeinspeisung", "kWh", ValueType::U32_WORD_SWAPPED, 0.01f, 0.0f, 2),

  REG16(6226, "Monthly PV energy yield January", "Monatlicher PV-Energieertrag Januar", "kWh", ValueType::U16, 0.1f, 0.0f, 1),
  REG16(6227, "Monthly PV energy yield February", "Monatlicher PV-Energieertrag Februar", "kWh", ValueType::U16, 0.1f, 0.0f, 1),
  REG16(6228, "Monthly PV energy yield March", "Monatlicher PV-Energieertrag März", "kWh", ValueType::U16, 0.1f, 0.0f, 1),
  REG16(6229, "Monthly PV energy yield April", "Monatlicher PV-Energieertrag April", "kWh", ValueType::U16, 0.1f, 0.0f, 1),
  REG16(6230, "Monthly PV energy yield May", "Monatlicher PV-Energieertrag Mai", "kWh", ValueType::U16, 0.1f, 0.0f, 1),
  REG16(6231, "Monthly PV energy yield June", "Monatlicher PV-Energieertrag Juni", "kWh", ValueType::U16, 0.1f, 0.0f, 1),
  REG16(6232, "Monthly PV energy yield July", "Monatlicher PV-Energieertrag Juli", "kWh", ValueType::U16, 0.1f, 0.0f, 1),
  REG16(6233, "Monthly PV energy yield August", "Monatlicher PV-Energieertrag August", "kWh", ValueType::U16, 0.1f, 0.0f, 1),
  REG16(6234, "Monthly PV energy yield September", "Monatlicher PV-Energieertrag September", "kWh", ValueType::U16, 0.1f, 0.0f, 1),
  REG16(6235, "Monthly PV energy yield October", "Monatlicher PV-Energieertrag Oktober", "kWh", ValueType::U16, 0.1f, 0.0f, 1),
  REG16(6236, "Monthly PV energy yield November", "Monatlicher PV-Energieertrag November", "kWh", ValueType::U16, 0.1f, 0.0f, 1),
  REG16(6237, "Monthly PV energy yield December", "Monatlicher PV-Energieertrag Dezember", "kWh", ValueType::U16, 0.1f, 0.0f, 1),

  REG16(6416, "Monthly direct PV consumption January", "Monatlicher Direktverbrauch aus PV im Januar", "kWh", ValueType::U16, 0.1f, 0.0f, 1),
  REG16(6417, "Monthly direct PV consumption February", "Monatlicher Direktverbrauch aus PV im Februar", "kWh", ValueType::U16, 0.1f, 0.0f, 1),
  REG16(6418, "Monthly direct PV consumption March", "Monatlicher Direktverbrauch aus PV im März", "kWh", ValueType::U16, 0.1f, 0.0f, 1),
  REG16(6419, "Monthly direct PV consumption April", "Monatlicher Direktverbrauch aus PV im April", "kWh", ValueType::U16, 0.1f, 0.0f, 1),
  REG16(6420, "Monthly direct PV consumption May", "Monatlicher Direktverbrauch aus PV im Mai", "kWh", ValueType::U16, 0.1f, 0.0f, 1),
  REG16(6421, "Monthly direct PV consumption June", "Monatlicher Direktverbrauch aus PV im Juni", "kWh", ValueType::U16, 0.1f, 0.0f, 1),
  REG16(6422, "Monthly direct PV consumption July", "Monatlicher Direktverbrauch aus PV im Juli", "kWh", ValueType::U16, 0.1f, 0.0f, 1),
  REG16(6423, "Monthly direct PV consumption August", "Monatlicher Direktverbrauch aus PV im August", "kWh", ValueType::U16, 0.1f, 0.0f, 1),
  REG16(6424, "Monthly direct PV consumption September", "Monatlicher Direktverbrauch aus PV im September", "kWh", ValueType::U16, 0.1f, 0.0f, 1),
  REG16(6425, "Monthly direct PV consumption October", "Monatlicher Direktverbrauch aus PV im Oktober", "kWh", ValueType::U16, 0.1f, 0.0f, 1),
  REG16(6426, "Monthly direct PV consumption November", "Monatlicher Direktverbrauch aus PV im November", "kWh", ValueType::U16, 0.1f, 0.0f, 1),
  REG16(6427, "Monthly direct PV consumption December", "Monatlicher Direktverbrauch aus PV im Dezember", "kWh", ValueType::U16, 0.1f, 0.0f, 1),

  REG16(6595, "Monthly PV export January", "Monatlicher Energieexport aus PV Januar", "kWh", ValueType::U16, 0.1f, 0.0f, 1),
  REG16(6596, "Monthly PV export February", "Monatlicher Energieexport aus PV Februar", "kWh", ValueType::U16, 0.1f, 0.0f, 1),
  REG16(6597, "Monthly PV export March", "Monatlicher Energieexport aus PV März", "kWh", ValueType::U16, 0.1f, 0.0f, 1),
  REG16(6598, "Monthly PV export April", "Monatlicher Energieexport aus PV April", "kWh", ValueType::U16, 0.1f, 0.0f, 1),
  REG16(6599, "Monthly PV export May", "Monatlicher Energieexport aus PV Mai", "kWh", ValueType::U16, 0.1f, 0.0f, 1),
  REG16(6600, "Monthly PV export June", "Monatlicher Energieexport aus PV Juni", "kWh", ValueType::U16, 0.1f, 0.0f, 1),
  REG16(6601, "Monthly PV export July", "Monatlicher Energieexport aus PV Juli", "kWh", ValueType::U16, 0.1f, 0.0f, 1),
  REG16(6602, "Monthly PV export August", "Monatlicher Energieexport aus PV August", "kWh", ValueType::U16, 0.1f, 0.0f, 1),
  REG16(6603, "Monthly PV export September", "Monatlicher Energieexport aus PV September", "kWh", ValueType::U16, 0.1f, 0.0f, 1),
  REG16(6604, "Monthly PV export October", "Monatlicher Energieexport aus PV Oktober", "kWh", ValueType::U16, 0.1f, 0.0f, 1),
  REG16(6605, "Monthly PV export November", "Monatlicher Energieexport aus PV November", "kWh", ValueType::U16, 0.1f, 0.0f, 1),
  REG16(6606, "Monthly PV export December", "Monatlicher Energieexport aus PV Dezember", "kWh", ValueType::U16, 0.1f, 0.0f, 1),

  REG16(12999, "System State", "Systemstatus", "", ValueType::U16, 1.0f, 0.0f, 0),
  REG16(13000, "Running State", "Betriebsstatus", "", ValueType::U16, 1.0f, 0.0f, 0),
  REG16(13001, "Daily PV Generation", "PV-Stromerzeugung heute", "kWh", ValueType::U16, 0.1f, 0.0f, 1),
  REG32(13002, "Total PV Generation", "PV-Stromerzeugung gesamt", "kWh", ValueType::U32_WORD_SWAPPED, 0.1f, 0.0f, 1),
  REG16(13004, "Daily export energy from PV", "PV-Einspeiseenergie heute", "kWh", ValueType::U16, 0.1f, 0.0f, 1),
  REG32(13005, "Total export energy from PV", "PV-Einspeiseenergie gesamt", "kWh", ValueType::U32_WORD_SWAPPED, 0.1f, 0.0f, 1),
  REG32(13007, "Load power", "Wirkleistung gesamt", "W", ValueType::S32_WORD_SWAPPED, 1.0f, 0.0f, 0),
  REG32(13009, "Export power", "Aktuelle Leistung am Netz-Übergabepunkt", "W", ValueType::S32_WORD_SWAPPED, 1.0f, 0.0f, 0),
  BREG16(13011, "Daily battery charge energy from PV", "Energie in Speicher heute", "kWh", ValueType::U16, 0.1f, 0.0f, 1),
  BREG32(13012, "Total battery charge energy from PV", "Energie in Speicher gesamt", "kWh", ValueType::U32_WORD_SWAPPED, 0.1f, 0.0f, 1),
  REG32(13014, "CO2 reduction", "CO₂-Reduzierung", "kg", ValueType::U32_WORD_SWAPPED, 0.1f, 0.0f, 1),
  REG16(13016, "Daily direct energy consumption", "Direkter Eigenverbrauch aus PV heute", "kWh", ValueType::U16, 0.1f, 0.0f, 1),
  REG32(13017, "Total direct energy consumption", "Direkter Eigenverbrauch aus PV gesamt", "kWh", ValueType::U32_WORD_SWAPPED, 0.1f, 0.0f, 1),
  BREG16(13019, "Battery voltage", "Batteriespannung", "V", ValueType::U16, 0.1f, 0.0f, 1),
  BREG16(13020, "Battery current", "Batteriestrom", "A", ValueType::U16, 0.1f, 0.0f, 1),
  BREG16(13021, "Battery power", "Batterieleistungsabgabe (+ Entladen / - Laden)", "W", ValueType::U16, 1.0f, 0.0f, 0),
  BREG16(13022, "Battery level", "Relativer Batteriestand zum aktuellen Max-SOC-Wert", "%", ValueType::U16, 0.1f, 0.0f, 1),
  BREG16(13023, "Battery state of health", "Batterie-Gesundheit", "%", ValueType::U16, 0.1f, 0.0f, 1),
  BREG16(13024, "Battery temperature", "Batterietemperatur", "°C", ValueType::S16, 0.1f, 0.0f, 1),
  BREG16(13025, "Daily battery discharge energy", "Batterie-Entladeenergie heute", "kWh", ValueType::U16, 0.1f, 0.0f, 1),
  BREG32(13026, "Total battery discharge energy", "Batterie-Entladeenergie gesamt", "kWh", ValueType::U32_WORD_SWAPPED, 0.1f, 1.0f, 1),
  REG16(13028, "Self-consumption today", "Eigenverbrauchsanteil heute", "%", ValueType::U16, 0.1f, 0.0f, 1),
  REG16(13029, "Grid state", "Netzstatus", "", ValueType::S16, 1.0f, 0.0f, 0),
  REG16(13030, "Phase A current", "Strom Phase A", "A", ValueType::U16, 0.1f, 0.0f, 1),
  REG16(13031, "Phase B current", "Strom Phase B", "A", ValueType::U16, 0.1f, 0.0f, 1),
  REG16(13032, "Phase C current", "Strom Phase C", "A", ValueType::U16, 0.1f, 0.0f, 1),
  REG32(13033, "Total active power", "Eigenverbrauch aktuell", "W", ValueType::S32_WORD_SWAPPED, 1.0f, 0.0f, 0),
  REG16(13035, "Daily import energy", "Gekaufte Energie heute", "kWh", ValueType::U16, 0.1f, 0.0f, 1),
  REG16(13036, "Total import energy", "Gekaufte Energie gesamt", "kWh", ValueType::U16, 0.1f, 0.0f, 1),
  BREG16(13038, "Battery capacity", "Batteriekapazität", "kWh", ValueType::U16, 0.01f, 0.0f, 1),
  BREG16(13039, "Daily charge energy", "Batterie-Ladeenergie heute", "kWh", ValueType::U16, 0.1f, 0.0f, 1),
  BREG32(13040, "Total charge energy", "Batterie-Ladeenergie gesamt", "kWh", ValueType::U32_WORD_SWAPPED, 0.1f, 0.0f, 1),
  REG16(13044, "Daily export energy", "Netzeinspeisung heute", "kWh", ValueType::U16, 0.1f, 0.0f, 1),
  REG32(13045, "Total export energy", "Netzeinspeisung gesamt", "kWh", ValueType::U32_WORD_SWAPPED, 0.1f, 0.0f, 1),
  REG32(13049, "Inverter alarm", "Wechselrichter-Alarm", "", ValueType::U32_WORD_SWAPPED, 1.0f, 0.0f, 0),
  REG32(13051, "Grid-side fault", "Netzfehler", "", ValueType::U32_WORD_SWAPPED, 1.0f, 0.0f, 0),
  REG32(13053, "System fault 1", "Systemfehler 1", "", ValueType::U32_WORD_SWAPPED, 1.0f, 0.0f, 0),
  REG32(13055, "System fault 2", "Systemfehler 2", "", ValueType::U32_WORD_SWAPPED, 1.0f, 0.0f, 0),
  REG32(13057, "DC-side fault", "Fehler DC-seitig", "", ValueType::U32_WORD_SWAPPED, 1.0f, 0.0f, 0),
  REG32(13059, "Permanent fault", "Permanenter Fehler", "", ValueType::U32_WORD_SWAPPED, 1.0f, 0.0f, 0),
  BREG32(13061, "BDC-side fault", "BDC-seitiger Fehler", "", ValueType::U32_WORD_SWAPPED, 1.0f, 0.0f, 0),
  BREG32(13063, "BDC-side permanent fault", "Permanenter BDC-seitiger Fehler", "", ValueType::U32_WORD_SWAPPED, 1.0f, 0.0f, 0),
  BREG32(13065, "Battery fault", "Batteriefehler", "", ValueType::U32_WORD_SWAPPED, 1.0f, 0.0f, 0),
  BREG32(13067, "Battery alarm", "Batteriealarm", "", ValueType::U32_WORD_SWAPPED, 1.0f, 0.0f, 0),
  BREG32(13069, "BMS alarm", "BMS-Alarm", "", ValueType::U32_WORD_SWAPPED, 1.0f, 0.0f, 0),
  BREG32(13071, "BMS protection", "BMS-Schutzstatus", "", ValueType::U32_WORD_SWAPPED, 1.0f, 0.0f, 0),
  BREG32(13073, "BMS fault 1", "BMS-Fehler 1", "", ValueType::U32_WORD_SWAPPED, 1.0f, 0.0f, 0),
  BREG32(13075, "BMS fault 2", "BMS-Fehler 2", "", ValueType::U32_WORD_SWAPPED, 1.0f, 0.0f, 0),
  BREG32(13077, "BMS alarm 2", "BMS-Alarm 2", "", ValueType::U32_WORD_SWAPPED, 1.0f, 0.0f, 0),

  BAT200_16(10740, "Battery1 Voltage", "Batteriespannung", "V", ValueType::U16, 0.1f, 0.0f, 1),
  BAT200_16(10741, "Battery1 Current", "Batteriestrom", "A", ValueType::U16, 0.1f, 0.0f, 1),
  BAT200_16(10742, "Battery1 Temperature", "Batterietemperatur", "°C", ValueType::U16, 0.1f, 0.0f, 1),
  BAT200_16(10743, "Battery1 SOC", "Absoluter Batteriestand", "%", ValueType::U16, 0.1f, 0.0f, 1),
  BAT200_16(10744, "Battery1 SOH", "Batterie SOH aus Batteriedaten", "%", ValueType::U16, 1.0f, 0.0f, 0),
  BAT200_32(10745, "Battery1 Total Battery Charge", "Batterie-Ladeenergie gesamt", "kWh", ValueType::U32_WORD_SWAPPED, 0.1f, 0.0f, 1),
  BAT200_32(10747, "Battery1 Total Battery Discharge", "Batterie-Entladeenergie gesamt", "kWh", ValueType::U32_WORD_SWAPPED, 0.1f, 0.0f, 1),
  BAT200_16(10756, "Battery1 Max Voltage of Cell", "Maximale Zellspannung", "V", ValueType::U16, 0.0001f, 0.0f, 4),
  BAT200_16(10757, "Battery1 Position of Max Voltage Cell", "Position der Zelle mit Maximalspannung", "", ValueType::U16, 1.0f, 0.0f, 0),
  BAT200_16(10758, "Battery1 Min Voltage of Cell", "Minimale Zellspannung", "V", ValueType::U16, 0.0001f, 0.0f, 4),
  BAT200_16(10759, "Battery1 Position of Min Voltage Cell", "Position der Zelle mit Minimalspannung", "", ValueType::U16, 1.0f, 0.0f, 0),
  BAT200_16(10760, "Battery1 Max Temperature of Module", "Maximale Modultemperatur", "°C", ValueType::U16, 0.1f, 0.0f, 1),
  BAT200_16(10761, "Battery1 Position of Max Temperature of Module", "Position der maximalen Modultemperatur", "", ValueType::U16, 1.0f, 0.0f, 0),
  BAT200_16(10762, "Battery1 Min Temperature of Module", "Minimale Modultemperatur", "°C", ValueType::U16, 0.1f, 0.0f, 1),
  BAT200_16(10763, "Battery1 Position of Min Temperature of Module", "Position der minimalen Modultemperatur", "", ValueType::U16, 1.0f, 0.0f, 0),
  BAT200_16(10764, "Battery1 Max Cell Voltage of Module 1", "Maximale Zellspannung Modul 1", "V", ValueType::U16, 0.0001f, 0.0f, 4),
  BAT200_16(10765, "Battery1 Max Cell Voltage of Module 2", "Maximale Zellspannung Modul 2", "V", ValueType::U16, 0.0001f, 0.0f, 4),
  BAT200_16(10766, "Battery1 Max Cell Voltage of Module 3", "Maximale Zellspannung Modul 3", "V", ValueType::U16, 0.0001f, 0.0f, 4),
  BAT200_16(10767, "Battery1 Max Cell Voltage of Module 4", "Maximale Zellspannung Modul 4", "V", ValueType::U16, 0.0001f, 0.0f, 4),
  BAT200_16(10768, "Battery1 Max Cell Voltage of Module 5", "Maximale Zellspannung Modul 5", "V", ValueType::U16, 0.0001f, 0.0f, 4),
  BAT200_16(10769, "Battery1 Max Cell Voltage of Module 6", "Maximale Zellspannung Modul 6", "V", ValueType::U16, 0.0001f, 0.0f, 4),
  BAT200_16(10770, "Battery1 Max Cell Voltage of Module 7", "Maximale Zellspannung Modul 7", "V", ValueType::U16, 0.0001f, 0.0f, 4),
  BAT200_16(10771, "Battery1 Max Cell Voltage of Module 8", "Maximale Zellspannung Modul 8", "V", ValueType::U16, 0.0001f, 0.0f, 4),
  BAT200_16(10772, "Battery1 Min Cell Voltage of Module 1", "Minimale Zellspannung Modul 1", "V", ValueType::U16, 0.0001f, 0.0f, 4),
  BAT200_16(10773, "Battery1 Min Cell Voltage of Module 2", "Minimale Zellspannung Modul 2", "V", ValueType::U16, 0.0001f, 0.0f, 4),
  BAT200_16(10774, "Battery1 Min Cell Voltage of Module 3", "Minimale Zellspannung Modul 3", "V", ValueType::U16, 0.0001f, 0.0f, 4),
  BAT200_16(10775, "Battery1 Min Cell Voltage of Module 4", "Minimale Zellspannung Modul 4", "V", ValueType::U16, 0.0001f, 0.0f, 4),
  BAT200_16(10776, "Battery1 Min Cell Voltage of Module 5", "Minimale Zellspannung Modul 5", "V", ValueType::U16, 0.0001f, 0.0f, 4),
  BAT200_16(10777, "Battery1 Min Cell Voltage of Module 6", "Minimale Zellspannung Modul 6", "V", ValueType::U16, 0.0001f, 0.0f, 4),
  BAT200_16(10778, "Battery1 Min Cell Voltage of Module 7", "Minimale Zellspannung Modul 7", "V", ValueType::U16, 0.0001f, 0.0f, 4),
  BAT200_16(10779, "Battery1 Min Cell Voltage of Module 8", "Minimale Zellspannung Modul 8", "V", ValueType::U16, 0.0001f, 0.0f, 4),
  BAT200_16(10780, "Battery1 Cell Type of Module 1", "Zelltyp Modul 1", "", ValueType::U16, 1.0f, 0.0f, 0),
  BAT200_16(10781, "Battery1 Cell Type of Module 2", "Zelltyp Modul 2", "", ValueType::U16, 1.0f, 0.0f, 0),
  BAT200_16(10782, "Battery1 Cell Type of Module 3", "Zelltyp Modul 3", "", ValueType::U16, 1.0f, 0.0f, 0),
  BAT200_16(10783, "Battery1 Cell Type of Module 4", "Zelltyp Modul 4", "", ValueType::U16, 1.0f, 0.0f, 0),
  BAT200_16(10784, "Battery1 Cell Type of Module 5", "Zelltyp Modul 5", "", ValueType::U16, 1.0f, 0.0f, 0),
  BAT200_16(10785, "Battery1 Cell Type of Module 6", "Zelltyp Modul 6", "", ValueType::U16, 1.0f, 0.0f, 0),
  BAT200_16(10786, "Battery1 Cell Type of Module 7", "Zelltyp Modul 7", "", ValueType::U16, 1.0f, 0.0f, 0),
  BAT200_16(10787, "Battery1 Cell Type of Module 8", "Zelltyp Modul 8", "", ValueType::U16, 1.0f, 0.0f, 0),
  BAT200_16(10788, "Battery1 State DC Switch", "Status Batterie-DC-Schalter", "", ValueType::U16, 1.0f, 0.0f, 0)
};

#undef REG16
#undef REG32
#undef BREG16
#undef BREG32
#undef BAT200_16
#undef BAT200_32

constexpr size_t REGISTER_COUNT = sizeof(registers) / sizeof(registers[0]);
static_assert(REGISTER_COUNT == 153, "Die Registerliste muss genau 153 Eintraege enthalten.");
#endif

RegisterDef registers[] = {
  REG16(5007, "Inside Temperature", "Temperatur im Wechselrichter", "°C", ValueType::S16, 0.1f, 0.0f, 1),
  REG32(5016, "Total DC Power", "PV-Leistung aktuell", "W", ValueType::U32_WORD_SWAPPED, 1.0f, 0.0f, 0),
  REG32(5746, "DTSU666 import energy", "Smart Meter Netzbezug gesamt", "kWh", ValueType::U32_WORD_SWAPPED, 0.01f, 0.0f, 2),
  REG32(5748, "DTSU666 export energy", "Smart Meter Netzeinspeisung gesamt", "kWh", ValueType::U32_WORD_SWAPPED, 0.01f, 0.0f, 2),
  REG16(13001, "Daily PV Generation", "PV-Erzeugung heute", "kWh", ValueType::U16, 0.1f, 0.0f, 1),
  REG32(13002, "Total PV Generation", "PV-Erzeugung gesamt", "kWh", ValueType::U32_WORD_SWAPPED, 0.1f, 0.0f, 1),
  REG16(13004, "Daily export energy from PV", "PV-Einspeiseenergie heute", "kWh", ValueType::U16, 0.1f, 0.0f, 1),
  REG32(13005, "Total export energy from PV", "PV-Einspeiseenergie gesamt", "kWh", ValueType::U32_WORD_SWAPPED, 0.1f, 0.0f, 1),
  REG32(13007, "Load power", "Haus-/Lastleistung aktuell", "W", ValueType::S32_WORD_SWAPPED, 1.0f, 0.0f, 0),
  REG32(13009, "Export power", "Leistung am Netzanschlusspunkt", "W", ValueType::S32_WORD_SWAPPED, 1.0f, 0.0f, 0),
  BREG16(13011, "Daily battery charge energy from PV", "Aus PV in Batterie heute", "kWh", ValueType::U16, 0.1f, 0.0f, 1),
  BREG32(13012, "Total battery charge energy from PV", "Aus PV in Batterie gesamt", "kWh", ValueType::U32_WORD_SWAPPED, 0.1f, 0.0f, 1),
  BREG16(13021, "Battery power", "Batterieleistungsabgabe aktuell (+ Entladen / - Laden)", "W", ValueType::U16, 1.0f, 0.0f, 0),
  BREG16(13022, "Battery level", "Relativer Batteriestand zum aktuellen Max-SOC-Wert", "%", ValueType::U16, 0.1f, 0.0f, 1),
  BREG16(13023, "Battery state of health", "Batterie-SOH über Wechselrichter", "%", ValueType::U16, 0.1f, 0.0f, 1),
  BREG16(13024, "Battery temperature", "Batterietemperatur", "°C", ValueType::S16, 0.1f, 0.0f, 1),
  BREG16(13038, "Battery capacity", "Batteriekapazität", "kWh", ValueType::U16, 0.01f, 0.0f, 1),
  BAT200_16(10743, "Battery1 SOC", "Absoluter Batteriestand", "%", ValueType::U16, 0.1f, 0.0f, 1),
  BAT200_16(10744, "Battery1 SOH", "Batterie SOH aus Batteriedaten", "%", ValueType::U16, 1.0f, 0.0f, 0)
};

#undef REG16
#undef REG32
#undef BREG16
#undef BREG32
#undef BAT200_16
#undef BAT200_32

constexpr size_t REGISTER_COUNT = sizeof(registers) / sizeof(registers[0]);
static_assert(REGISTER_COUNT == 19, "Die kompakte Registerliste muss genau 19 Eintraege enthalten.");

struct ReadBlock {
  SourceGroup source;
  uint16_t start;
  uint16_t count;
  bool optional;
};

// Nur benoetigte Bereiche lesen; jeder Block bleibt unter 125 Registern.
const ReadBlock readBlocks[] = {
  {SourceGroup::INVERTER, 5007, 1, false},
  {SourceGroup::INVERTER, 5016, 2, false},
  {SourceGroup::INVERTER, 5746, 2, true},
  {SourceGroup::INVERTER, 5748, 2, true},
  {SourceGroup::INVERTER, 13001, 13, false},
  {SourceGroup::INVERTER, 13021, 4, false},
  {SourceGroup::INVERTER, 13038, 1, false},
  {SourceGroup::BATTERY, 10743, 2, false}
};
constexpr size_t READ_BLOCK_COUNT = sizeof(readBlocks) / sizeof(readBlocks[0]);
bool readBlockUnsupported[READ_BLOCK_COUNT] = {};

struct PvArrayConfig {
  bool enabled;
  String name;
  float peakKwp;
  uint8_t tiltDegrees;
  int16_t azimuthDegrees;  // Open-Meteo: 0 Sued, -90 Ost, +90 West
};

struct RippleInputConfig {
  bool enabled;
  uint8_t gpio;
  uint8_t percent;
  bool activeLow;
};

struct AppConfig {
  String wifiSsid;
  String wifiPassword;
  String modbusHost;
  uint16_t modbusPort;
  uint8_t inverterUnit;
  uint8_t batteryTcpUnit;
  uint8_t batteryRtuUnit;
  uint16_t pollSeconds;
  uint8_t modbusMode;
  uint32_t rs485Baud;
  uint8_t rs485RxPin;
  uint8_t rs485TxPin;
  uint8_t rs485DePin;
  bool forecastEnabled;
  bool forecastBypass;
  float latitude;
  float longitude;
  uint8_t maxSoc;
  uint16_t finishBufferMinutes;
  uint8_t forecastSafetyPercent;
  uint8_t pvSystemEfficiencyPercent;
  uint8_t batteryChargeEfficiencyPercent;
  uint8_t socStepPercent;
  uint16_t minimumWriteMinutes;
  uint8_t maximumWritesPerDay;
  uint8_t forecastFetchHours;
  uint8_t forecastStaleHours;
  uint8_t learningPercent;
  uint16_t eventLoadThresholdWatts;
  PvArrayConfig pvArrays[PV_ARRAY_COUNT];
  bool rippleEnabled;
  uint8_t rippleSignalMode;
  RippleInputConfig rippleInputs[RIPPLE_INPUT_COUNT];
  bool pushoverEnabled;
  String pushoverAppToken;
  String pushoverUserKey;
  String pushoverDevice;
  uint16_t pushoverFailureDelaySeconds;
  uint16_t pushoverRetryMinutes;
} config;

enum class ModbusMode : uint8_t {
  TCP_ONLY,
  RTU_ONLY,
  TCP_WITH_RTU_FALLBACK
};

enum class ApiMethod : uint8_t {
  GET,
  PUT,
  POST
};

enum class StageType : uint8_t {
  GPIO,
  API
};

struct SurplusStageConfig {
  uint16_t watts;
  uint16_t onDelaySeconds;
  uint16_t offDelaySeconds;
  bool enabled;
  StageType type;
  uint8_t gpio;
  ApiMethod method;
  String onUrl;
  String offUrl;
  String onJson;
  String offJson;
};

SurplusStageConfig surplusStages[SURPLUS_STAGE_COUNT];
bool stageIsOn[SURPLUS_STAGE_COUNT] = {};
bool stageStateKnown[SURPLUS_STAGE_COUNT] = {};
bool delayPending[SURPLUS_STAGE_COUNT] = {};
bool pendingState[SURPLUS_STAGE_COUNT] = {};
uint32_t conditionSince[SURPLUS_STAGE_COUNT] = {};
uint32_t lastApiAttemptAt[SURPLUS_STAGE_COUNT] = {};
String lastApiStatus[SURPLUS_STAGE_COUNT];

struct ForecastPoint {
  time_t epoch;
  uint16_t gti[PV_ARRAY_COUNT];
  float pvKw;
  float learnedLoadKw;
  float batteryEnergyKwh;
  float plannedSoc;
  float reachableSoc;
};

struct ForecastState {
  bool valid;
  bool fetching;
  String status;
  String timezoneName;
  int32_t utcOffsetSeconds;
  uint8_t pointCount;
  time_t fetchedAt;
  time_t sunrise;
  time_t sunset;
  time_t finishAt;
  float startSoc;
  float currentSoc;
  float plannedSoc;
  float reachableSoc;
  float requestedSoc;
  float totalPvKwh;
  float totalLearnedLoadKwh;
  float totalBatteryEnergyKwh;
  ForecastPoint points[FORECAST_POINT_COUNT];
};

struct LoadProfileData {
  uint32_t magic;
  uint16_t version;
  uint16_t baseWatts[LOAD_PROFILE_DAYS][LOAD_PROFILE_SLOTS];
  uint16_t eventWatts[LOAD_PROFILE_DAYS][LOAD_PROFILE_SLOTS];
  uint8_t eventProbability[LOAD_PROFILE_DAYS][LOAD_PROFILE_SLOTS];
  uint8_t samples[LOAD_PROFILE_DAYS][LOAD_PROFILE_SLOTS];
  uint16_t learnedDays[LOAD_PROFILE_DAYS];
  uint32_t crc;
};

struct __attribute__((packed)) HistorySample {
  uint32_t epoch;
  int16_t pvDecawatts;
  int16_t loadDecawatts;
  int16_t gridDecawatts;
  int16_t batteryDecawatts;
  uint16_t socTenths;
};

struct __attribute__((packed)) HistoryData {
  uint32_t magic;
  uint16_t version;
  uint16_t writeIndex;
  uint16_t count;
  uint16_t reserved;
  HistorySample samples[HISTORY_POINT_COUNT];
  uint32_t crc;
};

struct __attribute__((packed)) HistoryFileHeader {
  uint32_t magic;
  uint16_t version;
  uint16_t sampleSize;
  uint32_t crc;
};

constexpr uint32_t LOAD_PROFILE_MAGIC = 0x53474C50UL;
constexpr uint16_t LOAD_PROFILE_VERSION = 2;
constexpr uint32_t HISTORY_MAGIC = 0x53474853UL;
constexpr uint16_t HISTORY_VERSION = 1;
constexpr uint32_t HISTORY_FILE_MAGIC = 0x53474846UL;
constexpr uint16_t HISTORY_FILE_VERSION = 1;
static_assert(sizeof(LoadProfileData) < 8192, "Das Lastprofil muss kompakt genug fuer NVS bleiben.");
static_assert(sizeof(HistoryData) < 5000, "Der 24-Stunden-RAM-Puffer muss kompakt bleiben.");
ForecastState forecast;
LoadProfileData loadProfile;
HistoryData historyData;
uint16_t todayLoadWatts[LOAD_PROFILE_SLOTS] = {};
uint16_t todaySamples[LOAD_PROFILE_SLOTS] = {};
uint32_t lastLoadSampleAt = 0;
uint32_t lastProfilePersistAt = 0;
int lastProfileSavedYearDay = -1;
int lastProfileWeekday = -1;
uint8_t activeLearningDay = 255;
uint8_t activeLearningSlot = 255;
uint64_t learningLoadSum = 0;
uint16_t learningLoadSamples = 0;
bool profileDirty = false;
bool historyStorageReady = false;
bool historyPersistenceOk = true;
bool historyDirty = false;
uint32_t lastHistorySampleSlot = UINT32_MAX;
uint32_t lastHistoryPersistAt = 0;
uint32_t lastHistoryPersistedEpoch = 0;
int lastHistoryCleanupYearDay = -1;
uint32_t nextForecastFetchAt = 0;
uint32_t nextForecastControlAt = 0;
uint16_t holdingMaxSocRaw = 0;
uint16_t holdingMinSocRaw = 0;
bool holdingSocValid = false;
uint32_t holdingSocUpdatedAt = 0;
uint16_t lastWrittenMaxSocRaw = 0;
uint32_t lastMaxSocWriteAt = 0;
uint8_t maxSocWritesToday = 0;
uint8_t priorityMaxSocWritesToday = 0;
int maxSocWriteYearDay = -1;
bool finalMaxSocReleasedToday = false;
bool forecastRestorePending = false;

uint16_t holdingExportLimitWatts = 0;
uint16_t holdingExportLimitEnabledRaw = 0;
bool holdingExportLimitValid = false;
uint32_t holdingExportLimitUpdatedAt = 0;
bool rippleReleasePending = false;
uint8_t rippleCandidatePercent = 255;
uint8_t rippleSelectedPercent = 255;
uint32_t rippleCandidateSince = 0;
uint32_t nextRippleActionAt = 0;
uint32_t nextRippleReadAt = 0;
String rippleStatus = "Rundsteuerung deaktiviert";

bool webDebugEnabled = false;
String webDebugBuffer;
bool browserUpdateSuccess = false;
bool browserUpdateRejected = false;
bool browserUpdateForced = false;
String browserUpdateMessage;
String browserUpdatePersistenceWarning;
bool firmwareUpdateInProgress = false;

Preferences preferences;
WebServer server(80);
DNSServer dnsServer;
WiFiClient modbusClient;
HardwareSerial modbusSerial(2);

String accessPointSsid;
String lastTransactionError = "Noch keine Abfrage";
String lastInverterError = "Noch keine Abfrage";
String lastBatteryError = "Noch keine Abfrage";
String lastModbusTransport = "Noch keine erfolgreiche Abfrage";
String lastBatteryTransport = "Noch keine direkte Batterieabfrage";
uint8_t lastModbusExceptionCode = 0;
uint16_t transactionId = 0;
uint32_t lastWifiAttempt = 0;
uint32_t nextPollAt = 0;
uint32_t lastSuccessfulInverterCycle = 0;
uint32_t lastSuccessfulBatteryCycle = 0;
uint32_t restartAt = 0;
size_t activeBlock = 0;
bool pollingCycleActive = false;
bool inverterCycleOk = true;
bool batteryCycleOk = true;
bool rtuFallbackActive = false;
bool mdnsStarted = false;
bool timeSyncStarted = false;

enum class PushoverEvent : uint8_t {
  NONE,
  STARTED,
  MODBUS_OUTAGE,
  MODBUS_RESTORED
};

PushoverEvent pendingPushoverEvent = PushoverEvent::NONE;
String pendingPushoverTitle;
String pendingPushoverMessage;
String lastPushoverStatus = "Noch keine Nachricht gesendet";
uint32_t nextPushoverAttemptAt = 0;
uint32_t pushoverModbusFailureSince = 0;
bool pushoverBootPending = true;
bool pushoverModbusOutageActive = false;
bool pushoverModbusOutageDelivered = false;
bool pushoverRecoveryNeeded = false;

bool timeReached(uint32_t target);

void appendWebDebug(const String &text) {
  if (!webDebugEnabled) return;
  webDebugBuffer += text;
  if (webDebugBuffer.length() > DEBUG_BUFFER_MAX) {
    webDebugBuffer.remove(0, webDebugBuffer.length() - DEBUG_BUFFER_MAX);
  }
}

void debugPrintln(const String &text = String()) {
  Serial.println(text);
  appendWebDebug(text + '\n');
}

void debugPrintf(const char *format, ...) {
  char buffer[512];
  va_list arguments;
  va_start(arguments, format);
  vsnprintf(buffer, sizeof(buffer), format, arguments);
  va_end(arguments);
  Serial.print(buffer);
  appendWebDebug(buffer);
}

bool isPushoverCredential(const String &value) {
  if (value.length() != 30) return false;
  for (size_t i = 0; i < value.length(); ++i) {
    const char c = value[i];
    if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'))) {
      return false;
    }
  }
  return true;
}

bool isPushoverDevice(const String &value) {
  if (value.length() > 25) return false;
  for (size_t i = 0; i < value.length(); ++i) {
    const char c = value[i];
    if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
          || c == '_' || c == '-')) {
      return false;
    }
  }
  return true;
}

bool pushoverConfigured() {
  return config.pushoverEnabled
      && isPushoverCredential(config.pushoverAppToken)
      && isPushoverCredential(config.pushoverUserKey)
      && isPushoverDevice(config.pushoverDevice);
}

bool systemTimeIsValid() {
  return time(nullptr) >= 1704067200;  // 01.01.2024; fuer die TLS-Zertifikatspruefung erforderlich
}

String formUrlEncode(const String &input) {
  static const char HEX_DIGITS[] = "0123456789ABCDEF";
  String output;
  output.reserve(input.length() * 3 / 2 + 8);
  for (size_t i = 0; i < input.length(); ++i) {
    const uint8_t c = static_cast<uint8_t>(input[i]);
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
        || c == '-' || c == '_' || c == '.' || c == '~') {
      output += static_cast<char>(c);
    } else if (c == ' ') {
      output += '+';
    } else {
      output += '%';
      output += HEX_DIGITS[c >> 4];
      output += HEX_DIGITS[c & 0x0F];
    }
  }
  return output;
}

bool sendPushoverMessage(const String &appToken, const String &userKey, const String &device,
                         const String &title, const String &message, String &status) {
  if (WiFi.status() != WL_CONNECTED) {
    status = "Kein Heim-WLAN verbunden";
    return false;
  }
  if (!systemTimeIsValid()) {
    status = "Systemzeit noch nicht per NTP synchronisiert";
    return false;
  }
  if (!isPushoverCredential(appToken) || !isPushoverCredential(userKey) || !isPushoverDevice(device)) {
    status = "Token, User-Key oder optionales Gerät ist ungültig";
    return false;
  }

  String body = "token=" + formUrlEncode(appToken)
              + "&user=" + formUrlEncode(userKey)
              + "&title=" + formUrlEncode(title)
              + "&message=" + formUrlEncode(message);
  if (!device.isEmpty()) body += "&device=" + formUrlEncode(device);

  NetworkClientSecure secureClient;
  secureClient.setCACert(PUSHOVER_ROOT_CA);
  HTTPClient http;
  http.setConnectTimeout(8000);
  http.setTimeout(10000);
  if (!http.begin(secureClient, ConfigDefaults::PUSHOVER_ENDPOINT)) {
    status = "HTTPS-Verbindung konnte nicht vorbereitet werden";
    return false;
  }
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  const int responseCode = http.POST(body);
  const String response = responseCode > 0 ? http.getString() : String();
  http.end();

  if (responseCode == HTTP_CODE_OK
      && (response.indexOf("\"status\":1") >= 0 || response.indexOf("\"status\": 1") >= 0)) {
    status = "Nachricht erfolgreich an Pushover übergeben";
    return true;
  }
  status = responseCode > 0
      ? "Pushover antwortete mit HTTP " + String(responseCode)
      : "HTTPS-Fehler: " + HTTPClient::errorToString(responseCode);
  return false;
}

bool queuePushoverEvent(PushoverEvent event, const String &title, const String &message) {
  if (pendingPushoverEvent != PushoverEvent::NONE) return false;
  pendingPushoverEvent = event;
  pendingPushoverTitle = title;
  pendingPushoverMessage = message;
  nextPushoverAttemptAt = millis();
  return true;
}

void observePushoverModbusState(bool healthy) {
  const uint32_t now = millis();
  if (healthy) {
    pushoverModbusFailureSince = 0;
    if (!pushoverModbusOutageActive) return;
    if (pendingPushoverEvent == PushoverEvent::MODBUS_OUTAGE && !pushoverModbusOutageDelivered) {
      pendingPushoverEvent = PushoverEvent::NONE;
      pendingPushoverTitle = "";
      pendingPushoverMessage = "";
    }
    if (pushoverModbusOutageDelivered) pushoverRecoveryNeeded = true;
    pushoverModbusOutageActive = false;
    return;
  }

  if (pushoverModbusFailureSince == 0) pushoverModbusFailureSince = now;
  const uint32_t delayMs = static_cast<uint32_t>(config.pushoverFailureDelaySeconds) * 1000UL;
  if (now - pushoverModbusFailureSince < delayMs) return;
  pushoverModbusOutageActive = true;
  if (!pushoverModbusOutageDelivered && pendingPushoverEvent == PushoverEvent::NONE) {
    String message = "Seit mindestens " + String(config.pushoverFailureDelaySeconds)
                   + " Sekunden keine Modbus-Antwort vom Wechselrichter. Fehler: "
                   + lastInverterError;
    queuePushoverEvent(PushoverEvent::MODBUS_OUTAGE, "Solar Prognose Monitor: Modbus-Ausfall", message);
  }
}

void servicePushover() {
  if (!pushoverConfigured() || WiFi.status() != WL_CONNECTED || !systemTimeIsValid()) return;

  if (pendingPushoverEvent == PushoverEvent::NONE && pushoverRecoveryNeeded) {
    queuePushoverEvent(PushoverEvent::MODBUS_RESTORED,
                       "Solar Prognose Monitor: Modbus wieder erreichbar",
                       "Die Modbus-Verbindung zum Wechselrichter arbeitet wieder. Transport: "
                           + lastModbusTransport);
  }
  if (pendingPushoverEvent == PushoverEvent::NONE && pushoverBootPending) {
    queuePushoverEvent(PushoverEvent::STARTED, "Solar Prognose Monitor gestartet",
                       "ESP32 gestartet oder neu gestartet. Firmware "
                           + String(ConfigDefaults::FIRMWARE_VERSION) + ", IP "
                           + WiFi.localIP().toString() + ".");
  }
  if (pendingPushoverEvent == PushoverEvent::NONE || !timeReached(nextPushoverAttemptAt)) return;

  String status;
  if (sendPushoverMessage(config.pushoverAppToken, config.pushoverUserKey, config.pushoverDevice,
                          pendingPushoverTitle, pendingPushoverMessage, status)) {
    const PushoverEvent deliveredEvent = pendingPushoverEvent;
    pendingPushoverEvent = PushoverEvent::NONE;
    pendingPushoverTitle = "";
    pendingPushoverMessage = "";
    nextPushoverAttemptAt = 0;
    if (deliveredEvent == PushoverEvent::STARTED) pushoverBootPending = false;
    if (deliveredEvent == PushoverEvent::MODBUS_OUTAGE) pushoverModbusOutageDelivered = true;
    if (deliveredEvent == PushoverEvent::MODBUS_RESTORED) {
      pushoverRecoveryNeeded = false;
      pushoverModbusOutageDelivered = false;
    }
    lastPushoverStatus = status;
    debugPrintln("Pushover: " + status + ".");
  } else {
    lastPushoverStatus = status;
    nextPushoverAttemptAt = millis()
        + static_cast<uint32_t>(config.pushoverRetryMinutes) * 60UL * 1000UL;
    debugPrintln("Pushover: " + status + "; späterer Wiederholungsversuch.");
  }
}

const char INDEX_HTML[] PROGMEM = R"HTML(
<!doctype html><html lang="de"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Solar Prognose Monitor</title>
<style>
:root{color-scheme:light;--bg:#f3f5f7;--card:#fff;--text:#17212b;--muted:#64717d;--accent:#087f5b;--line:#dfe4e8;--bad:#b42318}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--text);font:15px system-ui,-apple-system,Segoe UI,sans-serif}
header{background:var(--card);border-bottom:1px solid var(--line);position:sticky;top:0;z-index:2}.bar{max-width:1100px;margin:auto;padding:14px 18px;display:flex;align-items:center;gap:18px}
h1{font-size:20px;margin:0 auto 0 0}.nav{display:flex;gap:8px}.nav a{color:var(--text);text-decoration:none;padding:8px 11px;border-radius:8px}.nav a.active,.nav a:hover{background:#e6f4ef;color:#056044}
main{max-width:1100px;margin:22px auto;padding:0 18px}.status{display:flex;flex-wrap:wrap;gap:10px;margin-bottom:16px}.pill{background:var(--card);border:1px solid var(--line);border-radius:999px;padding:7px 11px}.ok{color:var(--accent)}.bad{color:var(--bad)}
.tools{display:flex;gap:10px;margin-bottom:16px}.tools input{width:100%;padding:11px 13px;border:1px solid var(--line);border-radius:9px;background:var(--card);font:inherit}
.group{background:var(--card);border:1px solid var(--line);border-radius:12px;margin:0 0 16px;overflow:hidden}.group h2{font-size:16px;margin:0;padding:14px 16px;background:#fafbfb;border-bottom:1px solid var(--line)}
table{width:100%;border-collapse:collapse}th,td{text-align:left;padding:10px 14px;border-bottom:1px solid var(--line)}th{font-size:12px;text-transform:uppercase;color:var(--muted)}tr:last-child td{border-bottom:0}.value{font-weight:650;white-space:nowrap}.addr{color:var(--muted);font-variant-numeric:tabular-nums}.empty{padding:20px;color:var(--muted)}
@media(max-width:650px){.bar{align-items:flex-start;flex-wrap:wrap}.nav{width:100%}th:nth-child(2),td:nth-child(2){display:none}th,td{padding:9px 10px}}
</style></head><body>
<header><div class="bar"><h1 id="pageTitle">Solar Prognose Monitor</h1><nav class="nav"><a id="navInverter" href="/inverter">Wechselrichter</a><a id="navBattery" href="/battery">Batterie</a><a href="/forecast">Prognose</a><a href="/load-profile">Lastprofil</a><a href="/history">Verlauf</a><a href="/settings">Einstellungen</a><a href="/firmware">Firmware</a><a href="/about">About</a></nav></div></header>
<main><div class="status" id="status"><span class="pill">Wird geladen …</span></div>
<div class="tools"><input id="search" type="search" placeholder="Messwert oder Adresse suchen …" autocomplete="off"></div>
<div id="content"><div class="group"><div class="empty">Messwerte werden geladen …</div></div></div></main>
<script>
const batteryPage=location.pathname==='/battery',view=batteryPage?'battery':'inverter';
document.querySelector(batteryPage?'#navBattery':'#navInverter').classList.add('active');
document.querySelector('#pageTitle').textContent=batteryPage?'Sungrow Batterie':'Sungrow Wechselrichter';
document.title=(batteryPage?'Batterie':'Wechselrichter')+' · Solar Prognose Monitor';
const groupName=r=>{const a=r.address;if(batteryPage){if(r.source==='battery')return a<=10748?'Batterie-ID · Übersicht':a<=10763?'Batterie-ID · Zell- und Temperaturgrenzen':a<=10779?'Batterie-ID · Modulspannungen':'Batterie-ID · Module und DC-Schalter';return a<13019?'Wechselrichter-ID · Batterieenergie aus PV':a<13038?'Wechselrichter-ID · Livewerte und Entladung':a<13061?'Wechselrichter-ID · Kapazität und Ladung':'Wechselrichter-ID · Fehler und Alarme'}return a<5100?'Wechselrichter':a<5700?'Smart Meter · Leistung':a<6000?'Smart Meter · Netzwerte':a<6400?'Monatlicher PV-Ertrag':a<6500?'Monatlicher Direktverbrauch':a<7000?'Monatlicher PV-Export':'System und Netz'};
let latest=[];
const esc=s=>String(s).replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
const renderRows=()=>{const q=document.querySelector('#search').value.trim().toLowerCase();const groups={};
 latest.filter(r=>!q||(`${r.address} ${r.name} ${r.description} ${r.unit}`).toLowerCase().includes(q)).forEach(r=>(groups[groupName(r)]??=[]).push(r));
 const out=Object.entries(groups).map(([name,rows])=>`<section class="group"><h2>${esc(name)}</h2><table><thead><tr><th>Adresse</th><th>Name</th><th>Beschreibung</th><th>Wert</th></tr></thead><tbody>${rows.map(r=>`<tr><td class="addr">${r.address}</td><td>${esc(r.name)}</td><td>${esc(r.description)}</td><td class="value">${r.unsupported?'— nicht unterstützt':r.valid?esc(r.display):'—'}${r.valid&&r.unit?' '+esc(r.unit):''}</td></tr>`).join('')}</tbody></table></section>`).join('');
 document.querySelector('#content').innerHTML=out||'<div class="group"><div class="empty">Keine passenden Messwerte.</div></div>';
};
const renderStatus=s=>{const wifi=s.wifiConnected?`WLAN: ${esc(s.wifiSsid)} · ${esc(s.stationIp)}`:'WLAN nicht verbunden';const link=` · ${esc(s.modbusTransport)}`;const inv=s.inverterOk?`ID ${s.inverterUnit}: OK · ${s.inverterValid}/${s.inverterTotal}${link}`:`ID ${s.inverterUnit}: ${esc(s.inverterError)}`;const batIds=s.modbusMode===0?`TCP-ID ${s.batteryTcpUnit}`:s.modbusMode===1?`RS485-ID ${s.batteryRtuUnit}`:`TCP-ID ${s.batteryTcpUnit} / RS485-ID ${s.batteryRtuUnit}`;const bat=s.batteryOk?`${batIds}: OK · ${s.batteryValid}/${s.batteryTotal} · ${esc(s.batteryTransport)}`:`${batIds}: ${esc(s.batteryError)}`;const heater=s.exportPowerFresh?`Netzeinspeisung: ${s.exportPower} W · verfügbar: ${s.availableSurplus} W · Stufen: ${s.stageOn}/${s.stageEnabled}`:'Überschusssteuerung: kein aktueller Einspeisewert';
 document.querySelector('#status').innerHTML=`<span class="pill ${s.wifiConnected?'ok':'bad'}">${wifi}</span><span class="pill ${s.inverterOk?'ok':'bad'}">${inv}</span>${batteryPage?`<span class="pill ${s.batteryOk?'ok':'bad'}">${bat}</span>`:`<span class="pill ${s.exportPowerFresh?'ok':'bad'}">${heater}</span><span class="pill ${s.rippleEnabled?'ok':''}">${esc(s.rippleStatus)}</span>`}<span class="pill">AP: ${esc(s.apSsid)} · ${esc(s.apIp)}</span>`;
};
const update=async()=>{try{const response=await fetch('/api/values',{cache:'no-store'});if(!response.ok)throw Error('HTTP '+response.status);const data=await response.json();latest=data.registers.filter(r=>r.view===view);renderStatus(data.status);renderRows()}catch(e){document.querySelector('#status').innerHTML=`<span class="pill bad">Webfehler: ${esc(e.message)}</span>`}};
document.querySelector('#search').addEventListener('input',renderRows);update();setInterval(update,3000);
</script></body></html>
)HTML";

const char SETTINGS_HEAD[] PROGMEM = R"HTML(
<!doctype html><html lang="de"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Einstellungen · Solar Prognose Monitor</title><style>
:root{--bg:#f3f5f7;--card:#fff;--text:#17212b;--muted:#64717d;--accent:#087f5b;--line:#dfe4e8}*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--text);font:15px system-ui,-apple-system,Segoe UI,sans-serif}
header{background:var(--card);border-bottom:1px solid var(--line)}.bar{max-width:760px;margin:auto;padding:14px 18px;display:flex;align-items:center;gap:18px}h1{font-size:20px;margin:0 auto 0 0}.nav{display:flex;gap:8px}.nav a{color:var(--text);text-decoration:none;padding:8px 11px;border-radius:8px}.nav a.active,.nav a:hover{background:#e6f4ef;color:#056044}
main{max-width:820px;margin:22px auto;padding:0 18px}.card{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:18px;margin-bottom:16px}h2{font-size:17px;margin:0 0 15px}.grid{display:grid;grid-template-columns:1fr 1fr;gap:14px}.full{grid-column:1/-1}label{display:block;font-weight:600;margin-bottom:5px}input,select,textarea{width:100%;padding:10px 11px;border:1px solid #cbd2d8;border-radius:8px;background:#fff;font:inherit}textarea{min-height:76px;resize:vertical}.hint{color:var(--muted);font-size:13px;margin:6px 0 0}.caution{background:#fff7dd;border:1px solid #e7c95d;border-radius:9px;color:#684f00;padding:11px 13px;line-height:1.45}.check{display:flex;gap:9px;align-items:center;font-weight:400}.check input{width:auto}.stage-card{border:1px solid var(--line);border-radius:10px;margin:12px 0;background:#fafbfb}.stage-card summary{cursor:pointer;padding:13px 14px;font-weight:700}.stage-body{padding:4px 14px 15px}.type-panel{margin-top:14px;padding-top:14px;border-top:1px solid var(--line)}.hidden{display:none}.button{border:0;border-radius:9px;background:var(--accent);color:#fff;padding:11px 16px;font:inherit;font-weight:650;cursor:pointer}.button.secondary{background:#66727d}.result{display:inline-block;margin-left:10px;color:var(--muted)}.info{line-height:1.6}@media(max-width:620px){.bar{flex-wrap:wrap}.nav{width:100%;overflow-x:auto}.grid{grid-template-columns:1fr}.full{grid-column:auto}.result{display:block;margin:10px 0 0}}
</style></head><body><header><div class="bar"><h1>Solar Prognose Monitor</h1><nav class="nav"><a href="/inverter">Wechselrichter</a><a href="/battery">Batterie</a><a href="/forecast">Prognose</a><a href="/load-profile">Lastprofil</a><a href="/history">Verlauf</a><a class="active" href="/settings">Einstellungen</a><a href="/firmware">Firmware</a><a href="/about">About</a></nav></div></header><main>
)HTML";

String htmlEscape(const String &input) {
  String output;
  output.reserve(input.length() + 8);
  for (size_t i = 0; i < input.length(); ++i) {
    switch (input[i]) {
      case '&': output += F("&amp;"); break;
      case '<': output += F("&lt;"); break;
      case '>': output += F("&gt;"); break;
      case '"': output += F("&quot;"); break;
      case '\'': output += F("&#39;"); break;
      default: output += input[i]; break;
    }
  }
  return output;
}

String jsonEscape(const String &input) {
  String output;
  output.reserve(input.length() + 8);
  for (size_t i = 0; i < input.length(); ++i) {
    const char c = input[i];
    switch (c) {
      case '"': output += F("\\\""); break;
      case '\\': output += F("\\\\"); break;
      case '\b': output += F("\\b"); break;
      case '\f': output += F("\\f"); break;
      case '\n': output += F("\\n"); break;
      case '\r': output += F("\\r"); break;
      case '\t': output += F("\\t"); break;
      default:
        if (static_cast<uint8_t>(c) < 0x20) {
          char escaped[7];
          snprintf(escaped, sizeof(escaped), "\\u%04x", static_cast<unsigned int>(static_cast<uint8_t>(c)));
          output += escaped;
        } else {
          output += c;
        }
    }
  }
  return output;
}

bool timeReached(uint32_t target) {
  return static_cast<int32_t>(millis() - target) >= 0;
}

bool isSafeOutputGpio(uint8_t gpio) {
  for (size_t i = 0; i < SAFE_OUTPUT_GPIO_COUNT; ++i) {
    if (SAFE_OUTPUT_GPIOS[i] == gpio) return true;
  }
  return false;
}

bool isSafeRs485RxGpio(uint8_t gpio) {
  for (size_t i = 0; i < SAFE_RS485_RX_GPIO_COUNT; ++i) {
    if (SAFE_RS485_RX_GPIOS[i] == gpio) return true;
  }
  return false;
}

ModbusMode selectedModbusMode() {
  return static_cast<ModbusMode>(config.modbusMode);
}

bool tcpTransportEnabled() {
  return selectedModbusMode() != ModbusMode::RTU_ONLY;
}

bool rs485TransportEnabled() {
  return selectedModbusMode() != ModbusMode::TCP_ONLY;
}

void sanitizeModbusConfig() {
  if (config.modbusMode > static_cast<uint8_t>(ModbusMode::TCP_WITH_RTU_FALLBACK)) {
    config.modbusMode = static_cast<uint8_t>(ModbusMode::TCP_ONLY);
  }
  if (config.rs485Baud < 1200 || config.rs485Baud > 115200) config.rs485Baud = ConfigDefaults::RS485_BAUD;
  if (!isSafeRs485RxGpio(config.rs485RxPin)) config.rs485RxPin = ConfigDefaults::RS485_RX_PIN;
  if (!isSafeOutputGpio(config.rs485TxPin)) config.rs485TxPin = ConfigDefaults::RS485_TX_PIN;
  if (config.rs485DePin != ConfigDefaults::RS485_AUTO_DIRECTION && !isSafeOutputGpio(config.rs485DePin)) {
    config.rs485DePin = ConfigDefaults::RS485_DE_PIN;
  }
  if (config.rs485RxPin == config.rs485TxPin
      || config.rs485RxPin == config.rs485DePin
      || config.rs485TxPin == config.rs485DePin) {
    config.rs485RxPin = ConfigDefaults::RS485_RX_PIN;
    config.rs485TxPin = ConfigDefaults::RS485_TX_PIN;
    config.rs485DePin = ConfigDefaults::RS485_DE_PIN;
  }
}

String stagePreferenceKey(size_t index, const char *suffix) {
  return "s" + String(index + 1) + suffix;
}

void setDefaultStage(size_t index) {
  SurplusStageConfig &stage = surplusStages[index];
  stage.watts = 1000;
  stage.onDelaySeconds = 30;
  stage.offDelaySeconds = 30;
  stage.enabled = false;
  stage.type = StageType::GPIO;
  stage.gpio = SAFE_OUTPUT_GPIOS[index % SAFE_OUTPUT_GPIO_COUNT];
  stage.method = ApiMethod::PUT;
  stage.onUrl = "";
  stage.offUrl = "";
  stage.onJson = "";
  stage.offJson = "";
}

void setDefaultForecastConfig() {
  config.forecastEnabled = false;
  config.forecastBypass = true;
  config.latitude = 0.0f;
  config.longitude = 0.0f;
  config.maxSoc = 100;
  config.finishBufferMinutes = 75;
  config.forecastSafetyPercent = 80;
  config.pvSystemEfficiencyPercent = 95;
  config.batteryChargeEfficiencyPercent = 95;
  config.socStepPercent = 3;
  config.minimumWriteMinutes = 20;
  config.maximumWritesPerDay = 20;
  config.forecastFetchHours = 3;
  config.forecastStaleHours = 6;
  config.learningPercent = 20;
  config.eventLoadThresholdWatts = 1800;
  for (size_t i = 0; i < PV_ARRAY_COUNT; ++i) {
    config.pvArrays[i].enabled = i == 0;
    config.pvArrays[i].name = i == 0 ? "Dach 1" : "Dach " + String(i + 1);
    config.pvArrays[i].peakKwp = i == 0 ? 10.0f : 0.0f;
    config.pvArrays[i].tiltDegrees = 30;
    config.pvArrays[i].azimuthDegrees = 0;
  }
  config.rippleEnabled = false;
  config.rippleSignalMode = 4;
  const uint8_t defaultPins[RIPPLE_INPUT_COUNT] = {25, 26, 32, 33};
  const uint8_t defaultPercents[RIPPLE_INPUT_COUNT] = {100, 60, 30, 0};
  for (size_t i = 0; i < RIPPLE_INPUT_COUNT; ++i) {
    config.rippleInputs[i].enabled = true;
    config.rippleInputs[i].gpio = defaultPins[i];
    config.rippleInputs[i].percent = defaultPercents[i];
    config.rippleInputs[i].activeLow = true;
  }
}

void sanitizeForecastConfig() {
  if (!isfinite(config.latitude) || config.latitude < -90.0f || config.latitude > 90.0f) config.latitude = 0.0f;
  if (!isfinite(config.longitude) || config.longitude < -180.0f || config.longitude > 180.0f) config.longitude = 0.0f;
  config.maxSoc = constrain(config.maxSoc, ConfigDefaults::MIN_MAX_SOC_PERCENT, 100);
  config.finishBufferMinutes = constrain(config.finishBufferMinutes, 0, 360);
  config.forecastSafetyPercent = constrain(config.forecastSafetyPercent, 40, 100);
  config.pvSystemEfficiencyPercent = constrain(config.pvSystemEfficiencyPercent, 40, 100);
  config.batteryChargeEfficiencyPercent = constrain(config.batteryChargeEfficiencyPercent, 50, 100);
  config.socStepPercent = constrain(config.socStepPercent, 1, 20);
  config.minimumWriteMinutes = constrain(config.minimumWriteMinutes, 5, 240);
  config.maximumWritesPerDay = constrain(config.maximumWritesPerDay, 1, 48);
  config.forecastFetchHours = constrain(config.forecastFetchHours, 1, 12);
  config.forecastStaleHours = constrain(config.forecastStaleHours, config.forecastFetchHours, 24);
  config.learningPercent = constrain(config.learningPercent, 1, 100);
  config.eventLoadThresholdWatts = constrain(config.eventLoadThresholdWatts, 250, 20000);
  for (size_t i = 0; i < PV_ARRAY_COUNT; ++i) {
    PvArrayConfig &array = config.pvArrays[i];
    array.name.trim();
    if (array.name.isEmpty()) array.name = "Dach " + String(i + 1);
    if (!isfinite(array.peakKwp) || array.peakKwp < 0.0f || array.peakKwp > 1000.0f) array.peakKwp = 0.0f;
    array.tiltDegrees = constrain(array.tiltDegrees, 0, 90);
    array.azimuthDegrees = constrain(array.azimuthDegrees, -180, 180);
    if (array.enabled && array.peakKwp <= 0.0f) array.enabled = false;
  }
}

void sanitizeSurplusConfig() {
  for (size_t i = 0; i < SURPLUS_STAGE_COUNT; ++i) {
    SurplusStageConfig &stage = surplusStages[i];
    if (stage.watts == 0 || stage.watts > 20000) stage.watts = 1000;
    if (stage.onDelaySeconds > 3600) stage.onDelaySeconds = 30;
    if (stage.offDelaySeconds > 3600) stage.offDelaySeconds = 30;
    if (static_cast<uint8_t>(stage.type) > static_cast<uint8_t>(StageType::API)) stage.type = StageType::GPIO;
    if (static_cast<uint8_t>(stage.method) > static_cast<uint8_t>(ApiMethod::POST)) stage.method = ApiMethod::PUT;
    if (!isSafeOutputGpio(stage.gpio)) stage.gpio = SAFE_OUTPUT_GPIOS[i % SAFE_OUTPUT_GPIO_COUNT];
    stage.onUrl.trim();
    stage.offUrl.trim();

    if (stage.enabled && stage.type == StageType::API
        && (!stage.onUrl.startsWith("http://") || !stage.offUrl.startsWith("http://"))) {
      stage.enabled = false;
    }

    if (!stage.enabled || stage.type != StageType::GPIO) continue;
    if (rs485TransportEnabled()
        && (stage.gpio == config.rs485RxPin || stage.gpio == config.rs485TxPin || stage.gpio == config.rs485DePin)) {
      stage.enabled = false;
      continue;
    }
    for (size_t previous = 0; previous < i; ++previous) {
      const SurplusStageConfig &other = surplusStages[previous];
      if (other.enabled && other.type == StageType::GPIO && other.gpio == stage.gpio) {
        stage.enabled = false;
        break;
      }
    }
  }
}

bool gpioUsedByActiveOutput(uint8_t gpio) {
  for (size_t i = 0; i < SURPLUS_STAGE_COUNT; ++i) {
    const SurplusStageConfig &stage = surplusStages[i];
    if (stage.enabled && stage.type == StageType::GPIO && stage.gpio == gpio) return true;
  }
  return false;
}

void sanitizeRippleConfig() {
  if (config.rippleSignalMode != 3 && config.rippleSignalMode != 4) config.rippleSignalMode = 4;
  bool hasUsableInput = false;
  uint8_t usableInputCount = 0;
  bool hasExplicitFullPowerInput = false;
  for (size_t i = 0; i < RIPPLE_INPUT_COUNT; ++i) {
    RippleInputConfig &input = config.rippleInputs[i];
    if (!isSafeRs485RxGpio(input.gpio)) input.gpio = SAFE_RS485_RX_GPIOS[(i + 10) % SAFE_RS485_RX_GPIO_COUNT];
    input.percent = constrain(input.percent, 0, 100);
    if (!input.enabled) continue;
    const bool conflictsWithRs485 = rs485TransportEnabled()
        && (input.gpio == config.rs485RxPin || input.gpio == config.rs485TxPin || input.gpio == config.rs485DePin);
    bool duplicateInput = false;
    for (size_t previous = 0; previous < i; ++previous) {
      if (config.rippleInputs[previous].enabled && config.rippleInputs[previous].gpio == input.gpio) {
        duplicateInput = true;
        break;
      }
    }
    if (conflictsWithRs485 || gpioUsedByActiveOutput(input.gpio) || duplicateInput) {
      input.enabled = false;
      continue;
    }
    hasUsableInput = true;
    ++usableInputCount;
    if (input.percent == 100) hasExplicitFullPowerInput = true;
  }
  // Ungueltige oder unvollstaendige Kontaktbelegungen niemals stillschweigend
  // auswerten. Im 3-Kontakt-Modus ersetzt die Ruhelage den 100-%-Kontakt; im
  // 4-Kontakt-Modus muss 100 % dagegen als eigener Eingang vorhanden sein.
  if (!hasUsableInput
      || usableInputCount != config.rippleSignalMode
      || (config.rippleSignalMode == 4 && !hasExplicitFullPowerInput)) {
    config.rippleEnabled = false;
  }
}

void loadConfig() {
  setDefaultForecastConfig();
  preferences.begin("sungrow", true);
  config.wifiSsid = preferences.getString("ssid", "");
  config.wifiPassword = preferences.getString("pass", "");
  config.modbusHost = preferences.getString("mbhost", "192.168.0.2");
  config.modbusPort = preferences.getUShort("mbport", ConfigDefaults::MODBUS_PORT);
  config.inverterUnit = preferences.getUChar("mbunit", ConfigDefaults::MODBUS_UNIT);
  config.batteryTcpUnit = preferences.getUChar("battcp", ConfigDefaults::BATTERY_TCP_UNIT);
  // Der bisherige gemeinsame Batterie-ID-Wert war fuer RS485 gedacht und wird
  // einmalig als RS485-ID uebernommen. TCP erhaelt unabhaengig davon ID 2.
  config.batteryRtuUnit = preferences.getUChar(
      "batrtu", preferences.getUChar("batunit", ConfigDefaults::BATTERY_RTU_UNIT));
  config.pollSeconds = preferences.getUShort("pollsec", ConfigDefaults::POLL_SECONDS);
  config.modbusMode = preferences.getUChar("mbmode", static_cast<uint8_t>(ModbusMode::TCP_ONLY));
  config.rs485Baud = preferences.getUInt("rbaud", ConfigDefaults::RS485_BAUD);
  config.rs485RxPin = preferences.getUChar("rrx", ConfigDefaults::RS485_RX_PIN);
  config.rs485TxPin = preferences.getUChar("rtx", ConfigDefaults::RS485_TX_PIN);
  config.rs485DePin = preferences.getUChar("rde", ConfigDefaults::RS485_DE_PIN);
  webDebugEnabled = preferences.getBool("webdebug", false);
  config.pushoverEnabled = preferences.getBool("poen", false);
  config.pushoverAppToken = preferences.getString("potoken", "");
  config.pushoverUserKey = preferences.getString("pouser", "");
  config.pushoverDevice = preferences.getString("podevice", "");
  config.pushoverFailureDelaySeconds = preferences.getUShort(
      "podelay", ConfigDefaults::PUSHOVER_FAILURE_DELAY_SECONDS);
  config.pushoverRetryMinutes = preferences.getUShort(
      "poretry", ConfigDefaults::PUSHOVER_RETRY_MINUTES);
  config.forecastEnabled = preferences.getBool("fcen", config.forecastEnabled);
  config.forecastBypass = preferences.getBool("fcbypass", config.forecastBypass);
  config.latitude = preferences.getFloat("lat", config.latitude);
  config.longitude = preferences.getFloat("lon", config.longitude);
  if (preferences.isKey("maxsoc")) {
    config.maxSoc = preferences.getUChar("maxsoc", config.maxSoc);
  } else if (preferences.isKey("normsoc")) {
    // Einmalige Übernahme des bisherigen Max-SOC aus älteren Versionen.
    config.maxSoc = preferences.getUChar("normsoc", config.maxSoc);
  } else {
    config.maxSoc = preferences.getUChar("targetsoc", config.maxSoc);
  }
  config.finishBufferMinutes = preferences.getUShort("finbuffer", config.finishBufferMinutes);
  config.forecastSafetyPercent = preferences.getUChar("fcsafe", config.forecastSafetyPercent);
  config.pvSystemEfficiencyPercent = preferences.getUChar("pveff", config.pvSystemEfficiencyPercent);
  config.batteryChargeEfficiencyPercent = preferences.getUChar("bateff", config.batteryChargeEfficiencyPercent);
  config.socStepPercent = preferences.getUChar("socstep", config.socStepPercent);
  config.minimumWriteMinutes = preferences.getUShort("writemin", config.minimumWriteMinutes);
  config.maximumWritesPerDay = preferences.getUChar("writemax", config.maximumWritesPerDay);
  config.forecastFetchHours = preferences.getUChar("fetchhrs", config.forecastFetchHours);
  config.forecastStaleHours = preferences.getUChar("stalehrs", config.forecastStaleHours);
  config.learningPercent = preferences.getUChar("learnpct", config.learningPercent);
  config.eventLoadThresholdWatts = preferences.getUShort("eventw", config.eventLoadThresholdWatts);
  forecastRestorePending = preferences.getBool("fcrestore", false);
  config.rippleEnabled = preferences.getBool("rcen", config.rippleEnabled);
  config.rippleSignalMode = preferences.getUChar("rcmode", config.rippleSignalMode);
  const bool legacyActiveLow = preferences.getBool("rclow", true);
  rippleReleasePending = preferences.getBool("rcrelease", false);
  for (size_t i = 0; i < RIPPLE_INPUT_COUNT; ++i) {
    const String prefix = "rc" + String(i + 1);
    config.rippleInputs[i].enabled = preferences.getBool((prefix + "e").c_str(), config.rippleInputs[i].enabled);
    config.rippleInputs[i].gpio = preferences.getUChar((prefix + "g").c_str(), config.rippleInputs[i].gpio);
    config.rippleInputs[i].percent = preferences.getUChar((prefix + "p").c_str(), config.rippleInputs[i].percent);
    config.rippleInputs[i].activeLow = preferences.getBool((prefix + "l").c_str(), legacyActiveLow);
  }
  for (size_t i = 0; i < PV_ARRAY_COUNT; ++i) {
    const String prefix = "pv" + String(i + 1);
    PvArrayConfig &array = config.pvArrays[i];
    array.enabled = preferences.getBool((prefix + "e").c_str(), array.enabled);
    array.name = preferences.getString((prefix + "n").c_str(), array.name);
    array.peakKwp = preferences.getFloat((prefix + "k").c_str(), array.peakKwp);
    array.tiltDegrees = preferences.getUChar((prefix + "t").c_str(), array.tiltDegrees);
    array.azimuthDegrees = preferences.getShort((prefix + "a").c_str(), array.azimuthDegrees);
  }

  for (size_t i = 0; i < SURPLUS_STAGE_COUNT; ++i) {
    setDefaultStage(i);
    SurplusStageConfig &stage = surplusStages[i];
    const String wattsKey = stagePreferenceKey(i, "w");
    if (preferences.isKey(wattsKey.c_str())) {
      stage.watts = preferences.getUShort(wattsKey.c_str(), 1000);
      stage.onDelaySeconds = preferences.getUShort(stagePreferenceKey(i, "di").c_str(), 30);
      stage.offDelaySeconds = preferences.getUShort(stagePreferenceKey(i, "do").c_str(), 30);
      stage.enabled = preferences.getBool(stagePreferenceKey(i, "e").c_str(), false);
      stage.type = static_cast<StageType>(preferences.getUChar(stagePreferenceKey(i, "t").c_str(), 0));
      stage.gpio = preferences.getUChar(stagePreferenceKey(i, "g").c_str(), stage.gpio);
      stage.method = static_cast<ApiMethod>(preferences.getUChar(stagePreferenceKey(i, "m").c_str(), 1));
      stage.onUrl = preferences.getString(stagePreferenceKey(i, "uon").c_str(), "");
      stage.offUrl = preferences.getString(stagePreferenceKey(i, "uoff").c_str(), "");
      stage.onJson = preferences.getString(stagePreferenceKey(i, "jon").c_str(), "");
      stage.offJson = preferences.getString(stagePreferenceKey(i, "joff").c_str(), "");
      continue;
    }

    // Einmalige Übernahme der Konfiguration aus der früheren 3-GPIO-plus-API-Version.
    if (i < 3) {
      const String oldPrefix = "h" + String(i + 1);
      stage.watts = preferences.getUShort((oldPrefix + "w").c_str(), 1000);
      const uint16_t oldDelay = preferences.getUShort((oldPrefix + "d").c_str(), 30);
      stage.onDelaySeconds = oldDelay;
      stage.offDelaySeconds = oldDelay;
      stage.gpio = preferences.getUChar((oldPrefix + "g").c_str(), static_cast<uint8_t>(25 + i));
      stage.enabled = preferences.getBool((oldPrefix + "e").c_str(), false);
    } else if (i == 3) {
      stage.type = StageType::API;
      stage.watts = preferences.getUShort("apiw", 1000);
      const uint16_t oldDelay = preferences.getUShort("apidelay", 30);
      stage.onDelaySeconds = oldDelay;
      stage.offDelaySeconds = oldDelay;
      stage.enabled = preferences.getBool("apien", false);
      stage.method = static_cast<ApiMethod>(preferences.getUChar("apimethod", 1));
      stage.onUrl = preferences.getString("apionurl", "");
      stage.offUrl = preferences.getString("apioffurl", "");
      stage.onJson = preferences.getString("apionjson", "");
      stage.offJson = preferences.getString("apioffjson", "");
    }
  }
  preferences.end();

  if (config.modbusPort == 0) config.modbusPort = ConfigDefaults::MODBUS_PORT;
  if (config.inverterUnit == 0) config.inverterUnit = ConfigDefaults::MODBUS_UNIT;
  if (config.batteryTcpUnit == 0) config.batteryTcpUnit = ConfigDefaults::BATTERY_TCP_UNIT;
  if (config.batteryRtuUnit == 0) config.batteryRtuUnit = ConfigDefaults::BATTERY_RTU_UNIT;
  if (config.pollSeconds < 2 || config.pollSeconds > 300) config.pollSeconds = ConfigDefaults::POLL_SECONDS;
  if (!isPushoverDevice(config.pushoverDevice)) config.pushoverDevice = "";
  if (config.pushoverFailureDelaySeconds < 10 || config.pushoverFailureDelaySeconds > 3600) {
    config.pushoverFailureDelaySeconds = ConfigDefaults::PUSHOVER_FAILURE_DELAY_SECONDS;
  }
  if (config.pushoverRetryMinutes < 1 || config.pushoverRetryMinutes > 1440) {
    config.pushoverRetryMinutes = ConfigDefaults::PUSHOVER_RETRY_MINUTES;
  }
  sanitizeModbusConfig();
  sanitizeSurplusConfig();
  sanitizeForecastConfig();
  sanitizeRippleConfig();
}

void saveConfig() {
  preferences.begin("sungrow", false);
  preferences.putString("ssid", config.wifiSsid);
  preferences.putString("pass", config.wifiPassword);
  preferences.putString("mbhost", config.modbusHost);
  preferences.putUShort("mbport", config.modbusPort);
  preferences.putUChar("mbunit", config.inverterUnit);
  preferences.putUChar("battcp", config.batteryTcpUnit);
  preferences.putUChar("batrtu", config.batteryRtuUnit);
  preferences.putUShort("pollsec", config.pollSeconds);
  preferences.putUChar("mbmode", config.modbusMode);
  preferences.putUInt("rbaud", config.rs485Baud);
  preferences.putUChar("rrx", config.rs485RxPin);
  preferences.putUChar("rtx", config.rs485TxPin);
  preferences.putUChar("rde", config.rs485DePin);
  preferences.putBool("webdebug", webDebugEnabled);
  preferences.putBool("poen", config.pushoverEnabled);
  preferences.putString("potoken", config.pushoverAppToken);
  preferences.putString("pouser", config.pushoverUserKey);
  preferences.putString("podevice", config.pushoverDevice);
  preferences.putUShort("podelay", config.pushoverFailureDelaySeconds);
  preferences.putUShort("poretry", config.pushoverRetryMinutes);
  preferences.putBool("fcen", config.forecastEnabled);
  preferences.putBool("fcbypass", config.forecastBypass);
  preferences.putFloat("lat", config.latitude);
  preferences.putFloat("lon", config.longitude);
  preferences.putUChar("maxsoc", config.maxSoc);
  preferences.putUShort("finbuffer", config.finishBufferMinutes);
  preferences.putUChar("fcsafe", config.forecastSafetyPercent);
  preferences.putUChar("pveff", config.pvSystemEfficiencyPercent);
  preferences.putUChar("bateff", config.batteryChargeEfficiencyPercent);
  preferences.putUChar("socstep", config.socStepPercent);
  preferences.putUShort("writemin", config.minimumWriteMinutes);
  preferences.putUChar("writemax", config.maximumWritesPerDay);
  preferences.putUChar("fetchhrs", config.forecastFetchHours);
  preferences.putUChar("stalehrs", config.forecastStaleHours);
  preferences.putUChar("learnpct", config.learningPercent);
  preferences.putUShort("eventw", config.eventLoadThresholdWatts);
  preferences.putBool("fcrestore", forecastRestorePending);
  preferences.putBool("rcen", config.rippleEnabled);
  preferences.putUChar("rcmode", config.rippleSignalMode);
  preferences.putBool("rcrelease", rippleReleasePending);
  for (size_t i = 0; i < RIPPLE_INPUT_COUNT; ++i) {
    const String prefix = "rc" + String(i + 1);
    const RippleInputConfig &input = config.rippleInputs[i];
    preferences.putBool((prefix + "e").c_str(), input.enabled);
    preferences.putUChar((prefix + "g").c_str(), input.gpio);
    preferences.putUChar((prefix + "p").c_str(), input.percent);
    preferences.putBool((prefix + "l").c_str(), input.activeLow);
  }
  for (size_t i = 0; i < PV_ARRAY_COUNT; ++i) {
    const String prefix = "pv" + String(i + 1);
    const PvArrayConfig &array = config.pvArrays[i];
    preferences.putBool((prefix + "e").c_str(), array.enabled);
    preferences.putString((prefix + "n").c_str(), array.name);
    preferences.putFloat((prefix + "k").c_str(), array.peakKwp);
    preferences.putUChar((prefix + "t").c_str(), array.tiltDegrees);
    preferences.putShort((prefix + "a").c_str(), array.azimuthDegrees);
  }
  for (size_t i = 0; i < SURPLUS_STAGE_COUNT; ++i) {
    const SurplusStageConfig &stage = surplusStages[i];
    preferences.putUShort(stagePreferenceKey(i, "w").c_str(), stage.watts);
    preferences.putUShort(stagePreferenceKey(i, "di").c_str(), stage.onDelaySeconds);
    preferences.putUShort(stagePreferenceKey(i, "do").c_str(), stage.offDelaySeconds);
    preferences.putBool(stagePreferenceKey(i, "e").c_str(), stage.enabled);
    preferences.putUChar(stagePreferenceKey(i, "t").c_str(), static_cast<uint8_t>(stage.type));
    preferences.putUChar(stagePreferenceKey(i, "g").c_str(), stage.gpio);
    preferences.putUChar(stagePreferenceKey(i, "m").c_str(), static_cast<uint8_t>(stage.method));
    preferences.putString(stagePreferenceKey(i, "uon").c_str(), stage.onUrl);
    preferences.putString(stagePreferenceKey(i, "uoff").c_str(), stage.offUrl);
    preferences.putString(stagePreferenceKey(i, "jon").c_str(), stage.onJson);
    preferences.putString(stagePreferenceKey(i, "joff").c_str(), stage.offJson);
  }
  preferences.end();
}

void buildAccessPointName() {
  const uint32_t suffix = static_cast<uint32_t>(ESP.getEfuseMac() & 0xFFFFFFULL);
  char text[32];
  snprintf(text, sizeof(text), "Solar-Prognose-%06lX", static_cast<unsigned long>(suffix));
  accessPointSsid = text;
}

void connectStation(uint32_t timeoutMs) {
  if (config.wifiSsid.isEmpty()) return;

  debugPrintf("Verbinde mit WLAN '%s' ...\n", config.wifiSsid.c_str());
  WiFi.begin(config.wifiSsid.c_str(), config.wifiPassword.c_str());
  lastWifiAttempt = millis();
  const uint32_t started = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - started < timeoutMs) {
    dnsServer.processNextRequest();
    delay(100);
  }

  if (WiFi.status() == WL_CONNECTED) {
    debugPrintf("WLAN verbunden, IP: %s\n", WiFi.localIP().toString().c_str());
  } else {
    debugPrintln("WLAN-Verbindung fehlgeschlagen; Access Point bleibt aktiv.");
  }
}

void startNetwork() {
  buildAccessPointName();
  WiFi.persistent(false);
  WiFi.mode(WIFI_AP_STA);
  WiFi.setAutoReconnect(true);
  WiFi.softAP(accessPointSsid.c_str(), ConfigDefaults::AP_PASSWORD);
  dnsServer.start(53, "*", WiFi.softAPIP());

  debugPrintf("Access Point: %s\n", accessPointSsid.c_str());
  debugPrintf("AP-Adresse: %s\n", WiFi.softAPIP().toString().c_str());
  connectStation(ConfigDefaults::WIFI_CONNECT_TIMEOUT_MS);
}

void initializeRs485() {
  if (!rs485TransportEnabled()) return;
  if (config.rs485DePin != ConfigDefaults::RS485_AUTO_DIRECTION) {
    pinMode(config.rs485DePin, OUTPUT);
    digitalWrite(config.rs485DePin, LOW);
  }
  modbusSerial.begin(config.rs485Baud, SERIAL_8N1, config.rs485RxPin, config.rs485TxPin);
  modbusSerial.setTimeout(ConfigDefaults::MODBUS_TIMEOUT_MS);
  debugPrintf("Modbus RTU bereit: %lu Baud, 8N1, RX GPIO %u, TX GPIO %u",
              static_cast<unsigned long>(config.rs485Baud), config.rs485RxPin, config.rs485TxPin);
  if (config.rs485DePin == ConfigDefaults::RS485_AUTO_DIRECTION) {
    debugPrintln(", automatische Richtung.");
  } else {
    debugPrintf(", DE/RE GPIO %u.\n", config.rs485DePin);
  }
}

bool readExact(WiFiClient &client, uint8_t *destination, size_t length, uint32_t timeoutMs) {
  size_t received = 0;
  const uint32_t started = millis();
  while (received < length && millis() - started < timeoutMs) {
    const int availableBytes = client.available();
    if (availableBytes > 0) {
      const size_t wanted = min(length - received, static_cast<size_t>(availableBytes));
      const int count = client.read(destination + received, wanted);
      if (count > 0) received += static_cast<size_t>(count);
    } else if (!client.connected()) {
      break;
    } else {
      delay(1);
    }
  }
  return received == length;
}

bool readExactSerial(uint8_t *destination, size_t length, uint32_t timeoutMs) {
  size_t received = 0;
  const uint32_t started = millis();
  while (received < length && millis() - started < timeoutMs) {
    const int availableBytes = modbusSerial.available();
    if (availableBytes > 0) {
      const size_t wanted = min(length - received, static_cast<size_t>(availableBytes));
      const size_t count = modbusSerial.readBytes(destination + received, wanted);
      received += count;
    } else {
      delay(1);
    }
  }
  return received == length;
}

uint16_t modbusCrc16(const uint8_t *data, size_t length) {
  uint16_t crc = 0xFFFFU;
  for (size_t i = 0; i < length; ++i) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; ++bit) {
      crc = (crc & 0x0001U) != 0 ? static_cast<uint16_t>((crc >> 1) ^ 0xA001U) : static_cast<uint16_t>(crc >> 1);
    }
  }
  return crc;
}

void invalidateBlock(SourceGroup source, uint16_t start, uint16_t count) {
  const uint32_t endExclusive = static_cast<uint32_t>(start) + count;
  for (size_t i = 0; i < REGISTER_COUNT; ++i) {
    const uint32_t registerEnd = static_cast<uint32_t>(registers[i].address) + registers[i].length;
    if (registers[i].source == source && registers[i].address >= start && registerEnd <= endExclusive) {
      registers[i].valid = false;
    }
  }
}

bool isInvalidMarker(const RegisterDef &reg, const uint16_t *words) {
  switch (reg.type) {
    case ValueType::U16: return words[0] == 0xFFFFU;
    case ValueType::S16: return words[0] == 0x7FFFU;
    case ValueType::U32_WORD_SWAPPED: return words[0] == 0xFFFFU && words[1] == 0xFFFFU;
    case ValueType::S32_WORD_SWAPPED: return words[0] == 0xFFFFU && words[1] == 0x7FFFU;
  }
  return true;
}

double decodeValue(const RegisterDef &reg, const uint16_t *words) {
  double raw = 0.0;
  switch (reg.type) {
    case ValueType::U16:
      raw = words[0];
      break;
    case ValueType::S16:
      raw = static_cast<int16_t>(words[0]);
      break;
    case ValueType::U32_WORD_SWAPPED: {
      const uint32_t number = (static_cast<uint32_t>(words[1]) << 16) | words[0];
      raw = number;
      break;
    }
    case ValueType::S32_WORD_SWAPPED: {
      const uint32_t bits = (static_cast<uint32_t>(words[1]) << 16) | words[0];
      raw = static_cast<int32_t>(bits);
      break;
    }
  }
  return raw * static_cast<double>(reg.factor) + static_cast<double>(reg.offset);
}

void decodeBlock(SourceGroup source, uint16_t start, uint16_t count, const uint16_t *words) {
  const uint32_t now = millis();
  const uint32_t endExclusive = static_cast<uint32_t>(start) + count;
  for (size_t i = 0; i < REGISTER_COUNT; ++i) {
    RegisterDef &reg = registers[i];
    if (reg.source != source) continue;
    const uint32_t registerEnd = static_cast<uint32_t>(reg.address) + reg.length;
    if (reg.address < start || registerEnd > endExclusive) continue;

    const uint16_t *rawWords = words + (reg.address - start);
    reg.valid = !isInvalidMarker(reg, rawWords);
    if (reg.valid) {
      reg.value = decodeValue(reg, rawWords);
      reg.updatedAt = now;
    }
  }
}

void applyBatteryPowerDirection() {
  RegisterDef *battery = nullptr;
  RegisterDef *pv = nullptr;
  RegisterDef *load = nullptr;
  RegisterDef *grid = nullptr;
  for (size_t i = 0; i < REGISTER_COUNT; ++i) {
    RegisterDef &reg = registers[i];
    if (reg.source != SourceGroup::INVERTER) continue;
    if (reg.address == 13021) battery = &reg;
    else if (reg.address == 5016) pv = &reg;
    else if (reg.address == 13007) load = &reg;
    else if (reg.address == 13009) grid = &reg;
  }
  if (battery == nullptr || pv == nullptr || load == nullptr || grid == nullptr
      || !battery->valid || !pv->valid || !load->valid || !grid->valid) return;

  // Einige Firmwarestaende liefern 13021 nur als vorzeichenlosen Betrag.
  // Die Richtung folgt deshalb aus der Leistungsbilanz am Wechselrichter:
  // Hausverbrauch + Netzeinspeisung - PV = + Entladen / - Laden.
  const double magnitude = fabs(battery->value);
  const double powerBalance = load->value + grid->value - pv->value;
  battery->value = powerBalance < 0.0 ? -magnitude : magnitude;
}

bool ensureModbusConnection() {
  if (modbusClient.connected()) return true;
  modbusClient.stop();
  modbusClient.setTimeout(ConfigDefaults::MODBUS_TIMEOUT_MS);
  if (!modbusClient.connect(config.modbusHost.c_str(), config.modbusPort, ConfigDefaults::MODBUS_TIMEOUT_MS)) {
    lastTransactionError = "Verbindung zu " + config.modbusHost + ":" + String(config.modbusPort) + " fehlgeschlagen";
    return false;
  }
  modbusClient.setNoDelay(true);
  return true;
}

bool readRegistersTcp(uint8_t unitId, uint8_t functionCode, uint16_t start, uint16_t count, uint16_t *words) {
  lastModbusExceptionCode = 0;
  if (count == 0 || count > 125) {
    lastTransactionError = "Ungültige Blockgröße";
    return false;
  }
  if (!ensureModbusConnection()) return false;

  ++transactionId;
  const uint8_t request[12] = {
    static_cast<uint8_t>(transactionId >> 8), static_cast<uint8_t>(transactionId),
    0, 0, 0, 6, unitId, functionCode,
    static_cast<uint8_t>(start >> 8), static_cast<uint8_t>(start),
    static_cast<uint8_t>(count >> 8), static_cast<uint8_t>(count)
  };

  if (modbusClient.write(request, sizeof(request)) != sizeof(request)) {
    lastTransactionError = "Modbus-Anfrage konnte nicht gesendet werden";
    modbusClient.stop();
    return false;
  }

  uint8_t header[9];
  if (!readExact(modbusClient, header, sizeof(header), ConfigDefaults::MODBUS_TIMEOUT_MS)) {
    lastTransactionError = "Zeitüberschreitung bei Modbus-Antwort";
    modbusClient.stop();
    return false;
  }

  const uint16_t receivedTransaction = (static_cast<uint16_t>(header[0]) << 8) | header[1];
  const uint16_t protocol = (static_cast<uint16_t>(header[2]) << 8) | header[3];
  if (receivedTransaction != transactionId || protocol != 0 || header[6] != unitId) {
    lastTransactionError = "Ungültiger Modbus-TCP-Kopf";
    modbusClient.stop();
    return false;
  }
  if (header[7] == static_cast<uint8_t>(functionCode | 0x80U)) {
    lastModbusExceptionCode = header[8];
    lastTransactionError = "Modbus-Ausnahme 0x" + String(header[8], HEX) + " bei Adresse " + String(start);
    modbusClient.stop();
    return false;
  }
  if (header[7] != functionCode || header[8] != count * 2) {
    lastTransactionError = "Unerwartete Modbus-Antwort bei Adresse " + String(start);
    modbusClient.stop();
    return false;
  }

  uint8_t payload[250];
  const size_t payloadLength = static_cast<size_t>(count) * 2;
  if (!readExact(modbusClient, payload, payloadLength, ConfigDefaults::MODBUS_TIMEOUT_MS)) {
    lastTransactionError = "Unvollständige Modbus-Daten";
    modbusClient.stop();
    return false;
  }
  for (uint16_t i = 0; i < count; ++i) {
    words[i] = (static_cast<uint16_t>(payload[i * 2]) << 8) | payload[i * 2 + 1];
  }
  lastModbusTransport = "TCP";
  return true;
}

bool readRegistersRtu(uint8_t unitId, uint8_t functionCode, uint16_t start, uint16_t count, uint16_t *words) {
  lastModbusExceptionCode = 0;
  if (count == 0 || count > 125) {
    lastTransactionError = "Ungültige RTU-Blockgröße";
    return false;
  }

  while (modbusSerial.available() > 0) modbusSerial.read();
  const uint32_t silentIntervalUs = max(1750UL, 35000000UL / config.rs485Baud);
  delayMicroseconds(silentIntervalUs);  // Modbus-RTU-Ruhezeit von mindestens 3,5 Zeichen
  uint8_t request[8] = {
    unitId, functionCode,
    static_cast<uint8_t>(start >> 8), static_cast<uint8_t>(start),
    static_cast<uint8_t>(count >> 8), static_cast<uint8_t>(count),
    0, 0
  };
  const uint16_t requestCrc = modbusCrc16(request, 6);
  request[6] = static_cast<uint8_t>(requestCrc);
  request[7] = static_cast<uint8_t>(requestCrc >> 8);

  if (config.rs485DePin != ConfigDefaults::RS485_AUTO_DIRECTION) {
    digitalWrite(config.rs485DePin, HIGH);
    delayMicroseconds(100);
  }
  const size_t sent = modbusSerial.write(request, sizeof(request));
  modbusSerial.flush();
  if (config.rs485DePin != ConfigDefaults::RS485_AUTO_DIRECTION) {
    delayMicroseconds(100);
    digitalWrite(config.rs485DePin, LOW);
  }
  if (sent != sizeof(request)) {
    lastTransactionError = "Modbus-RTU-Anfrage konnte nicht gesendet werden";
    return false;
  }

  uint8_t response[255];
  if (!readExactSerial(response, 3, ConfigDefaults::MODBUS_TIMEOUT_MS)) {
    lastTransactionError = "Zeitüberschreitung bei Modbus-RTU-Antwort";
    return false;
  }
  if (response[0] != unitId) {
    lastTransactionError = "Unerwartete Unit-ID in Modbus-RTU-Antwort";
    return false;
  }

  if (response[1] == static_cast<uint8_t>(functionCode | 0x80U)) {
    if (!readExactSerial(response + 3, 2, ConfigDefaults::MODBUS_TIMEOUT_MS)) {
      lastTransactionError = "Unvollständige Modbus-RTU-Ausnahme";
      return false;
    }
    const uint16_t receivedCrc = static_cast<uint16_t>(response[3]) | (static_cast<uint16_t>(response[4]) << 8);
    if (modbusCrc16(response, 3) != receivedCrc) {
      lastTransactionError = "CRC-Fehler in Modbus-RTU-Ausnahme";
      return false;
    }
    lastModbusExceptionCode = response[2];
    lastTransactionError = "Modbus-RTU-Ausnahme 0x" + String(response[2], HEX) + " bei Adresse " + String(start);
    return false;
  }
  if (response[1] != functionCode || response[2] != count * 2) {
    lastTransactionError = "Unerwartete Modbus-RTU-Antwort bei Adresse " + String(start);
    return false;
  }

  const size_t dataLength = response[2];
  const size_t remainingLength = dataLength + 2;
  if (!readExactSerial(response + 3, remainingLength, ConfigDefaults::MODBUS_TIMEOUT_MS)) {
    lastTransactionError = "Unvollständige Modbus-RTU-Daten";
    return false;
  }
  const size_t frameLength = 3 + remainingLength;
  const uint16_t receivedCrc = static_cast<uint16_t>(response[frameLength - 2])
                             | (static_cast<uint16_t>(response[frameLength - 1]) << 8);
  if (modbusCrc16(response, frameLength - 2) != receivedCrc) {
    lastTransactionError = "CRC-Fehler in Modbus-RTU-Antwort";
    return false;
  }
  for (uint16_t i = 0; i < count; ++i) {
    words[i] = (static_cast<uint16_t>(response[3 + i * 2]) << 8) | response[4 + i * 2];
  }
  lastModbusTransport = "RS485 / Modbus RTU";
  return true;
}

bool readInputRegistersTcp(uint8_t unitId, uint16_t start, uint16_t count, uint16_t *words) {
  return readRegistersTcp(unitId, 0x04, start, count, words);
}

bool readInputRegistersRtu(uint8_t unitId, uint16_t start, uint16_t count, uint16_t *words) {
  return readRegistersRtu(unitId, 0x04, start, count, words);
}

bool readHoldingRegistersTcp(uint8_t unitId, uint16_t start, uint16_t count, uint16_t *words) {
  return readRegistersTcp(unitId, 0x03, start, count, words);
}

bool readHoldingRegistersRtu(uint8_t unitId, uint16_t start, uint16_t count, uint16_t *words) {
  return readRegistersRtu(unitId, 0x03, start, count, words);
}

bool writeSingleRegisterTcp(uint8_t unitId, uint16_t address, uint16_t value) {
  if (!ensureModbusConnection()) return false;
  ++transactionId;
  const uint8_t request[12] = {
    static_cast<uint8_t>(transactionId >> 8), static_cast<uint8_t>(transactionId),
    0, 0, 0, 6, unitId, 0x06,
    static_cast<uint8_t>(address >> 8), static_cast<uint8_t>(address),
    static_cast<uint8_t>(value >> 8), static_cast<uint8_t>(value)
  };
  if (modbusClient.write(request, sizeof(request)) != sizeof(request)) {
    lastTransactionError = "Modbus-Schreibanfrage konnte nicht gesendet werden";
    modbusClient.stop();
    return false;
  }
  uint8_t response[12];
  if (!readExact(modbusClient, response, sizeof(response), ConfigDefaults::MODBUS_TIMEOUT_MS)) {
    lastTransactionError = "Zeitüberschreitung beim Modbus-Schreiben";
    modbusClient.stop();
    return false;
  }
  const uint16_t receivedTransaction = (static_cast<uint16_t>(response[0]) << 8) | response[1];
  const uint16_t echoedAddress = (static_cast<uint16_t>(response[8]) << 8) | response[9];
  const uint16_t echoedValue = (static_cast<uint16_t>(response[10]) << 8) | response[11];
  if (receivedTransaction != transactionId || response[6] != unitId || response[7] != 0x06
      || echoedAddress != address || echoedValue != value) {
    lastTransactionError = "Ungültige Bestätigung des Modbus-Schreibzugriffs";
    modbusClient.stop();
    return false;
  }
  lastModbusTransport = "TCP";
  return true;
}

bool writeSingleRegisterRtu(uint8_t unitId, uint16_t address, uint16_t value) {
  while (modbusSerial.available() > 0) modbusSerial.read();
  const uint32_t silentIntervalUs = max(1750UL, 35000000UL / config.rs485Baud);
  delayMicroseconds(silentIntervalUs);
  uint8_t frame[8] = {
    unitId, 0x06,
    static_cast<uint8_t>(address >> 8), static_cast<uint8_t>(address),
    static_cast<uint8_t>(value >> 8), static_cast<uint8_t>(value), 0, 0
  };
  const uint16_t crc = modbusCrc16(frame, 6);
  frame[6] = static_cast<uint8_t>(crc);
  frame[7] = static_cast<uint8_t>(crc >> 8);
  if (config.rs485DePin != ConfigDefaults::RS485_AUTO_DIRECTION) {
    digitalWrite(config.rs485DePin, HIGH);
    delayMicroseconds(100);
  }
  const size_t sent = modbusSerial.write(frame, sizeof(frame));
  modbusSerial.flush();
  if (config.rs485DePin != ConfigDefaults::RS485_AUTO_DIRECTION) {
    delayMicroseconds(100);
    digitalWrite(config.rs485DePin, LOW);
  }
  if (sent != sizeof(frame)) {
    lastTransactionError = "Modbus-RTU-Schreibanfrage konnte nicht gesendet werden";
    return false;
  }
  uint8_t response[8];
  if (!readExactSerial(response, sizeof(response), ConfigDefaults::MODBUS_TIMEOUT_MS)) {
    lastTransactionError = "Zeitüberschreitung beim Modbus-RTU-Schreiben";
    return false;
  }
  const uint16_t receivedCrc = static_cast<uint16_t>(response[6]) | (static_cast<uint16_t>(response[7]) << 8);
  if (modbusCrc16(response, 6) != receivedCrc || memcmp(response, frame, 6) != 0) {
    lastTransactionError = "Ungültige Modbus-RTU-Schreibbestätigung";
    return false;
  }
  lastModbusTransport = "RS485 / Modbus RTU";
  return true;
}

bool readInputRegistersConfigured(uint8_t unitId, uint16_t start, uint16_t count, uint16_t *words) {
  const ModbusMode mode = selectedModbusMode();
  if (mode == ModbusMode::TCP_ONLY) return readInputRegistersTcp(unitId, start, count, words);
  if (mode == ModbusMode::RTU_ONLY) return readInputRegistersRtu(unitId, start, count, words);
  if (rtuFallbackActive) return readInputRegistersRtu(unitId, start, count, words);

  String tcpError;
  if (WiFi.status() == WL_CONNECTED && !config.modbusHost.isEmpty()) {
    if (readInputRegistersTcp(unitId, start, count, words)) return true;
    tcpError = lastTransactionError;
  } else {
    tcpError = "TCP nicht verfügbar";
  }
  rtuFallbackActive = true;
  if (readInputRegistersRtu(unitId, start, count, words)) {
    debugPrintf("TCP fehlgeschlagen; restlicher Zyklus wird ab Block %u über RS485 gelesen.\n", start);
    return true;
  }
  lastTransactionError = "TCP: " + tcpError + " | RS485: " + lastTransactionError;
  return false;
}

enum class OptionalReadResult : uint8_t {
  OK,
  FAILED,
  UNSUPPORTED
};

OptionalReadResult readOptionalInputRegistersConfigured(uint8_t unitId, uint16_t start,
                                                        uint16_t count, uint16_t *words) {
  const ModbusMode mode = selectedModbusMode();
  if (mode == ModbusMode::TCP_ONLY) {
    if (readInputRegistersTcp(unitId, start, count, words)) return OptionalReadResult::OK;
    return lastModbusExceptionCode == 0x02 ? OptionalReadResult::UNSUPPORTED
                                           : OptionalReadResult::FAILED;
  }
  if (mode == ModbusMode::RTU_ONLY || rtuFallbackActive) {
    if (readInputRegistersRtu(unitId, start, count, words)) return OptionalReadResult::OK;
    return lastModbusExceptionCode == 0x02 ? OptionalReadResult::UNSUPPORTED
                                           : OptionalReadResult::FAILED;
  }

  String tcpError = "TCP nicht verfügbar";
  uint8_t tcpException = 0;
  if (WiFi.status() == WL_CONNECTED && !config.modbusHost.isEmpty()) {
    if (readInputRegistersTcp(unitId, start, count, words)) return OptionalReadResult::OK;
    tcpError = lastTransactionError;
    tcpException = lastModbusExceptionCode;
  }

  // Eine illegale optionale Adresse darf den restlichen Abfragezyklus nicht
  // dauerhaft auf RS485 umschalten. RS485 wird fuer diesen Block trotzdem
  // einmal versucht, falls der Zaehler dort erreichbar ist.
  if (tcpException == 0x02) {
    if (readInputRegistersRtu(unitId, start, count, words)) return OptionalReadResult::OK;
    const String rtuError = lastTransactionError;
    lastTransactionError = "TCP: " + tcpError + " | RS485: " + rtuError;
    // TCP hat bereits eindeutig gemeldet, dass diese Adresse nicht existiert.
    // Ein ausgefallener zweiter Transport soll nicht zu Wiederholungen und
    // einem unruhigen Zyklus fuehren.
    return OptionalReadResult::UNSUPPORTED;
  }

  // Bei echten Transportfehlern bleibt das normale Verhalten erhalten: Der
  // restliche Zyklus darf ueber RS485 weiterlaufen.
  rtuFallbackActive = true;
  if (readInputRegistersRtu(unitId, start, count, words)) return OptionalReadResult::OK;
  const String rtuError = lastTransactionError;
  const bool rtuUnsupported = lastModbusExceptionCode == 0x02;
  lastTransactionError = "TCP: " + tcpError + " | RS485: " + rtuError;
  return rtuUnsupported ? OptionalReadResult::UNSUPPORTED : OptionalReadResult::FAILED;
}

bool readBatteryInputRegisters(uint16_t start, uint16_t count, uint16_t *words) {
  const ModbusMode mode = selectedModbusMode();
  if (mode == ModbusMode::TCP_ONLY) {
    return readInputRegistersTcp(config.batteryTcpUnit, start, count, words);
  }
  if (mode == ModbusMode::RTU_ONLY) {
    return readInputRegistersRtu(config.batteryRtuUnit, start, count, words);
  }
  if (rtuFallbackActive) {
    return readInputRegistersRtu(config.batteryRtuUnit, start, count, words);
  }

  String tcpError = "TCP nicht verfügbar";
  if (WiFi.status() == WL_CONNECTED && !config.modbusHost.isEmpty()) {
    if (readInputRegistersTcp(config.batteryTcpUnit, start, count, words)) return true;
    tcpError = lastTransactionError;
  }

  rtuFallbackActive = true;
  if (readInputRegistersRtu(config.batteryRtuUnit, start, count, words)) {
    debugPrintf("Batterie TCP-ID %u fehlgeschlagen; RS485-ID %u wird verwendet.\n",
                config.batteryTcpUnit, config.batteryRtuUnit);
    return true;
  }
  lastTransactionError = "Batterie TCP-ID " + String(config.batteryTcpUnit) + ": " + tcpError
                       + " | RS485-ID " + String(config.batteryRtuUnit) + ": " + lastTransactionError;
  return false;
}

bool readHoldingRegistersConfigured(uint16_t start, uint16_t count, uint16_t *words) {
  const ModbusMode mode = selectedModbusMode();
  if (mode == ModbusMode::TCP_ONLY) return readHoldingRegistersTcp(config.inverterUnit, start, count, words);
  if (mode == ModbusMode::RTU_ONLY) return readHoldingRegistersRtu(config.inverterUnit, start, count, words);
  String tcpError = "TCP nicht verfügbar";
  if (WiFi.status() == WL_CONNECTED && !config.modbusHost.isEmpty()) {
    if (readHoldingRegistersTcp(config.inverterUnit, start, count, words)) return true;
    tcpError = lastTransactionError;
  }
  if (readHoldingRegistersRtu(config.inverterUnit, start, count, words)) return true;
  lastTransactionError = "TCP: " + tcpError + " | RS485: " + lastTransactionError;
  return false;
}

bool writeHoldingRegisterConfigured(uint16_t address, uint16_t value) {
  const ModbusMode mode = selectedModbusMode();
  if (mode == ModbusMode::TCP_ONLY) return writeSingleRegisterTcp(config.inverterUnit, address, value);
  if (mode == ModbusMode::RTU_ONLY) return writeSingleRegisterRtu(config.inverterUnit, address, value);
  String tcpError = "TCP nicht verfügbar";
  if (WiFi.status() == WL_CONNECTED && !config.modbusHost.isEmpty()) {
    if (writeSingleRegisterTcp(config.inverterUnit, address, value)) return true;
    tcpError = lastTransactionError;
  }
  if (writeSingleRegisterRtu(config.inverterUnit, address, value)) return true;
  lastTransactionError = "TCP: " + tcpError + " | RS485: " + lastTransactionError;
  return false;
}

uint32_t configuredPvPeakWatts() {
  double totalWatts = 0.0;
  for (size_t i = 0; i < PV_ARRAY_COUNT; ++i) {
    if (config.pvArrays[i].enabled) totalWatts += static_cast<double>(config.pvArrays[i].peakKwp) * 1000.0;
  }
  return totalWatts <= 0.0 ? 0U : static_cast<uint32_t>(lround(totalWatts));
}

uint8_t activeRipplePercent() {
  uint8_t selected = 255;
  for (size_t i = 0; i < RIPPLE_INPUT_COUNT; ++i) {
    const RippleInputConfig &input = config.rippleInputs[i];
    if (!input.enabled) continue;
    const bool high = digitalRead(input.gpio) == HIGH;
    const bool active = input.activeLow ? !high : high;
    if (active) selected = min(selected, input.percent);
  }
  if (selected == 255 && config.rippleSignalMode == 3) return 100;
  return selected;
}

void initializeRippleInputs() {
  for (size_t i = 0; i < RIPPLE_INPUT_COUNT; ++i) {
    const RippleInputConfig &input = config.rippleInputs[i];
    if (!input.enabled) continue;
    if (input.gpio >= 34) {
      pinMode(input.gpio, INPUT);  // GPIO 34–39 besitzen keine internen Pull-Widerstände.
    } else {
      pinMode(input.gpio, input.activeLow ? INPUT_PULLUP : INPUT_PULLDOWN);
    }
  }
  rippleCandidatePercent = activeRipplePercent();
  rippleSelectedPercent = 255;
  rippleCandidateSince = millis();
  nextRippleActionAt = millis();
  nextRippleReadAt = millis();
}

bool readExportLimitHoldingValues() {
  uint16_t watts = 0;
  uint16_t enabled = 0;
  if (!readHoldingRegistersConfigured(ConfigDefaults::EXPORT_LIMIT_WATTS_HOLDING_ADDRESS, 1, &watts)) return false;
  if (!readHoldingRegistersConfigured(ConfigDefaults::EXPORT_LIMIT_ENABLE_HOLDING_ADDRESS, 1, &enabled)) return false;
  holdingExportLimitWatts = watts;
  holdingExportLimitEnabledRaw = enabled;
  holdingExportLimitValid = true;
  holdingExportLimitUpdatedAt = millis();
  return true;
}

bool writeAndVerifyHolding(uint16_t address, uint16_t requested) {
  if (!writeHoldingRegisterConfigured(address, requested)) return false;
  delay(80);
  uint16_t verify = 0;
  if (!readHoldingRegistersConfigured(address, 1, &verify) || verify != requested) {
    lastTransactionError = "Rundsteuer-Schreibwert an Adresse " + String(address) + " konnte nicht verifiziert werden";
    return false;
  }
  return true;
}

void persistRippleReleasePending(bool pending) {
  rippleReleasePending = pending;
  preferences.begin("sungrow", false);
  preferences.putBool("rcrelease", pending);
  preferences.end();
}

void serviceRippleControl() {
  if (pollingCycleActive) return;
  const uint32_t now = millis();

  if (!config.rippleEnabled) {
    rippleSelectedPercent = 255;
    if (rippleReleasePending && timeReached(nextRippleActionAt)) {
      nextRippleActionAt = now + ConfigDefaults::RIPPLE_RETRY_MS;
      if (writeAndVerifyHolding(ConfigDefaults::EXPORT_LIMIT_ENABLE_HOLDING_ADDRESS,
                                ConfigDefaults::EXPORT_LIMIT_DISABLED_RAW)) {
        holdingExportLimitEnabledRaw = ConfigDefaults::EXPORT_LIMIT_DISABLED_RAW;
        holdingExportLimitValid = true;
        holdingExportLimitUpdatedAt = now;
        persistRippleReleasePending(false);
        rippleStatus = "Rundsteuerung ausgeschaltet; Limitierung einmalig deaktiviert und geprüft";
        debugPrintln(rippleStatus);
      } else {
        rippleStatus = "Rundsteuerung: Deaktivierung fehlgeschlagen – " + lastTransactionError;
      }
      return;
    }
    if (!rippleReleasePending) rippleStatus = "Rundsteuerung deaktiviert";
  } else {
    const uint8_t candidate = activeRipplePercent();
    if (candidate != rippleCandidatePercent) {
      rippleCandidatePercent = candidate;
      rippleCandidateSince = now;
    }
    if (candidate != rippleSelectedPercent && now - rippleCandidateSince >= ConfigDefaults::RIPPLE_DEBOUNCE_MS) {
      rippleSelectedPercent = candidate;
      nextRippleActionAt = now;
    }

    if (rippleSelectedPercent == 255) {
      rippleStatus = "Rundsteuerung bereit; kein Eingang aktiv – Wechselrichterwert bleibt unverändert";
    } else if (timeReached(nextRippleActionAt)) {
      nextRippleActionAt = now + ConfigDefaults::RIPPLE_RETRY_MS;
      const uint32_t installedWatts = configuredPvPeakWatts();
      if (installedWatts == 0 || installedWatts > UINT16_MAX) {
        rippleStatus = installedWatts == 0
            ? "Rundsteuerung: keine aktive Dachleistung konfiguriert"
            : "Rundsteuerung: installierte Leistung übersteigt die 65.535-W-Registergrenze";
        return;
      }
      const uint16_t targetWatts = static_cast<uint16_t>((installedWatts * rippleSelectedPercent + 50U) / 100U);
      bool currentValuesOk = readExportLimitHoldingValues();
      if (!currentValuesOk
          || holdingExportLimitWatts != targetWatts
          || holdingExportLimitEnabledRaw != ConfigDefaults::EXPORT_LIMIT_ENABLED_RAW) {
        if (!writeAndVerifyHolding(ConfigDefaults::EXPORT_LIMIT_WATTS_HOLDING_ADDRESS, targetWatts)
            || !writeAndVerifyHolding(ConfigDefaults::EXPORT_LIMIT_ENABLE_HOLDING_ADDRESS,
                                      ConfigDefaults::EXPORT_LIMIT_ENABLED_RAW)) {
          holdingExportLimitValid = false;
          rippleStatus = "Rundsteuerung: Schreiben fehlgeschlagen – " + lastTransactionError;
          return;
        }
        holdingExportLimitWatts = targetWatts;
        holdingExportLimitEnabledRaw = ConfigDefaults::EXPORT_LIMIT_ENABLED_RAW;
        holdingExportLimitValid = true;
        holdingExportLimitUpdatedAt = now;
        debugPrintf("Rundsteuerung: %u %% -> %u W; Adressen 13073/13086 geschrieben und geprüft.\n",
                    rippleSelectedPercent, targetWatts);
      }
      rippleStatus = "Rundsteuerung aktiv: " + String(rippleSelectedPercent) + " % = "
                   + String(targetWatts) + " W";
      nextRippleReadAt = now + ConfigDefaults::RIPPLE_READ_INTERVAL_MS;
      return;
    }
  }

  if (timeReached(nextRippleReadAt)) {
    nextRippleReadAt = now + ConfigDefaults::RIPPLE_READ_INTERVAL_MS;
    if (!readExportLimitHoldingValues()) holdingExportLimitValid = false;
  }
}

String unitLabelFor(SourceGroup source) {
  if (source == SourceGroup::INVERTER) return "ID " + String(config.inverterUnit);
  if (selectedModbusMode() == ModbusMode::TCP_ONLY) return "TCP-ID " + String(config.batteryTcpUnit);
  if (selectedModbusMode() == ModbusMode::RTU_ONLY) return "RS485-ID " + String(config.batteryRtuUnit);
  return "TCP-ID " + String(config.batteryTcpUnit) + " / RS485-ID " + String(config.batteryRtuUnit);
}

void finishPollingCycle() {
  const uint32_t now = millis();
  applyBatteryPowerDirection();
  if (inverterCycleOk) {
    lastSuccessfulInverterCycle = now;
    lastInverterError = "OK";
  }
  if (batteryCycleOk) {
    lastSuccessfulBatteryCycle = now;
    lastBatteryError = "OK";
  }
  pollingCycleActive = false;
  nextPollAt = now + static_cast<uint32_t>(config.pollSeconds) * 1000UL;
  observePushoverModbusState(inverterCycleOk);
  debugPrintf("Modbus-Zyklus beendet: Wechselrichter %s, Batterie %s; letzter Transport: %s.\n",
              inverterCycleOk ? "OK" : "Fehler", batteryCycleOk ? "OK" : "Fehler", lastModbusTransport.c_str());
}

void servicePolling() {
  const bool tcpReady = tcpTransportEnabled() && WiFi.status() == WL_CONNECTED && !config.modbusHost.isEmpty();
  const bool rtuReady = rs485TransportEnabled();
  if (!tcpReady && !rtuReady) return;

  if (!pollingCycleActive) {
    if (!timeReached(nextPollAt)) return;
    pollingCycleActive = true;
    activeBlock = 0;
    inverterCycleOk = true;
    batteryCycleOk = true;
    rtuFallbackActive = false;
  }

  if (activeBlock >= READ_BLOCK_COUNT) {
    finishPollingCycle();
    return;
  }

  const ReadBlock &block = readBlocks[activeBlock];
  if (block.optional && readBlockUnsupported[activeBlock]) {
    ++activeBlock;
    if (activeBlock >= READ_BLOCK_COUNT) finishPollingCycle();
    return;
  }

  uint16_t words[125];
  OptionalReadResult optionalResult = OptionalReadResult::FAILED;
  bool readOk = false;
  if (block.optional) {
    optionalResult = readOptionalInputRegistersConfigured(config.inverterUnit, block.start,
                                                          block.count, words);
    readOk = optionalResult == OptionalReadResult::OK;
  } else {
    readOk = block.source == SourceGroup::BATTERY
        ? readBatteryInputRegisters(block.start, block.count, words)
        : readInputRegistersConfigured(config.inverterUnit, block.start, block.count, words);
  }
  if (!readOk) {
    invalidateBlock(block.source, block.start, block.count);
    if (block.optional) {
      if (optionalResult == OptionalReadResult::UNSUPPORTED) {
        readBlockUnsupported[activeBlock] = true;
        debugPrintf("Optionaler Modbus-Block %u-%u wird nicht unterstuetzt und bis zum Neustart uebersprungen.\n",
                    static_cast<unsigned int>(block.start),
                    static_cast<unsigned int>(block.start + block.count - 1));
      } else {
        debugPrintf("Optionaler Modbus-Block %u-%u voruebergehend nicht lesbar: %s\n",
                    static_cast<unsigned int>(block.start),
                    static_cast<unsigned int>(block.start + block.count - 1), lastTransactionError.c_str());
      }
    } else if (block.source == SourceGroup::BATTERY) {
      debugPrintf("Modbus-Fehler (%s): %s\n", unitLabelFor(block.source).c_str(), lastTransactionError.c_str());
      batteryCycleOk = false;
      lastBatteryError = lastTransactionError;
    } else {
      debugPrintf("Modbus-Fehler (%s): %s\n", unitLabelFor(block.source).c_str(), lastTransactionError.c_str());
      inverterCycleOk = false;
      lastInverterError = lastTransactionError;
    }
    ++activeBlock;
    if (activeBlock >= READ_BLOCK_COUNT) finishPollingCycle();
    return;
  }

  decodeBlock(block.source, block.start, block.count, words);
  if (block.source == SourceGroup::BATTERY) lastBatteryTransport = lastModbusTransport;
  ++activeBlock;
  if (activeBlock >= READ_BLOCK_COUNT) {
    finishPollingCycle();
  }
}

bool registerIsUnsupported(const RegisterDef &reg) {
  for (size_t i = 0; i < READ_BLOCK_COUNT; ++i) {
    const ReadBlock &block = readBlocks[i];
    if (!block.optional || !readBlockUnsupported[i] || block.source != reg.source) continue;
    const uint32_t blockEnd = static_cast<uint32_t>(block.start) + block.count;
    const uint32_t registerEnd = static_cast<uint32_t>(reg.address) + reg.length;
    if (reg.address >= block.start && registerEnd <= blockEnd) return true;
  }
  return false;
}

String formattedValue(const RegisterDef &reg) {
  if (!reg.valid) return String();
  return String(reg.value, static_cast<unsigned int>(reg.decimals));
}

RegisterDef *findRegister(SourceGroup source, uint16_t address) {
  for (size_t i = 0; i < REGISTER_COUNT; ++i) {
    if (registers[i].source == source && registers[i].address == address) return &registers[i];
  }
  return nullptr;
}

bool registerIsFresh(const RegisterDef *reg, uint32_t maximumAgeMs = 30000UL) {
  return reg != nullptr && reg->valid && reg->updatedAt != 0 && millis() - reg->updatedAt <= maximumAgeMs;
}

float currentBatterySoc() {
  RegisterDef *direct = findRegister(SourceGroup::BATTERY, 10743);
  const uint32_t maximumAgeMs = max<uint32_t>(30000UL,
      static_cast<uint32_t>(config.pollSeconds) * 2000UL + 5000UL);
  // 13022 der Wechselrichter-ID kann den Ladezustand relativ zum aktuell
  // freigegebenen SOC-Fenster darstellen. Fuer die Regelung darf deshalb nur
  // der direkte Batterie-SOC 10743 von TCP-ID 2 bzw. RS485-ID 200 dienen.
  return registerIsFresh(direct, maximumAgeMs) ? static_cast<float>(direct->value) : NAN;
}

float configuredBatteryCapacityKwh() {
  RegisterDef *capacity = findRegister(SourceGroup::INVERTER, 13038);
  return registerIsFresh(capacity, 120000UL) ? static_cast<float>(capacity->value) : NAN;
}

int32_t measuredLoadPower() {
  RegisterDef *load = findRegister(SourceGroup::INVERTER, 13007);
  return registerIsFresh(load) ? max<int32_t>(0, static_cast<int32_t>(load->value)) : -1;
}

uint32_t controlledStagePowerWatts() {
  uint32_t result = 0;
  for (size_t i = 0; i < SURPLUS_STAGE_COUNT; ++i) {
    if (surplusStages[i].enabled && stageIsOn[i]) result += surplusStages[i].watts;
  }
  return result;
}

bool clockIsValid() {
  return time(nullptr) > 1700000000;
}

bool localCalendar(time_t epoch, tm &parts) {
  if (epoch <= 0) return false;
  const time_t shifted = epoch + forecast.utcOffsetSeconds;
  return gmtime_r(&shifted, &parts) != nullptr;
}

uint8_t profileSlotFor(time_t epoch) {
  tm parts = {};
  if (!localCalendar(epoch, parts)) return 0;
  return static_cast<uint8_t>(parts.tm_hour * 4 + parts.tm_min / 15);
}

uint8_t profileDayFor(time_t epoch) {
  tm parts = {};
  if (!localCalendar(epoch, parts)) return 0;
  return static_cast<uint8_t>(parts.tm_wday);
}

uint32_t crc32Bytes(const uint8_t *data, size_t length) {
  uint32_t crc = 0xFFFFFFFFUL;
  for (size_t i = 0; i < length; ++i) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; ++bit) {
      crc = (crc & 1U) ? (crc >> 1) ^ 0xEDB88320UL : crc >> 1;
    }
  }
  return ~crc;
}

uint32_t loadProfileCrcFor(const LoadProfileData &data) {
  return crc32Bytes(reinterpret_cast<const uint8_t *>(&data), offsetof(LoadProfileData, crc));
}

uint32_t loadProfileCrc() {
  return loadProfileCrcFor(loadProfile);
}

bool validLoadProfileBlob(const LoadProfileData &data) {
  return data.magic == LOAD_PROFILE_MAGIC
      && data.version == LOAD_PROFILE_VERSION
      && data.crc == loadProfileCrcFor(data);
}

bool ensureProfileStorage() {
  esp_err_t result = nvs_flash_init_partition(ConfigDefaults::PROFILE_PARTITION_LABEL);
  if (result == ESP_ERR_NVS_NO_FREE_PAGES || result == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    if (nvs_flash_erase_partition(ConfigDefaults::PROFILE_PARTITION_LABEL) != ESP_OK) return false;
    result = nvs_flash_init_partition(ConfigDefaults::PROFILE_PARTITION_LABEL);
  }
  return result == ESP_OK;
}

bool readStoredProfileFrom(const char *partitionLabel, LoadProfileData &target) {
  if (!preferences.begin("sgprofile", true, partitionLabel)) return false;
  const size_t storedSize = preferences.getBytesLength("profile");
  const size_t bytesRead = storedSize == sizeof(target)
      ? preferences.getBytes("profile", &target, sizeof(target))
      : 0;
  preferences.end();
  return bytesRead == sizeof(target) && validLoadProfileBlob(target);
}

bool saveStoredProfile();

uint32_t historyFileHeaderCrc(const HistoryFileHeader &header) {
  return crc32Bytes(reinterpret_cast<const uint8_t *>(&header), offsetof(HistoryFileHeader, crc));
}

bool validHistoryFileHeader(const HistoryFileHeader &header) {
  return header.magic == HISTORY_FILE_MAGIC
      && header.version == HISTORY_FILE_VERSION
      && header.sampleSize == sizeof(HistorySample)
      && header.crc == historyFileHeaderCrc(header);
}

uint32_t historyDataCrcFor(const HistoryData &data) {
  return crc32Bytes(reinterpret_cast<const uint8_t *>(&data), offsetof(HistoryData, crc));
}

bool validLegacyHistoryBlob(const HistoryData &data) {
  return data.magic == HISTORY_MAGIC
      && data.version == HISTORY_VERSION
      && data.writeIndex < HISTORY_POINT_COUNT
      && data.count <= HISTORY_POINT_COUNT
      && data.crc == historyDataCrcFor(data);
}

void resetHistory() {
  memset(&historyData, 0, sizeof(historyData));
  historyData.magic = HISTORY_MAGIC;
  historyData.version = HISTORY_VERSION;
  historyDirty = false;
  lastHistorySampleSlot = UINT32_MAX;
}

String historyFilePath(uint32_t epoch) {
  tm parts = {};
  const time_t value = static_cast<time_t>(epoch);
  if (epoch == 0 || gmtime_r(&value, &parts) == nullptr) return String();
  char path[32];
  snprintf(path, sizeof(path), "%s/%04d%02d%02d.bin", ConfigDefaults::HISTORY_DIRECTORY,
           parts.tm_year + 1900, parts.tm_mon + 1, parts.tm_mday);
  return String(path);
}

size_t collectHistoryFiles(String *names, size_t maximum) {
  if (!historyStorageReady || names == nullptr || maximum == 0) return 0;
  File root = LittleFS.open(ConfigDefaults::HISTORY_DIRECTORY);
  if (!root || !root.isDirectory()) return 0;
  size_t count = 0;
  File file = root.openNextFile();
  while (file && count < maximum) {
    if (!file.isDirectory()) {
      String name = file.name();
      if (!name.startsWith("/")) name = String(ConfigDefaults::HISTORY_DIRECTORY) + "/" + name;
      if (name.endsWith(".bin")) names[count++] = name;
    }
    file.close();
    file = root.openNextFile();
  }
  root.close();
  for (size_t i = 1; i < count; ++i) {
    String value = names[i];
    size_t j = i;
    while (j > 0 && names[j - 1] > value) {
      names[j] = names[j - 1];
      --j;
    }
    names[j] = value;
  }
  return count;
}

bool readHistoryHeader(File &file, HistoryFileHeader &header) {
  if (!file || file.size() < sizeof(HistoryFileHeader)) return false;
  if (!file.seek(0) || file.read(reinterpret_cast<uint8_t *>(&header), sizeof(header)) != sizeof(header)) return false;
  return validHistoryFileHeader(header);
}

void appendHistoryToRam(const HistorySample &sample) {
  HistorySample &target = historyData.samples[historyData.writeIndex];
  target = sample;
  historyData.writeIndex = (historyData.writeIndex + 1) % HISTORY_POINT_COUNT;
  if (historyData.count < HISTORY_POINT_COUNT) ++historyData.count;
  lastHistorySampleSlot = sample.epoch / ConfigDefaults::HISTORY_SAMPLE_SECONDS;
}

bool appendHistoryToFile(const HistorySample &sample) {
  if (!historyStorageReady || sample.epoch == 0) return false;
  const String path = historyFilePath(sample.epoch);
  if (path.isEmpty()) return false;
  const bool exists = LittleFS.exists(path);
  if (exists) {
    File check = LittleFS.open(path, FILE_READ);
    HistoryFileHeader header = {};
    const bool valid = readHistoryHeader(check, header);
    check.close();
    if (!valid) {
      debugPrintf("Ungültige Verlaufsdatei wird neu angelegt: %s\n", path.c_str());
      LittleFS.remove(path);
    }
  }

  const bool createFile = !LittleFS.exists(path);
  File file = LittleFS.open(path, createFile ? FILE_WRITE : FILE_APPEND);
  if (!file) return false;
  if (createFile) {
    HistoryFileHeader header = {HISTORY_FILE_MAGIC, HISTORY_FILE_VERSION,
                                static_cast<uint16_t>(sizeof(HistorySample)), 0};
    header.crc = historyFileHeaderCrc(header);
    if (file.write(reinterpret_cast<const uint8_t *>(&header), sizeof(header)) != sizeof(header)) {
      file.close();
      return false;
    }
  }
  const size_t written = file.write(reinterpret_cast<const uint8_t *>(&sample), sizeof(sample));
  file.flush();
  file.close();
  if (written != sizeof(sample)) return false;

  File verify = LittleFS.open(path, FILE_READ);
  HistorySample stored = {};
  const size_t fileSize = verify ? verify.size() : 0;
  const bool readbackOk = verify && fileSize >= sizeof(HistoryFileHeader) + sizeof(HistorySample)
      && verify.seek(fileSize - sizeof(HistorySample))
      && verify.read(reinterpret_cast<uint8_t *>(&stored), sizeof(stored)) == sizeof(stored)
      && memcmp(&stored, &sample, sizeof(sample)) == 0;
  if (verify) verify.close();
  if (readbackOk) lastHistoryPersistedEpoch = max(lastHistoryPersistedEpoch, sample.epoch);
  return readbackOk;
}

bool verifyHistorySampleStored(const HistorySample &sample) {
  if (!historyStorageReady || sample.epoch == 0) return false;
  const String path = historyFilePath(sample.epoch);
  if (path.isEmpty()) return false;
  File file = LittleFS.open(path, FILE_READ);
  HistoryFileHeader header = {};
  if (!readHistoryHeader(file, header)) {
    file.close();
    return false;
  }
  const size_t fileSize = file.size();
  const size_t payloadSize = fileSize - sizeof(HistoryFileHeader);
  if (payloadSize < sizeof(HistorySample) || payloadSize % sizeof(HistorySample) != 0) {
    file.close();
    return false;
  }
  HistorySample stored = {};
  const bool valid = file.seek(fileSize - sizeof(HistorySample))
      && file.read(reinterpret_cast<uint8_t *>(&stored), sizeof(stored)) == sizeof(stored)
      && memcmp(&stored, &sample, sizeof(sample)) == 0;
  file.close();
  return valid;
}

bool migrateLegacyHistory() {
  String existing[2];
  if (collectHistoryFiles(existing, 2) > 0) return true;

  HistoryData *legacy = static_cast<HistoryData *>(malloc(sizeof(HistoryData)));
  if (legacy == nullptr) return false;
  bool found = false;
  if (preferences.begin("sghistory", true)) {
    const size_t storedSize = preferences.getBytesLength("history");
    const size_t bytesRead = storedSize == sizeof(HistoryData)
        ? preferences.getBytes("history", legacy, sizeof(HistoryData))
        : 0;
    preferences.end();
    found = bytesRead == sizeof(HistoryData) && validLegacyHistoryBlob(*legacy);
  }
  if (!found) {
    free(legacy);
    return true;
  }

  debugPrintf("Alter 24-Stunden-Verlauf gefunden; %u Punkte werden nach LittleFS übernommen.\n",
              static_cast<unsigned int>(legacy->count));
  const size_t oldestIndex = (legacy->writeIndex + HISTORY_POINT_COUNT - legacy->count)
                           % HISTORY_POINT_COUNT;
  bool migrated = true;
  for (size_t offset = 0; offset < legacy->count; ++offset) {
    const HistorySample &sample = legacy->samples[(oldestIndex + offset) % HISTORY_POINT_COUNT];
    if (sample.epoch == 0) continue;
    if (!appendHistoryToFile(sample)) {
      migrated = false;
      break;
    }
  }
  if (migrated && legacy->count > 0) {
    const size_t newestIndex = (legacy->writeIndex + HISTORY_POINT_COUNT - 1) % HISTORY_POINT_COUNT;
    migrated = verifyHistorySampleStored(legacy->samples[newestIndex]);
  }
  free(legacy);

  if (!migrated) {
    String created[40];
    const size_t createdCount = collectHistoryFiles(created, 40);
    for (size_t i = 0; i < createdCount; ++i) LittleFS.remove(created[i]);
    lastHistoryPersistedEpoch = 0;
    debugPrintln("Übernahme des alten Verlaufs fehlgeschlagen; der alte NVS-Datenblock bleibt erhalten.");
    return false;
  }

  if (preferences.begin("sghistory", false)) {
    preferences.remove("history");
    preferences.end();
  }
  debugPrintln("Alter Verlauf wurde nach LittleFS übernommen, verifiziert und aus NVS entfernt.");
  return true;
}

void cleanupOldHistoryFiles(uint32_t now) {
  if (!historyStorageReady || now == 0) return;
  tm parts = {};
  if (localCalendar(now, parts) && parts.tm_yday == lastHistoryCleanupYearDay) return;
  if (localCalendar(now, parts)) lastHistoryCleanupYearDay = parts.tm_yday;
  const uint32_t cutoff = now > static_cast<uint32_t>(ConfigDefaults::HISTORY_RETENTION_DAYS) * 86400UL
      ? now - static_cast<uint32_t>(ConfigDefaults::HISTORY_RETENTION_DAYS) * 86400UL
      : 0;
  String names[40];
  const size_t count = collectHistoryFiles(names, 40);
  for (size_t i = 0; i < count; ++i) {
    File file = LittleFS.open(names[i], FILE_READ);
    HistoryFileHeader header = {};
    if (!readHistoryHeader(file, header) || file.size() < sizeof(header) + sizeof(HistorySample)) {
      file.close();
      continue;
    }
    HistorySample newest = {};
    const size_t fileSize = file.size();
    const bool validNewest = file.seek(fileSize - sizeof(HistorySample))
        && file.read(reinterpret_cast<uint8_t *>(&newest), sizeof(newest)) == sizeof(newest);
    file.close();
    if (validNewest && newest.epoch < cutoff) {
      LittleFS.remove(names[i]);
      debugPrintf("Alte Verlaufsdatei gelöscht: %s\n", names[i].c_str());
    }
  }
}

void loadStoredHistory() {
  resetHistory();
  historyStorageReady = LittleFS.begin(true, "/littlefs", 6, ConfigDefaults::HISTORY_PARTITION_LABEL);
  if (!historyStorageReady) {
    historyPersistenceOk = false;
    debugPrintln("LittleFS-Verlaufsspeicher konnte nicht geöffnet werden; Verlauf bleibt nur im RAM.");
    return;
  }
  if (!LittleFS.exists(ConfigDefaults::HISTORY_DIRECTORY)) LittleFS.mkdir(ConfigDefaults::HISTORY_DIRECTORY);
  migrateLegacyHistory();
  const uint32_t now = clockIsValid() ? static_cast<uint32_t>(time(nullptr)) : 0;
  const uint32_t cutoff = now > 86400UL ? now - 86400UL : 0;
  String names[40];
  const size_t count = collectHistoryFiles(names, 40);
  for (size_t i = 0; i < count; ++i) {
    File file = LittleFS.open(names[i], FILE_READ);
    HistoryFileHeader header = {};
    if (!readHistoryHeader(file, header)) {
      file.close();
      continue;
    }
    HistorySample sample = {};
    while (file.read(reinterpret_cast<uint8_t *>(&sample), sizeof(sample)) == sizeof(sample)) {
      if (sample.epoch == 0) continue;
      lastHistoryPersistedEpoch = max(lastHistoryPersistedEpoch, sample.epoch);
      if (sample.epoch >= cutoff) appendHistoryToRam(sample);
    }
    file.close();
  }
  historyDirty = false;
  historyPersistenceOk = true;
  lastHistoryPersistAt = millis();
  cleanupOldHistoryFiles(now);
  debugPrintf("LittleFS-Verlauf geladen: %u RAM-Punkte, %u Dateien, %u/%u Byte belegt.\n",
              static_cast<unsigned int>(historyData.count), static_cast<unsigned int>(count),
              static_cast<unsigned int>(LittleFS.usedBytes()), static_cast<unsigned int>(LittleFS.totalBytes()));
}

bool saveStoredHistory(bool forceWrite = false) {
  if (!historyStorageReady) {
    historyPersistenceOk = false;
    return false;
  }
  if (!historyDirty && !forceWrite) return historyPersistenceOk;

  const size_t oldestIndex = (historyData.writeIndex + HISTORY_POINT_COUNT - historyData.count)
                           % HISTORY_POINT_COUNT;
  bool allWritten = true;
  for (size_t offset = 0; offset < historyData.count; ++offset) {
    const HistorySample &sample = historyData.samples[(oldestIndex + offset) % HISTORY_POINT_COUNT];
    if (sample.epoch == 0 || sample.epoch <= lastHistoryPersistedEpoch) continue;
    if (!appendHistoryToFile(sample)) {
      allWritten = false;
      break;
    }
  }

  if (allWritten && forceWrite && historyData.count > 0) {
    const size_t newestIndex = (historyData.writeIndex + HISTORY_POINT_COUNT - 1) % HISTORY_POINT_COUNT;
    allWritten = verifyHistorySampleStored(historyData.samples[newestIndex]);
  }

  historyDirty = !allWritten;
  historyPersistenceOk = allWritten;
  lastHistoryPersistAt = millis();
  if (allWritten) {
    if (forceWrite) debugPrintln("Verlaufsdateien dauerhaft gespeichert und verifiziert.");
    cleanupOldHistoryFiles(clockIsValid() ? static_cast<uint32_t>(time(nullptr)) : 0);
  } else {
    debugPrintln("Verlaufsdaten konnten nicht vollständig in LittleFS gespeichert werden.");
  }
  return allWritten;
}

void resetLoadProfile() {
  memset(&loadProfile, 0, sizeof(loadProfile));
  loadProfile.magic = LOAD_PROFILE_MAGIC;
  loadProfile.version = LOAD_PROFILE_VERSION;
  memset(todayLoadWatts, 0, sizeof(todayLoadWatts));
  memset(todaySamples, 0, sizeof(todaySamples));
  profileDirty = true;
}

void loadStoredProfile() {
  resetLoadProfile();
  profileDirty = false;
  const bool dedicatedReady = ensureProfileStorage();
  bool loaded = dedicatedReady
      && readStoredProfileFrom(ConfigDefaults::PROFILE_PARTITION_LABEL, loadProfile);
  bool migrated = false;
  if (!loaded) {
    resetLoadProfile();
    profileDirty = false;
    loaded = readStoredProfileFrom(nullptr, loadProfile);
    migrated = loaded;
  }
  if (!loaded) {
    resetLoadProfile();
    profileDirty = false;
    debugPrintln("Kein gültiges Lastprofil gefunden; Lernen startet neu.");
    return;
  }

  if (migrated) {
    debugPrintln("Altes Lastprofil gefunden; Übernahme in die neue Profil-NVS läuft.");
    if (saveStoredProfile()) {
      if (preferences.begin("sgprofile", false)) {
        preferences.remove("profile");
        preferences.end();
        debugPrintln("Lastprofil in die neue Profil-NVS übernommen und alten Datenblock entfernt.");
      } else {
        debugPrintln("Lastprofil wurde übernommen; der alte Datenblock konnte noch nicht entfernt werden.");
      }
    } else {
      profileDirty = true;
      debugPrintln("Altes Lastprofil bleibt erhalten, da die Übernahme noch nicht verifiziert werden konnte.");
    }
  } else {
    debugPrintln("Lastprofil aus der Profil-NVS geladen.");
  }
}

bool saveStoredProfile() {
  loadProfile.crc = 0;
  loadProfile.crc = loadProfileCrc();
  if (!ensureProfileStorage()
      || !preferences.begin("sgprofile", false, ConfigDefaults::PROFILE_PARTITION_LABEL)) {
    debugPrintln("Lastprofil konnte nicht zum Speichern geöffnet werden.");
    return false;
  }
  const size_t written = preferences.putBytes("profile", &loadProfile, sizeof(loadProfile));
  preferences.end();
  if (written != sizeof(loadProfile)) {
    debugPrintln("Lastprofil konnte nicht vollständig gespeichert werden.");
    return false;
  }

  LoadProfileData *verified = static_cast<LoadProfileData *>(malloc(sizeof(LoadProfileData)));
  if (verified == nullptr) {
    debugPrintln("Lastprofil konnte wegen Speichermangel nicht rückgelesen werden.");
    return false;
  }
  bool readbackOk = false;
  if (preferences.begin("sgprofile", true, ConfigDefaults::PROFILE_PARTITION_LABEL)) {
    const size_t storedSize = preferences.getBytesLength("profile");
    const size_t bytesRead = storedSize == sizeof(LoadProfileData)
        ? preferences.getBytes("profile", verified, sizeof(LoadProfileData))
        : 0;
    preferences.end();
    readbackOk = bytesRead == sizeof(LoadProfileData)
        && validLoadProfileBlob(*verified)
        && memcmp(verified, &loadProfile, sizeof(LoadProfileData)) == 0;
  }
  free(verified);
  if (!readbackOk) {
    debugPrintln("Lastprofil konnte nach dem Speichern nicht verifiziert werden.");
    return false;
  }

  profileDirty = false;
  lastProfilePersistAt = millis();
  debugPrintln("Lastprofil dauerhaft gespeichert und verifiziert.");
  return true;
}

uint16_t expectedLoadWatts(time_t epoch) {
  const uint8_t day = profileDayFor(epoch);
  const uint8_t slot = profileSlotFor(epoch);
  if (loadProfile.samples[day][slot] > 0) {
    const uint32_t eventPart = static_cast<uint32_t>(loadProfile.eventWatts[day][slot])
                             * loadProfile.eventProbability[day][slot] / 100U;
    return static_cast<uint16_t>(min<uint32_t>(65535U, loadProfile.baseWatts[day][slot] + eventPart));
  }
  uint32_t sum = 0;
  uint8_t count = 0;
  for (size_t otherDay = 0; otherDay < LOAD_PROFILE_DAYS; ++otherDay) {
    if (loadProfile.samples[otherDay][slot] == 0) continue;
    sum += loadProfile.baseWatts[otherDay][slot]
         + static_cast<uint32_t>(loadProfile.eventWatts[otherDay][slot])
         * loadProfile.eventProbability[otherDay][slot] / 100U;
    ++count;
  }
  if (count > 0) return static_cast<uint16_t>(min<uint32_t>(65535U, sum / count));
  const int32_t liveLoad = measuredLoadPower();
  if (liveLoad >= 0) {
    const uint32_t controlled = controlledStagePowerWatts();
    return static_cast<uint16_t>(min<uint32_t>(65535U,
        static_cast<uint32_t>(liveLoad) > controlled ? static_cast<uint32_t>(liveLoad) - controlled : 0U));
  }
  return 400;  // konservative Startannahme, bis echte 15-Minuten-Werte gelernt wurden
}

uint16_t blendUnsigned(uint16_t previous, uint16_t sample, uint8_t percent) {
  if (previous == 0) return sample;
  return static_cast<uint16_t>((static_cast<uint32_t>(previous) * (100U - percent)
                              + static_cast<uint32_t>(sample) * percent + 50U) / 100U);
}

void commitLearningBlock() {
  if (activeLearningDay >= LOAD_PROFILE_DAYS || activeLearningSlot >= LOAD_PROFILE_SLOTS || learningLoadSamples == 0) return;
  const uint16_t sample = static_cast<uint16_t>(min<uint64_t>(65535ULL, learningLoadSum / learningLoadSamples));
  uint16_t &base = loadProfile.baseWatts[activeLearningDay][activeLearningSlot];
  uint16_t &event = loadProfile.eventWatts[activeLearningDay][activeLearningSlot];
  uint8_t &probability = loadProfile.eventProbability[activeLearningDay][activeLearningSlot];
  uint8_t &samples = loadProfile.samples[activeLearningDay][activeLearningSlot];
  const bool highEvent = base != 0 && sample > static_cast<uint32_t>(base) + config.eventLoadThresholdWatts;
  if (highEvent) {
    event = blendUnsigned(event, static_cast<uint16_t>(sample - base), config.learningPercent);
  } else {
    base = blendUnsigned(base, sample, config.learningPercent);
  }
  const uint16_t observation = highEvent ? 100U : 0U;
  probability = static_cast<uint8_t>((static_cast<uint16_t>(probability) * (100U - config.learningPercent)
                                    + observation * config.learningPercent + 50U) / 100U);
  if (samples < 255) ++samples;
  todayLoadWatts[activeLearningSlot] = sample;
  if (todaySamples[activeLearningSlot] < 65535) ++todaySamples[activeLearningSlot];
  profileDirty = true;
}

void serviceLoadProfile() {
  if (!clockIsValid() || millis() - lastLoadSampleAt < ConfigDefaults::LOAD_SAMPLE_INTERVAL_MS) return;
  lastLoadSampleAt = millis();
  const int32_t rawLoad = measuredLoadPower();
  if (rawLoad < 0) return;
  const uint32_t knownControlled = controlledStagePowerWatts();
  const uint16_t sample = static_cast<uint16_t>(min<uint32_t>(65535U,
      static_cast<uint32_t>(rawLoad) > knownControlled ? static_cast<uint32_t>(rawLoad) - knownControlled : 0U));
  const time_t now = time(nullptr);
  tm parts = {};
  if (!localCalendar(now, parts)) return;
  const uint8_t day = static_cast<uint8_t>(parts.tm_wday);
  const uint8_t slot = static_cast<uint8_t>(parts.tm_hour * 4 + parts.tm_min / 15);
  if (day != activeLearningDay || slot != activeLearningSlot) {
    commitLearningBlock();
    activeLearningDay = day;
    activeLearningSlot = slot;
    learningLoadSum = 0;
    learningLoadSamples = 0;
  }
  learningLoadSum += sample;
  if (learningLoadSamples < 65535) ++learningLoadSamples;
  todayLoadWatts[slot] = static_cast<uint16_t>(min<uint64_t>(65535ULL, learningLoadSum / learningLoadSamples));

  if (lastProfileSavedYearDay < 0) {
    lastProfileSavedYearDay = parts.tm_yday;
    lastProfileWeekday = parts.tm_wday;
  }
  if (parts.tm_yday != lastProfileSavedYearDay) {
    if (lastProfileWeekday >= 0 && loadProfile.learnedDays[lastProfileWeekday] < 65535) {
      ++loadProfile.learnedDays[lastProfileWeekday];
    }
    lastProfileSavedYearDay = parts.tm_yday;
    lastProfileWeekday = parts.tm_wday;
    saveStoredProfile();
    memset(todayLoadWatts, 0, sizeof(todayLoadWatts));
    memset(todaySamples, 0, sizeof(todaySamples));
  } else if (profileDirty && millis() - lastProfilePersistAt >= ConfigDefaults::PROFILE_PERSIST_INTERVAL_MS) {
    saveStoredProfile();
  }
}

RegisterDef *exportPowerRegister() {
  for (size_t i = 0; i < REGISTER_COUNT; ++i) {
    if (registers[i].source == SourceGroup::INVERTER && registers[i].address == 13009) return &registers[i];
  }
  return nullptr;
}

bool exportPowerIsFresh() {
  RegisterDef *reg = exportPowerRegister();
  if (reg == nullptr || !reg->valid || reg->updatedAt == 0) return false;
  const uint32_t configuredAge = static_cast<uint32_t>(config.pollSeconds) * 3000UL;
  const uint32_t maximumAge = configuredAge > 10000UL ? configuredAge : 10000UL;
  return millis() - reg->updatedAt <= maximumAge;
}

int32_t measuredExportPower() {
  RegisterDef *reg = exportPowerRegister();
  if (reg == nullptr || !reg->valid) return 0;
  return static_cast<int32_t>(reg->value);
}

bool historyPowerValue(SourceGroup source, uint16_t address, int32_t &watts) {
  RegisterDef *reg = findRegister(source, address);
  const uint32_t maximumAge = max<uint32_t>(30000UL,
      static_cast<uint32_t>(config.pollSeconds) * 3000UL + 5000UL);
  if (!registerIsFresh(reg, maximumAge) || !isfinite(reg->value)) return false;
  watts = static_cast<int32_t>(constrain(reg->value, -327670.0, 327670.0));
  return true;
}

int16_t encodeHistoryPower(int32_t watts) {
  const int32_t rounded = watts >= 0 ? (watts + 5) / 10 : (watts - 5) / 10;
  return static_cast<int16_t>(constrain(rounded, -32767L, 32767L));
}

void serviceHistory() {
  if (!clockIsValid()) return;
  const uint32_t epoch = static_cast<uint32_t>(time(nullptr));
  const uint32_t slot = epoch / ConfigDefaults::HISTORY_SAMPLE_SECONDS;
  if (slot == lastHistorySampleSlot) return;

  int32_t pv = 0;
  int32_t load = 0;
  int32_t grid = 0;
  int32_t battery = 0;
  const bool pvValid = historyPowerValue(SourceGroup::INVERTER, 5016, pv);
  const bool loadValid = historyPowerValue(SourceGroup::INVERTER, 13007, load);
  const bool gridValid = historyPowerValue(SourceGroup::INVERTER, 13009, grid);
  const bool batteryValid = historyPowerValue(SourceGroup::INVERTER, 13021, battery);
  const float soc = currentBatterySoc();
  const bool socValid = isfinite(soc);
  if (!pvValid && !loadValid && !gridValid && !batteryValid && !socValid) return;

  HistorySample sample = {};
  sample.epoch = slot * ConfigDefaults::HISTORY_SAMPLE_SECONDS;
  sample.pvDecawatts = pvValid ? encodeHistoryPower(pv) : HISTORY_INVALID_POWER;
  sample.loadDecawatts = loadValid ? encodeHistoryPower(max<int32_t>(0, load)) : HISTORY_INVALID_POWER;
  sample.gridDecawatts = gridValid ? encodeHistoryPower(grid) : HISTORY_INVALID_POWER;
  sample.batteryDecawatts = batteryValid ? encodeHistoryPower(battery) : HISTORY_INVALID_POWER;
  sample.socTenths = socValid
      ? static_cast<uint16_t>(constrain(static_cast<int32_t>(lroundf(soc * 10.0f)), 0L, 1000L))
      : HISTORY_INVALID_SOC;

  appendHistoryToRam(sample);
  historyDirty = true;
  saveStoredHistory();
}

int32_t forecastBatteryReserveWatts();

int32_t calculatedAvailableSurplus() {
  if (!exportPowerIsFresh()) return 0;
  int32_t available = measuredExportPower();
  for (size_t i = 0; i < SURPLUS_STAGE_COUNT; ++i) {
    if (stageIsOn[i]) available += surplusStages[i].watts;
  }
  available -= forecastBatteryReserveWatts();
  return available;
}

void initializeSurplusStages() {
  for (size_t i = 0; i < SURPLUS_STAGE_COUNT; ++i) {
    const SurplusStageConfig &stage = surplusStages[i];
    if (stage.enabled && stage.type == StageType::GPIO && isSafeOutputGpio(stage.gpio)) {
      pinMode(stage.gpio, OUTPUT);
      digitalWrite(stage.gpio, LOW);
      stageStateKnown[i] = true;
    } else {
      stageStateKnown[i] = false;
    }
    stageIsOn[i] = false;
    delayPending[i] = false;
    conditionSince[i] = 0;
    lastApiAttemptAt[i] = 0;
    lastApiStatus[i] = stage.type == StageType::API ? "Noch nicht synchronisiert" : "GPIO bereit";
  }
  debugPrintln("Alle Überschuss-Stufen sind AUS.");
}

const char *apiMethodName(ApiMethod method) {
  switch (method) {
    case ApiMethod::GET: return "GET";
    case ApiMethod::PUT: return "PUT";
    case ApiMethod::POST: return "POST";
  }
  return "PUT";
}

bool sendStageApiCommand(size_t index, bool turnOn) {
  SurplusStageConfig &stage = surplusStages[index];
  lastApiAttemptAt[index] = millis();
  if (WiFi.status() != WL_CONNECTED) {
    lastApiStatus[index] = "WLAN nicht verbunden";
    return false;
  }

  const String &url = turnOn ? stage.onUrl : stage.offUrl;
  const String &body = turnOn ? stage.onJson : stage.offJson;
  if (!url.startsWith("http://")) {
    lastApiStatus[index] = "Nur http://-URLs werden unterstützt";
    return false;
  }

  HTTPClient http;
  http.setConnectTimeout(3000);
  http.setTimeout(5000);
  if (!http.begin(url)) {
    lastApiStatus[index] = "API-URL konnte nicht geöffnet werden";
    return false;
  }

  int responseCode = -1;
  if (stage.method == ApiMethod::GET) {
    responseCode = http.GET();
  } else {
    http.addHeader("Content-Type", "application/json");
    responseCode = stage.method == ApiMethod::POST ? http.POST(body) : http.PUT(body);
  }
  http.end();

  if (responseCode >= 200 && responseCode < 300) {
    lastApiStatus[index] = String(apiMethodName(stage.method)) + " " + (turnOn ? "EIN" : "AUS") + " erfolgreich (HTTP " + String(responseCode) + ")";
    debugPrintf("Stufe %u API %s: HTTP %d.\n", static_cast<unsigned int>(index + 1), turnOn ? "EIN" : "AUS", responseCode);
    return true;
  }

  lastApiStatus[index] = String(apiMethodName(stage.method)) + " fehlgeschlagen (HTTP " + String(responseCode) + ")";
  debugPrintf("Stufe %u API-Fehler: HTTP %d.\n", static_cast<unsigned int>(index + 1), responseCode);
  return false;
}

void forceAllStagesOff(bool bypassApiRetry = false) {
  bool changed = false;
  for (size_t i = 0; i < SURPLUS_STAGE_COUNT; ++i) {
    SurplusStageConfig &stage = surplusStages[i];
    delayPending[i] = false;
    if (stage.enabled && stage.type == StageType::GPIO) {
      if (isSafeOutputGpio(stage.gpio)) digitalWrite(stage.gpio, LOW);
      if (stageIsOn[i]) changed = true;
      stageIsOn[i] = false;
      stageStateKnown[i] = true;
      continue;
    }

    if (!stage.enabled) continue;

    if ((!stageStateKnown[i] || stageIsOn[i])
        && (bypassApiRetry || lastApiAttemptAt[i] == 0 || millis() - lastApiAttemptAt[i] >= ConfigDefaults::API_RETRY_MS)) {
      if (sendStageApiCommand(i, false)) {
        if (stageIsOn[i]) changed = true;
        stageIsOn[i] = false;
        stageStateKnown[i] = true;
      }
    }
  }
  if (changed) debugPrintln("Alle Überschuss-Stufen wurden AUS geschaltet.");
}

bool delayedStageState(size_t index, bool currentState, bool desiredState) {
  if (desiredState == currentState) {
    delayPending[index] = false;
    return currentState;
  }

  if (!delayPending[index] || pendingState[index] != desiredState) {
    delayPending[index] = true;
    pendingState[index] = desiredState;
    conditionSince[index] = millis();
  }

  const uint16_t delaySeconds = desiredState ? surplusStages[index].onDelaySeconds : surplusStages[index].offDelaySeconds;
  const uint32_t requiredMs = static_cast<uint32_t>(delaySeconds) * 1000UL;
  if (requiredMs == 0 || millis() - conditionSince[index] >= requiredMs) {
    delayPending[index] = false;
    return desiredState;
  }
  return currentState;
}

void controlSurplusStages() {
  if (!exportPowerIsFresh()) {
    forceAllStagesOff();
    return;
  }

  const int32_t available = calculatedAvailableSurplus();
  bool desired[SURPLUS_STAGE_COUNT] = {};
  int32_t cumulativePower = 0;
  bool precedingStagesAvailable = true;

  for (size_t i = 0; i < SURPLUS_STAGE_COUNT; ++i) {
    const SurplusStageConfig &stage = surplusStages[i];
    if (!stage.enabled) continue;
    cumulativePower += stage.watts;
    const int32_t offThreshold = cumulativePower - ConfigDefaults::HEATER_HYSTERESIS_W;
    const bool enoughPower = stageIsOn[i] ? available >= offThreshold : available >= cumulativePower;
    desired[i] = precedingStagesAvailable && enoughPower;
    if (!desired[i]) precedingStagesAvailable = false;
  }

  for (size_t i = 0; i < SURPLUS_STAGE_COUNT; ++i) {
    SurplusStageConfig &stage = surplusStages[i];
    if (!stage.enabled) {
      delayPending[i] = false;
      continue;
    }
    if (stage.type == StageType::GPIO) {
      const bool target = delayedStageState(i, stageIsOn[i], desired[i]);
      if (target != stageIsOn[i]) {
        stageIsOn[i] = target;
        digitalWrite(stage.gpio, target ? HIGH : LOW);
        debugPrintf("Stufe %u %s (GPIO %u).\n", static_cast<unsigned int>(i + 1), target ? "EIN" : "AUS", stage.gpio);
      }
      continue;
    }

    // Nach einem Neustart zuerst AUS senden, damit der externe Zustand bekannt ist.
    if (!stageStateKnown[i]) {
      if (lastApiAttemptAt[i] == 0 || millis() - lastApiAttemptAt[i] >= ConfigDefaults::API_RETRY_MS) {
        if (sendStageApiCommand(i, false)) {
          stageIsOn[i] = false;
          stageStateKnown[i] = true;
        }
      }
      continue;
    }

    const bool apiTarget = delayedStageState(i, stageIsOn[i], desired[i]);
    if (apiTarget != stageIsOn[i]
        && (lastApiAttemptAt[i] == 0 || millis() - lastApiAttemptAt[i] >= ConfigDefaults::API_RETRY_MS)) {
      if (sendStageApiCommand(i, apiTarget)) {
        stageIsOn[i] = apiTarget;
      } else {
        const uint16_t requiredSeconds = apiTarget ? stage.onDelaySeconds : stage.offDelaySeconds;
        delayPending[i] = true;
        pendingState[i] = apiTarget;
        conditionSince[i] = millis() - static_cast<uint32_t>(requiredSeconds) * 1000UL;
      }
    }
  }
}

int findJsonValueStart(const String &json, const String &key, int fromIndex = 0) {
  const String token = "\"" + key + "\":";
  const int position = json.indexOf(token, fromIndex);
  return position < 0 ? -1 : position + token.length();
}

bool jsonNumber(const String &json, const String &key, double &value, int fromIndex = 0) {
  int position = findJsonValueStart(json, key, fromIndex);
  if (position < 0) return false;
  while (position < static_cast<int>(json.length()) && isspace(static_cast<unsigned char>(json[position]))) ++position;
  char *end = nullptr;
  value = strtod(json.c_str() + position, &end);
  return end != json.c_str() + position;
}

bool jsonString(const String &json, const String &key, String &value, int fromIndex = 0) {
  int position = findJsonValueStart(json, key, fromIndex);
  if (position < 0) return false;
  while (position < static_cast<int>(json.length()) && json[position] != '"') ++position;
  if (position >= static_cast<int>(json.length())) return false;
  const int end = json.indexOf('"', position + 1);
  if (end < 0) return false;
  value = json.substring(position + 1, end);
  return true;
}

size_t jsonNumberArray(const String &json, const String &key, double *values, size_t capacity, int fromIndex = 0) {
  int position = findJsonValueStart(json, key, fromIndex);
  if (position < 0) return 0;
  position = json.indexOf('[', position);
  if (position < 0) return 0;
  ++position;
  size_t count = 0;
  while (position < static_cast<int>(json.length()) && count < capacity) {
    while (position < static_cast<int>(json.length())
           && (isspace(static_cast<unsigned char>(json[position])) || json[position] == ',')) ++position;
    if (position >= static_cast<int>(json.length()) || json[position] == ']') break;
    if (json.startsWith("null", position)) {
      values[count++] = 0.0;
      position += 4;
      continue;
    }
    char *end = nullptr;
    const double parsed = strtod(json.c_str() + position, &end);
    if (end == json.c_str() + position) break;
    values[count++] = parsed;
    position = static_cast<int>(end - json.c_str());
  }
  return count;
}

String formatLocalTime(time_t epoch, bool withDate = false) {
  tm parts = {};
  if (!localCalendar(epoch, parts)) return "—";
  char buffer[28];
  strftime(buffer, sizeof(buffer), withDate ? "%d.%m. %H:%M" : "%H:%M", &parts);
  return String(buffer);
}

bool fetchOpenMeteoArray(size_t arrayIndex, bool initializeTimes) {
  const PvArrayConfig &array = config.pvArrays[arrayIndex];
  String url = F("https://api.open-meteo.com/v1/forecast?latitude=");
  url += String(config.latitude, 6);
  url += F("&longitude=");
  url += String(config.longitude, 6);
  url += F("&hourly=global_tilted_irradiance&daily=sunrise,sunset&tilt=");
  url += String(array.tiltDegrees);
  url += F("&azimuth=");
  url += String(array.azimuthDegrees);
  url += F("&timezone=auto&timeformat=unixtime&forecast_days=2");

  NetworkClientSecure secureClient;
  secureClient.setInsecure();  // Open-Meteo HTTPS ohne lokalen CA-Speicher
  HTTPClient http;
  http.setConnectTimeout(6000);
  http.setTimeout(9000);
  if (!http.begin(secureClient, url)) {
    forecast.status = "Open-Meteo-Verbindung konnte nicht geöffnet werden";
    return false;
  }
  http.addHeader("Accept", "application/json");
  const int responseCode = http.GET();
  if (responseCode != HTTP_CODE_OK) {
    forecast.status = "Open-Meteo HTTP " + String(responseCode) + " für " + array.name;
    http.end();
    return false;
  }
  const String json = http.getString();
  http.end();
  if (json.length() < 100) {
    forecast.status = "Leere Open-Meteo-Antwort für " + array.name;
    return false;
  }

  const int hourlyObject = json.indexOf("\"hourly\":");
  double values[FORECAST_POINT_COUNT];
  if (initializeTimes) {
    double offset = 0.0;
    jsonNumber(json, "utc_offset_seconds", offset);
    forecast.utcOffsetSeconds = static_cast<int32_t>(offset);
    jsonString(json, "timezone", forecast.timezoneName);
    const size_t timeCount = jsonNumberArray(json, "time", values, FORECAST_POINT_COUNT, hourlyObject);
    if (timeCount == 0) {
      forecast.status = "Keine Zeitreihe in Open-Meteo-Antwort";
      return false;
    }
    forecast.pointCount = static_cast<uint8_t>(timeCount);
    for (size_t i = 0; i < timeCount; ++i) forecast.points[i].epoch = static_cast<time_t>(values[i]);
    const int dailyObject = json.indexOf("\"daily\":");
    if (jsonNumberArray(json, "sunrise", values, 2, dailyObject) > 0) forecast.sunrise = static_cast<time_t>(values[0]);
    if (jsonNumberArray(json, "sunset", values, 2, dailyObject) > 0) forecast.sunset = static_cast<time_t>(values[0]);
  }

  const size_t gtiCount = jsonNumberArray(json, "global_tilted_irradiance", values,
                                           min<size_t>(FORECAST_POINT_COUNT, forecast.pointCount), hourlyObject);
  if (gtiCount != forecast.pointCount) {
    forecast.status = "Unvollständige Einstrahlungswerte für " + array.name;
    return false;
  }
  for (size_t i = 0; i < gtiCount; ++i) {
    forecast.points[i].gti[arrayIndex] = static_cast<uint16_t>(constrain(values[i], 0.0, 2000.0));
  }
  return true;
}

float overlappingHours(time_t intervalStart, time_t intervalEnd,
                       time_t rangeStart, time_t rangeEnd) {
  const time_t overlapStart = intervalStart > rangeStart ? intervalStart : rangeStart;
  const time_t overlapEnd = intervalEnd < rangeEnd ? intervalEnd : rangeEnd;
  return overlapEnd > overlapStart
      ? static_cast<float>(overlapEnd - overlapStart) / 3600.0f
      : 0.0f;
}

void calculateForecastPlan() {
  const float capacityKwh = configuredBatteryCapacityKwh();
  const float soc = currentBatterySoc();
  if (!forecast.valid || !isfinite(capacityKwh) || capacityKwh <= 0.1f || !isfinite(soc)) return;
  forecast.currentSoc = soc;
  if (!isfinite(forecast.startSoc)) forecast.startSoc = soc;
  forecast.startSoc = constrain(forecast.startSoc, 0.0f, 100.0f);
  const time_t now = time(nullptr);
  const time_t planStart = forecast.fetchedAt > 0 ? forecast.fetchedAt : now;
  forecast.finishAt = forecast.sunset - static_cast<time_t>(config.finishBufferMinutes) * 60;
  if (forecast.finishAt <= forecast.sunrise) forecast.finishAt = forecast.sunset;
  forecast.totalPvKwh = 0.0f;
  forecast.totalLearnedLoadKwh = 0.0f;
  forecast.totalBatteryEnergyKwh = 0.0f;

  for (size_t i = 0; i < forecast.pointCount; ++i) {
    ForecastPoint &point = forecast.points[i];
    point.pvKw = 0.0f;
    for (size_t roof = 0; roof < PV_ARRAY_COUNT; ++roof) {
      if (!config.pvArrays[roof].enabled) continue;
      point.pvKw += config.pvArrays[roof].peakKwp * (static_cast<float>(point.gti[roof]) / 1000.0f)
                  * (config.pvSystemEfficiencyPercent / 100.0f);
    }
    point.learnedLoadKw = expectedLoadWatts(point.epoch) / 1000.0f;
    point.batteryEnergyKwh = max(0.0f, point.pvKw - point.learnedLoadKw)
                           * (config.batteryChargeEfficiencyPercent / 100.0f)
                           * (config.forecastSafetyPercent / 100.0f);
    const float includedHours = overlappingHours(point.epoch, point.epoch + 3600,
                                                  planStart, forecast.finishAt);
    if (includedHours > 0.0f) {
      forecast.totalPvKwh += point.pvKw * includedHours;
      forecast.totalLearnedLoadKwh += point.learnedLoadKw * includedHours;
      forecast.totalBatteryEnergyKwh += point.batteryEnergyKwh * includedHours;
    }
  }

  const float safetyFraction = max(0.01f, config.forecastSafetyPercent / 100.0f);
  // Die Punktenergien enthalten bereits den Sicherheitsabschlag. Durch die
  // zusaetzliche Division des Gesamtwerts durch denselben Faktor entspricht
  // der Quotient wieder: Rohenergie bisher / (Rohenergie gesamt * Sicherheit).
  // Bei 80 % erreicht der Fahrplan sein Ziel damit nach rund 80 % der
  // erwarteten nutzbaren Tagesenergie; der Rest bleibt als Wetterreserve.
  const float scheduleEnergyKwh = forecast.totalBatteryEnergyKwh * safetyFraction;
  auto calculateSocAtEnergy = [&](float cumulativeEnergyKwh,
                                  float &plannedSoc, float &reachableSoc) {
    const float ratio = scheduleEnergyKwh > 0.01f
        ? cumulativeEnergyKwh / scheduleEnergyKwh
        : 1.0f;
    plannedSoc = forecast.startSoc
        + (config.maxSoc - forecast.startSoc) * constrain(ratio, 0.0f, 1.0f);
    const float remainingSafeEnergy = max(0.0f,
        forecast.totalBatteryEnergyKwh - cumulativeEnergyKwh);
    reachableSoc = constrain(config.maxSoc - remainingSafeEnergy / capacityKwh * 100.0f,
                             0.0f, static_cast<float>(config.maxSoc));
    plannedSoc = constrain(max(plannedSoc, reachableSoc),
                           static_cast<float>(ConfigDefaults::MIN_MAX_SOC_PERCENT),
                           static_cast<float>(config.maxSoc));
  };

  float cumulative = 0.0f;
  for (size_t i = 0; i < forecast.pointCount; ++i) {
    ForecastPoint &point = forecast.points[i];
    // Der Kurvenwert gehoert zum auf der X-Achse gezeigten Stundenbeginn.
    // Deshalb wird erst der SOC berechnet und danach die Energie dieser
    // Stunde zum naechsten Punkt addiert.
    calculateSocAtEnergy(cumulative, point.plannedSoc, point.reachableSoc);
    cumulative += point.batteryEnergyKwh
        * overlappingHours(point.epoch, point.epoch + 3600, planStart, forecast.finishAt);
  }

  float cumulativeUntilNow = 0.0f;
  const time_t progressEnd = now < forecast.finishAt ? now : forecast.finishAt;
  for (size_t i = 0; i < forecast.pointCount; ++i) {
    const ForecastPoint &point = forecast.points[i];
    cumulativeUntilNow += point.batteryEnergyKwh
        * overlappingHours(point.epoch, point.epoch + 3600, planStart, progressEnd);
  }
  calculateSocAtEnergy(cumulativeUntilNow, forecast.plannedSoc, forecast.reachableSoc);
}

bool fetchOpenMeteoForecast() {
  if (WiFi.status() != WL_CONNECTED) {
    forecast.status = "WLAN für Open-Meteo nicht verbunden";
    return false;
  }
  if (config.latitude == 0.0f && config.longitude == 0.0f) {
    forecast.status = "Standortkoordinaten fehlen";
    return false;
  }
  size_t enabledArrays = 0;
  for (size_t i = 0; i < PV_ARRAY_COUNT; ++i) if (config.pvArrays[i].enabled) ++enabledArrays;
  if (enabledArrays == 0) {
    forecast.status = "Keine PV-Dachfläche aktiviert";
    return false;
  }
  forecast.fetching = true;
  forecast.status = "Open-Meteo wird abgerufen";
  forecast.pointCount = 0;
  memset(forecast.points, 0, sizeof(forecast.points));
  bool first = true;
  for (size_t i = 0; i < PV_ARRAY_COUNT; ++i) {
    if (!config.pvArrays[i].enabled) continue;
    if (!fetchOpenMeteoArray(i, first)) {
      forecast.valid = false;
      forecast.fetching = false;
      nextForecastFetchAt = millis() + 15UL * 60UL * 1000UL;
      debugPrintln("Prognosefehler: " + forecast.status);
      return false;
    }
    first = false;
    delay(0);
  }
  forecast.valid = true;
  forecast.fetching = false;
  forecast.fetchedAt = time(nullptr);
  forecast.startSoc = currentBatterySoc();
  if (isfinite(forecast.startSoc)) {
    forecast.status = "Open-Meteo-Prognose aktuell";
    calculateForecastPlan();
  } else {
    forecast.status = "Wetter aktuell; absoluter Batteriestand 10743 fehlt";
  }
  nextForecastFetchAt = millis() + static_cast<uint32_t>(config.forecastFetchHours) * 60UL * 60UL * 1000UL;
  debugPrintf("Open-Meteo: %u Stunden, PV %.1f kWh, Last %.1f kWh, Akku verfügbar %.1f kWh.\n",
              forecast.pointCount, forecast.totalPvKwh, forecast.totalLearnedLoadKwh, forecast.totalBatteryEnergyKwh);
  return true;
}

bool forecastIsFresh() {
  return forecast.valid && forecast.fetchedAt > 0 && clockIsValid()
      && time(nullptr) - forecast.fetchedAt <= static_cast<time_t>(config.forecastStaleHours) * 3600;
}

int32_t forecastBatteryReserveWatts() {
  if (!config.forecastEnabled || config.forecastBypass || !forecastIsFresh()) return 0;
  const float soc = currentBatterySoc();
  const float capacity = configuredBatteryCapacityKwh();
  if (!isfinite(soc) || !isfinite(capacity) || soc + 1.0f >= forecast.plannedSoc) return 0;
  const time_t remainingSeconds = max<time_t>(900, forecast.finishAt - time(nullptr));
  const float missingKwh = capacity * (forecast.plannedSoc - soc) / 100.0f;
  return static_cast<int32_t>(constrain(missingKwh * 3600000.0f / remainingSeconds, 0.0f, 10000.0f));
}

bool readSocHoldingRegisters() {
  uint16_t words[2];
  if (!readHoldingRegistersConfigured(ConfigDefaults::MAX_SOC_HOLDING_ADDRESS, 2, words)) return false;
  if (words[0] < 100 || words[0] > 1000 || words[1] > 1000 || words[1] > words[0]) {
    lastTransactionError = "Unplausible Max-/Min-SOC-Holdingwerte";
    return false;
  }
  if (lastWrittenMaxSocRaw != 0 && words[0] != lastWrittenMaxSocRaw) {
    debugPrintf("Max-SOC wurde extern geändert: %.1f %% (zuletzt geschrieben %.1f %%).\n",
                words[0] / 10.0f, lastWrittenMaxSocRaw / 10.0f);
    if (config.forecastBypass && !forecastRestorePending) {
      forecast.status = "Bypass: externe Max-SOC-Änderung erkannt und nicht überschrieben";
    }
    lastWrittenMaxSocRaw = 0;
  }
  holdingMaxSocRaw = words[0];
  holdingMinSocRaw = words[1];
  holdingSocValid = true;
  holdingSocUpdatedAt = millis();
  return true;
}

bool writeAndVerifyMaxSoc(uint16_t requestedRaw, bool countTowardDailyLimit = true) {
  requestedRaw = constrain(requestedRaw,
                           static_cast<uint16_t>(ConfigDefaults::MIN_MAX_SOC_PERCENT * 10U),
                           static_cast<uint16_t>(1000));
  if (holdingSocValid) requestedRaw = max(requestedRaw, holdingMinSocRaw);
  if (!writeHoldingRegisterConfigured(ConfigDefaults::MAX_SOC_HOLDING_ADDRESS, requestedRaw)) return false;
  delay(80);
  uint16_t verify = 0;
  if (!readHoldingRegistersConfigured(ConfigDefaults::MAX_SOC_HOLDING_ADDRESS, 1, &verify) || verify != requestedRaw) {
    lastTransactionError = "Max-SOC-Schreibwert konnte nicht verifiziert werden";
    return false;
  }
  holdingMaxSocRaw = verify;
  holdingSocValid = true;
  holdingSocUpdatedAt = millis();
  lastWrittenMaxSocRaw = verify;
  lastMaxSocWriteAt = millis();
  if (countTowardDailyLimit) {
    ++maxSocWritesToday;
  } else {
    ++priorityMaxSocWritesToday;
  }
  debugPrintf("Max-SOC sicher geschrieben und gelesen: %.1f %%.\n", verify / 10.0f);
  return true;
}

uint8_t effectiveMaxSocWritesPerDay() {
  const uint8_t step = max<uint8_t>(1, config.socStepPercent);
  const uint8_t controlledRange = config.maxSoc > ConfigDefaults::MIN_MAX_SOC_PERCENT
      ? config.maxSoc - ConfigDefaults::MIN_MAX_SOC_PERCENT
      : 0;
  // Genug normale Schreibvorgaenge zulassen, um den gesamten steuerbaren
  // Bereich in der eingestellten Schrittweite abfahren zu koennen. Die vom
  // Benutzer gewaehlte Grenze bleibt wirksam, wenn sie hoeher liegt.
  const uint8_t requiredWrites = controlledRange == 0
      ? 1
      : static_cast<uint8_t>((controlledRange + step - 1U) / step);
  return max(config.maximumWritesPerDay, requiredWrites);
}

float quantizedForecastSoc(float scheduledSoc) {
  scheduledSoc = constrain(scheduledSoc,
                           static_cast<float>(ConfigDefaults::MIN_MAX_SOC_PERCENT),
                           static_cast<float>(config.maxSoc));
  const uint8_t step = max<uint8_t>(1, config.socStepPercent);
  const float offset = scheduledSoc - ConfigDefaults::MIN_MAX_SOC_PERCENT;
  const float quantized = ConfigDefaults::MIN_MAX_SOC_PERCENT
      + ceilf(max(0.0f, offset) / step) * step;
  return min(quantized, static_cast<float>(config.maxSoc));
}

uint16_t desiredMaxSocRaw() {
  const float actualSoc = currentBatterySoc();
  float desired = config.maxSoc;
  if (config.forecastEnabled && !config.forecastBypass && forecastIsFresh()) {
    calculateForecastPlan();
    // Nur der zeitliche Prognosefahrplan darf die naechste SOC-Stufe
    // freigeben. Das Erreichen der aktuellen Grenze selbst darf nicht wie
    // frueher ueber "aktueller SOC + 1 %" die Folgestufe ausloesen.
    desired = quantizedForecastSoc(max(forecast.plannedSoc, forecast.reachableSoc));

    // Einen bereits erreichten Batteriestand nicht durch einen kleineren
    // Holdingwert unterschreiten. Der Ist-SOC wird bewusst erst NACH der
    // Fahrplan-Rasterung beruecksichtigt, damit ein geringes Ueberschwingen
    // nicht auf die naechste SOC-Stufe aufgerundet wird.
    if (isfinite(actualSoc)) {
      desired = max(desired, min(actualSoc, static_cast<float>(config.maxSoc)));
    }
  }
  desired = constrain(desired, static_cast<float>(ConfigDefaults::MIN_MAX_SOC_PERCENT),
                      static_cast<float>(config.maxSoc));
  forecast.requestedSoc = desired;
  return static_cast<uint16_t>(lroundf(desired * 10.0f));
}

void serviceForecastCharging() {
  if (!config.forecastEnabled && !forecastRestorePending) return;
  if (config.forecastEnabled && timeReached(nextForecastFetchAt) && WiFi.status() == WL_CONNECTED && clockIsValid()) {
    fetchOpenMeteoForecast();
  }
  if (!timeReached(nextForecastControlAt) || pollingCycleActive) return;
  nextForecastControlAt = millis() + ConfigDefaults::FORECAST_CONTROL_INTERVAL_MS;

  tm parts = {};
  if (clockIsValid() && localCalendar(time(nullptr), parts) && parts.tm_yday != maxSocWriteYearDay) {
    maxSocWriteYearDay = parts.tm_yday;
    maxSocWritesToday = 0;
    priorityMaxSocWritesToday = 0;
    finalMaxSocReleasedToday = false;
  }
  if (!readSocHoldingRegisters()) {
    forecast.status = "Holding-Register nicht lesbar: " + lastTransactionError;
    debugPrintln(forecast.status);
    return;
  }
  const bool forecastRequested = config.forecastEnabled && !config.forecastBypass && forecastIsFresh();
  const bool directSocAvailable = isfinite(currentBatterySoc());
  const bool controlling = forecastRequested && directSocAvailable;
  const bool finalReleaseDue = controlling && forecast.finishAt > 0
      && time(nullptr) >= forecast.finishAt && !finalMaxSocReleasedToday;
  const uint16_t configuredMaxSocRaw = static_cast<uint16_t>(config.maxSoc) * 10U;
  const uint16_t desiredRaw = finalReleaseDue
      ? configuredMaxSocRaw
      : (controlling ? desiredMaxSocRaw() : configuredMaxSocRaw);
  if (finalReleaseDue) forecast.requestedSoc = config.maxSoc;
  if (controlling) forecastRestorePending = true;
  if (forecastRequested && !directSocAvailable) {
    forecast.status = "Sicherheitsfreigabe: absoluter Batteriestand 10743 nicht verfügbar";
    if (holdingMaxSocRaw != desiredRaw) forecastRestorePending = true;
  }
  // Außerhalb der aktiven Prognoseregelung darf nur ein ausdrücklich
  // vorgemerkter Einmalauftrag schreiben. Abweichungen, die später etwa über
  // die Sungrow-App entstehen, werden im Bypass lediglich gelesen.
  if (!controlling && !forecastRestorePending) {
    if (config.forecastBypass && !forecast.status.startsWith("Bypass: externe")) {
      forecast.status = "Bypass aktiv: Max-SOC-Einmalauftrag erledigt; keine weiteren Schreibzugriffe";
    }
    return;
  }
  const uint16_t difference = holdingMaxSocRaw > desiredRaw ? holdingMaxSocRaw - desiredRaw : desiredRaw - holdingMaxSocRaw;
  const uint16_t normalDifference = static_cast<uint16_t>(config.socStepPercent) * 10U;
  const bool oneShotWrite = !controlling && forecastRestorePending;
  const bool priorityWrite = oneShotWrite || finalReleaseDue;
  if (oneShotWrite && difference == 0) {
    forecastRestorePending = false;
    preferences.begin("sungrow", false);
    preferences.putBool("fcrestore", false);
    preferences.end();
    if (forecastRequested && !directSocAvailable) {
      forecast.status = "Prognose pausiert: Max-SOC sicher freigegeben; absoluter Batteriestand 10743 fehlt";
    }
    return;
  }
  if (finalReleaseDue && difference == 0) {
    finalMaxSocReleasedToday = true;
    forecast.status = "Tages-Max-SOC war bereits vollständig freigegeben";
    return;
  }
  if (!priorityWrite && difference < normalDifference) {
    forecast.status = "Fahrplan hält die aktuelle SOC-Freigabe; nächste Stufe folgt zeitbasiert";
    return;
  }
  if (!priorityWrite && maxSocWritesToday >= effectiveMaxSocWritesPerDay()) {
    forecast.status = "Tageslimit der Max-SOC-Schreibzugriffe erreicht";
    return;
  }
  const uint32_t minimumInterval = static_cast<uint32_t>(config.minimumWriteMinutes) * 60UL * 1000UL;
  if (!priorityWrite && lastMaxSocWriteAt != 0 && millis() - lastMaxSocWriteAt < minimumInterval) return;
  const bool writeApplied = writeAndVerifyMaxSoc(desiredRaw, !priorityWrite);

  if (writeApplied) {
    if (finalReleaseDue) {
      finalMaxSocReleasedToday = true;
      forecast.status = "Tages-Max-SOC abschließend geschrieben und verifiziert";
      debugPrintln(forecast.status);
    } else if (oneShotWrite) {
      forecastRestorePending = false;
      preferences.begin("sungrow", false);
      preferences.putBool("fcrestore", false);
      preferences.end();
      forecast.status = config.forecastBypass
          ? "Bypass: Max-SOC einmalig geschrieben und verifiziert; weitere Änderungen werden nicht überschrieben"
          : "Max-SOC einmalig wiederhergestellt und verifiziert";
      debugPrintln(forecast.status);
    } else {
      forecast.status = "Zeitbasierte SOC-Stufe geschrieben und verifiziert: "
                      + String(holdingMaxSocRaw / 10.0f, 1) + " %";
      debugPrintln(forecast.status);
    }
  } else {
    forecast.status = "Max-SOC-Schreiben fehlgeschlagen: " + lastTransactionError;
    debugPrintln(forecast.status);
  }
}

void addNoCacheHeaders() {
  server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate");
  server.sendHeader("Pragma", "no-cache");
}

void handleIndex() {
  addNoCacheHeaders();
  server.send_P(200, "text/html; charset=utf-8", INDEX_HTML);
}

String gpioSelectOptions(uint8_t selectedGpio) {
  String options;
  options.reserve(700);
  for (size_t i = 0; i < SAFE_OUTPUT_GPIO_COUNT; ++i) {
    options += F("<option value='");
    options += String(SAFE_OUTPUT_GPIOS[i]);
    options += '\'';
    if (SAFE_OUTPUT_GPIOS[i] == selectedGpio) options += F(" selected");
    options += '>';
    options += F("GPIO ");
    options += String(SAFE_OUTPUT_GPIOS[i]);
    options += F("</option>");
  }
  return options;
}

String rs485RxSelectOptions(uint8_t selectedGpio) {
  String options;
  options.reserve(900);
  for (size_t i = 0; i < SAFE_RS485_RX_GPIO_COUNT; ++i) {
    options += F("<option value='");
    options += String(SAFE_RS485_RX_GPIOS[i]);
    options += '\'';
    if (SAFE_RS485_RX_GPIOS[i] == selectedGpio) options += F(" selected");
    options += F(">GPIO ");
    options += String(SAFE_RS485_RX_GPIOS[i]);
    if (!isSafeOutputGpio(SAFE_RS485_RX_GPIOS[i])) options += F(" (nur Eingang)");
    options += F("</option>");
  }
  return options;
}

String rs485DirectionSelectOptions(uint8_t selectedGpio) {
  String options = F("<option value='255'");
  if (selectedGpio == ConfigDefaults::RS485_AUTO_DIRECTION) options += F(" selected");
  options += F(">Nicht verwendet / Automatikmodul</option>");
  options += gpioSelectOptions(selectedGpio);
  return options;
}

void sendChunk(const String &text);

void handleSettings() {
  addNoCacheHeaders();
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html; charset=utf-8", "");
  sendChunk(FPSTR(SETTINGS_HEAD));

  String part;
  part.reserve(3000);
  part = F("<form id='settingsForm' method='post' action='/save'><section class='card'><h2>WLAN</h2><div class='grid'><div class='full'><label for='ssid'>WLAN-Name (SSID)</label><input id='ssid' name='ssid' maxlength='32' value='");
  part += htmlEscape(config.wifiSsid);
  part += F("' autocomplete='off'></div><div class='full'><label for='password'>WLAN-Passwort</label><input id='password' name='password' type='password' maxlength='64' placeholder='Leer lassen, um das gespeicherte Passwort beizubehalten'><p class='hint'>Das gespeicherte Passwort wird nicht im Browser angezeigt.</p></div><div class='full'><label class='check'><input type='checkbox' name='clearWifi' value='1'> Gespeicherte WLAN-Zugangsdaten löschen</label></div></div></section>");
  part += F("<section class='card'><h2>Modbus-Verbindung</h2><div class='grid'><div class='full'><label for='mbMode'>Betriebsart</label><select id='mbMode' name='mbMode'><option value='0'");
  if (selectedModbusMode() == ModbusMode::TCP_ONLY) part += F(" selected");
  part += F(">Nur Modbus TCP</option><option value='1'");
  if (selectedModbusMode() == ModbusMode::RTU_ONLY) part += F(" selected");
  part += F(">Nur Modbus RTU über RS485</option><option value='2'");
  if (selectedModbusMode() == ModbusMode::TCP_WITH_RTU_FALLBACK) part += F(" selected");
  part += F(">TCP bevorzugt, RS485 als Ersatz</option></select><p class='hint'>Im kombinierten Modus wird jeder Block zuerst über TCP gelesen. Schlägt die TCP-Anfrage fehl, wird derselbe Block automatisch über RS485 versucht.</p></div><div><label for='invUnit'>Wechselrichter-ID</label><input id='invUnit' name='invUnit' type='number' min='1' max='247' value='");
  part += String(config.inverterUnit);
  part += F("'></div><div><label for='poll'>Abfrageintervall (Sekunden)</label><input id='poll' name='poll' type='number' min='2' max='300' value='");
  part += String(config.pollSeconds);
  part += F("'></div><div id='modbusTcpFields' class='grid full'><div class='full'><label for='host'>TCP-IP-Adresse oder Hostname</label><input id='host' name='host' maxlength='64' value='");
  part += htmlEscape(config.modbusHost);
  part += F("'></div><div><label for='port'>Port</label><input id='port' name='port' type='number' min='1' max='65535' value='");
  part += String(config.modbusPort);
  part += F("'></div><div><label for='batTcpUnit'>Batterie-ID über TCP</label><input id='batTcpUnit' name='batTcpUnit' type='number' min='1' max='247' value='");
  part += String(config.batteryTcpUnit);
  part += F("'><p class='hint'>Bei Sungrow normalerweise ID 2.</p></div></div><div id='modbusRtuFields' class='grid full'><div><label for='batRtuUnit'>Batterie-ID über RS485</label><input id='batRtuUnit' name='batRtuUnit' type='number' min='1' max='247' value='");
  part += String(config.batteryRtuUnit);
  part += F("'><p class='hint'>Bei Sungrow normalerweise ID 200.</p></div><div><label for='rBaud'>RS485-Baudrate</label><input id='rBaud' name='rBaud' type='number' min='1200' max='115200' value='");
  part += String(config.rs485Baud);
  part += F("'></div><div><label for='rRx'>RX-Pin (RO)</label><select id='rRx' name='rRx'>");
  part += rs485RxSelectOptions(config.rs485RxPin);
  part += F("</select></div><div><label for='rTx'>TX-Pin (DI)</label><select id='rTx' name='rTx'>");
  part += gpioSelectOptions(config.rs485TxPin);
  part += F("</select></div><div><label for='rDe'>DE/RE-Richtungspin</label><select id='rDe' name='rDe'>");
  part += rs485DirectionSelectOptions(config.rs485DePin);
  part += F("</select></div><p class='hint full'>Datenformat: 8N1. Bei 3,3-V-Transceivern wie MAX3485/SP3485 DE und /RE miteinander verbinden und den verwendeten GPIO auswählen. Bei Modulen mit automatischer Senderichtung „Nicht verwendet“ wählen. RO → RX, DI → TX; A/B werden mit dem RS485-Bus verbunden. Keine 5-V-Signale direkt an den ESP32 anschließen. Der absolute Batteriestand 10743 wird über TCP-ID 2 oder RS485-ID 200 gelesen und ist für die Prognoseregelung erforderlich.</p></div></div></section>");
  part += F("<section class='card'><h2>Prognosebasiertes Laden</h2><div class='grid'><div class='full'><label for='forecastMode'>Betriebsart</label><select id='forecastMode' name='forecastMode'><option value='1'");
  if (config.forecastEnabled && !config.forecastBypass) part += F(" selected");
  part += F(">Prognose aktiv</option><option value='0'");
  if (!config.forecastEnabled || config.forecastBypass) part += F(" selected");
  part += F(">Bypass aktiv – Max-SOC einmalig freigeben</option></select><p class='hint'>Beim Wechsel auf Bypass wird der eingestellte Max-SOC genau einmal geschrieben und durch Rücklesen geprüft. Danach erfolgen keine weiteren Max-SOC-Schreibzugriffe; spätere Änderungen über die Sungrow-App bleiben bestehen. Erst ein erneuter Wechsel auf Bypass oder eine Änderung des eingestellten Max-SOC erzeugt einen neuen Einmalauftrag.</p></div><div class='caution full'><b>Wichtig bei Wartung und Inbetriebnahme:</b> Vor Wartungs-, Service-, Umbau- oder Inbetriebnahmearbeiten an Wechselrichter oder Batteriesystem auf Bypass umschalten und speichern. Besonders wichtig ist dies bei Batterieerweiterungen, weil das System zur Angleichung automatisch bis ungefähr 40 % laden oder entladen kann. Prognose erst nach vollständig abgeschlossenen und durch den Fachbetrieb freigegebenen Arbeiten wieder aktivieren. Hersteller- und Fachbetriebsvorgaben haben Vorrang.</div><label>Breitengrad<input name='lat' type='number' step='0.000001' min='-90' max='90' value='");
  part += String(config.latitude, 6);
  part += F("'></label><label>Längengrad<input name='lon' type='number' step='0.000001' min='-180' max='180' value='");
  part += String(config.longitude, 6);
  part += F("'></label><div class='full'><p class='hint'>Für den Max-SOC kann ein Wert zwischen <b>50 und 100 %</b> eingetragen werden.</p></div><label>Max-SOC (%)<input name='maxsoc' type='number' min='50' max='100' value='");
  part += String(config.maxSoc);
  part += F("'><p class='hint'>Das ist der einzige SOC-Sollwert: Im Bypass wird er sofort freigegeben, in der Prognose wird die Ladegrenze schrittweise bis genau zu diesem Wert angehoben.</p></label><label>Fertig vor Sonnenuntergang (min)<input name='finbuffer' type='number' min='0' max='360' value='");
  part += String(config.finishBufferMinutes);
  part += F("'></label><label>Prognose-Sicherheitsfaktor (%)<input name='fcsafe' type='number' min='40' max='100' value='");
  part += String(config.forecastSafetyPercent);
  part += F("'></label><label>PV-Systemwirkungsgrad (%)<input name='pveff' type='number' min='40' max='100' value='");
  part += String(config.pvSystemEfficiencyPercent);
  part += F("'></label><label>Batterie-Ladewirkungsgrad (%)<input name='bateff' type='number' min='50' max='100' value='");
  part += String(config.batteryChargeEfficiencyPercent);
  part += F("'></label><label>SOC-Schrittweite (%)<input name='socstep' type='number' min='1' max='20' value='");
  part += String(config.socStepPercent);
  part += F("'><p class='hint'>Empfohlener Mindestwert: 3 %. Verpasste Fahrplanstufen werden nicht abgewartet; es gilt immer der zur aktuellen Uhrzeit vorgesehene Wert.</p></label><div class='caution full'><b>Achtung bei weniger als 3 %:</b> Kleinere Schritte erzeugen deutlich mehr Schreibzugriffe. Da der Max-SOC dauerhaft im Wechselrichter erhalten bleibt, kann eine zusätzliche Belastung seines nichtflüchtigen Speichers nicht ausgeschlossen werden. Nutzung kleinerer Werte auf eigene Verantwortung.</div><label>Mindestabstand Schreibzugriffe (min)<input name='writemin' type='number' min='5' max='240' value='");
  part += String(config.minimumWriteMinutes);
  part += F("'></label><label>Gewünschte Schreibobergrenze pro Tag<input name='writemax' type='number' min='1' max='48' value='");
  part += String(config.maximumWritesPerDay);
  part += F("'><p class='hint'>Falls für den Bereich 50 % bis Max-SOC mehr Schritte nötig sind, wird die Tagesgrenze automatisch entsprechend angehoben.</p></label><label>Open-Meteo-Intervall (h)<input name='fetchhrs' type='number' min='1' max='12' value='");
  part += String(config.forecastFetchHours);
  part += F("'></label><label>Prognose gilt als veraltet nach (h)<input name='stalehrs' type='number' min='1' max='24' value='");
  part += String(config.forecastStaleHours);
  part += F("'></label><label>Lernrate Lastprofil (%)<input name='learnpct' type='number' min='1' max='100' value='");
  part += String(config.learningPercent);
  part += F("'></label><label>Schwelle wiederkehrende Großlast (W)<input name='eventw' type='number' min='250' max='20000' value='");
  part += String(config.eventLoadThresholdWatts);
  part += F("'></label><p class='hint full'>Max-SOC wird an Holding-Adresse 13057 (Herstellerregister 13058) mit 0,1-%-Skalierung geschrieben und danach zurückgelesen. Die Regelung schreibt ausschließlich Werte von 50 bis 100 % und nie unter den aktuellen direkten SOC oder unter Min-SOC. Das Erreichen einer freigegebenen SOC-Stufe löst nicht selbst die nächste Stufe aus; diese folgt ausschließlich dem zeitlichen Prognosefahrplan.</p></div><h3>Dachflächen</h3>");
  sendChunk(part);
  for (size_t i = 0; i < PV_ARRAY_COUNT; ++i) {
    const PvArrayConfig &array = config.pvArrays[i];
    const String n = String(i + 1);
    String roof;
    roof.reserve(1000);
    roof = F("<details class='stage-card'");
    if (array.enabled) roof += F(" open");
    roof += F("><summary>Dachfläche "); roof += n; roof += F(" · "); roof += htmlEscape(array.name);
    roof += F("</summary><div class='stage-body'><div class='grid'><label class='check full'><input type='checkbox' name='pv"); roof += n; roof += F("e' value='1'");
    if (array.enabled) roof += F(" checked");
    roof += F("> Dachfläche aktiv</label><label>Name<input name='pv"); roof += n; roof += F("n' maxlength='28' value='"); roof += htmlEscape(array.name);
    roof += F("'></label><label>Installierte Leistung (kWp)<input name='pv"); roof += n; roof += F("k' type='number' min='0' max='1000' step='0.01' value='"); roof += String(array.peakKwp, 2);
    roof += F("'></label><label>Neigung (0–90°)<input name='pv"); roof += n; roof += F("t' type='number' min='0' max='90' value='"); roof += String(array.tiltDegrees);
    roof += F("'></label><label>Ausrichtung<input name='pv"); roof += n; roof += F("a' type='number' min='-180' max='180' value='"); roof += String(array.azimuthDegrees);
    roof += F("'><p class='hint'>Open-Meteo: 0° Süd, −90° Ost, +90° West, ±180° Nord.</p></label></div></div></details>");
    sendChunk(roof);
  }
  part = F("</section><section class='card'><h2>Rundsteuerempfänger</h2><div class='grid'><label class='check full'><input type='checkbox' name='rcen' value='1'");
  if (config.rippleEnabled) part += F(" checked");
  part += F("> Einspeisebegrenzung über Rundsteuerempfänger aktivieren</label><div class='full'><label for='rcmode'>Signalart</label><select id='rcmode' name='rcmode'><option value='3'");
  if (config.rippleSignalMode == 3) part += F(" selected");
  part += F(">3 Kontakte – kein Signal bedeutet 100 %</option><option value='4'");
  if (config.rippleSignalMode == 4) part += F(" selected");
  part += F(">4 Kontakte – auch 100 % hat ein eigenes Signal</option></select></div><p class='hint full'>Im 3-Kontakt-Betrieb müssen drei Eingänge verwendet werden; die Ruhestellung ohne Signal wird als 100 % ausgewertet. Im 4-Kontakt-Betrieb wird 100 % wie jede andere Stufe durch einen Eingang gemeldet; ohne aktiven Eingang bleibt die letzte Wechselrichtereinstellung unverändert. Die installierte Leistung ist die Summe aller oben aktivierten Dachflächen. 13073 erhält Leistung × Prozent direkt in Watt, 13086 aktiviert die Begrenzung mit 0xAA. Sind mehrere Eingänge aktiv, gilt der niedrigste Prozentwert. Der aktive HIGH-/LOW-Pegel wird je Eingang festgelegt.</p></div>");
  sendChunk(part);
  for (size_t i = 0; i < RIPPLE_INPUT_COUNT; ++i) {
    const RippleInputConfig &input = config.rippleInputs[i];
    const String n = String(i + 1);
    String row;
    row.reserve(1300);
    row = F("<details class='stage-card'");
    if (input.enabled) row += F(" open");
    row += F("><summary>Schaltstufe "); row += n; row += F(" · "); row += String(input.percent); row += F(" %</summary><div class='stage-body'><div class='grid'><label class='check full'><input type='checkbox' name='rc");
    row += n; row += F("e' value='1'"); if (input.enabled) row += F(" checked");
    row += F("> Eingang verwenden</label><label>GPIO-Eingang<select name='rc"); row += n; row += F("g'>");
    row += rs485RxSelectOptions(input.gpio);
    row += F("</select></label><label>Begrenzung (%)<input name='rc"); row += n;
    row += F("p' type='number' min='0' max='100' value='"); row += String(input.percent);
    row += F("'></label><label>Aktiver Pegel<select name='rc"); row += n; row += F("l'><option value='0'");
    if (input.activeLow) row += F(" selected");
    row += F(">LOW / Kontakt nach GND</option><option value='1'");
    if (!input.activeLow) row += F(" selected");
    row += F(">HIGH / 3,3-V-Signal</option></select></label></div></div></details>");
    sendChunk(row);
  }
  part = F("<p class='hint'><b>Anschluss:</b> Nur potentialfreie Relaiskontakte gegen GND oder saubere 3,3-V-Logik verwenden. Niemals Netzspannung oder 5 V an den ESP32 legen. GPIO 34–39 benötigen einen externen Pull-Widerstand. Beim Abschalten der Funktion wird 13086 einmalig mit 0x55 beschrieben und zurückgelesen.</p></section><section class='card'><h2>Überschuss-Stufen</h2><p class='hint'>Nur aktivierte Positionen werden in der Reihenfolge 1 bis 5 kumuliert. Beispiel: Sind nur Stufe 1 und 5 aktiv, liegt die Schaltschwelle von Stufe 5 bei Leistung 1 plus Leistung 5. Zusätzlich bleibt eine Leistungshysterese von 150 W aktiv. Wenn der Batterie-SOC dem Tagesplan hinterherläuft, reserviert die Prognoselogik einen Teil des Überschusses für die Batterie.</p>");
  sendChunk(part);

  for (size_t i = 0; i < SURPLUS_STAGE_COUNT; ++i) {
    const SurplusStageConfig &stage = surplusStages[i];
    const String n = String(i + 1);
    String card;
    card.reserve(4200 + stage.onUrl.length() + stage.offUrl.length() + stage.onJson.length() + stage.offJson.length());
    card = F("<details class='stage-card'");
    if (stage.enabled) card += F(" open");
    card += F("><summary>Stufe ");
    card += n;
    card += stage.enabled ? F(" · aktiviert") : F(" · deaktiviert");
    card += F("</summary><div class='stage-body'><div class='grid'><label class='check full'><input type='checkbox' name='s");
    card += n;
    card += F("e' value='1'");
    if (stage.enabled) card += F(" checked");
    card += F("> Stufe aktiviert</label><label>Leistung (W)<input name='s");
    card += n;
    card += F("w' type='number' min='1' max='20000' value='");
    card += String(stage.watts);
    card += F("'></label><label>Typ<select name='s");
    card += n;
    card += F("t' data-stage-type='");
    card += n;
    card += F("'><option value='0'");
    if (stage.type == StageType::GPIO) card += F(" selected");
    card += F(">GPIO</option><option value='1'");
    if (stage.type == StageType::API) card += F(" selected");
    card += F(">Web-API</option></select></label><label>Einschaltverzögerung (s)<input name='s");
    card += n;
    card += F("di' type='number' min='0' max='3600' value='");
    card += String(stage.onDelaySeconds);
    card += F("'></label><label>Ausschaltverzögerung (s)<input name='s");
    card += n;
    card += F("do' type='number' min='0' max='3600' value='");
    card += String(stage.offDelaySeconds);
    card += F("'></label></div><div class='type-panel' id='s");
    card += n;
    card += F("-gpio'><label>GPIO-Ausgang<select name='s");
    card += n;
    card += F("g'>");
    card += gpioSelectOptions(stage.gpio);
    card += F("</select></label><p class='hint'>HIGH = EIN, LOW = AUS. Nur geeignete Ausgangspins des klassischen ESP32-WROOM/DevKit werden angeboten.</p></div><div class='type-panel' id='s");
    card += n;
    card += F("-api'><div class='grid'><label>Methode<select name='s");
    card += n;
    card += F("m' data-stage-method='");
    card += n;
    card += F("'><option value='0'");
    if (stage.method == ApiMethod::GET) card += F(" selected");
    card += F(">GET</option><option value='1'");
    if (stage.method == ApiMethod::PUT) card += F(" selected");
    card += F(">PUT</option><option value='2'");
    if (stage.method == ApiMethod::POST) card += F(" selected");
    card += F(">POST</option></select></label><div></div><label class='full'>EIN-URL<input name='s");
    card += n;
    card += F("uon' maxlength='255' placeholder='http://192.168.1.80/api/power' value='");
    card += htmlEscape(stage.onUrl);
    card += F("'></label><label class='full' data-json='");
    card += n;
    card += F("'>EIN-JSON<textarea name='s");
    card += n;
    card += F("jon' maxlength='1000' placeholder='{&quot;power&quot;:true}'>");
    card += htmlEscape(stage.onJson);
    card += F("</textarea></label><label class='full'>AUS-URL<input name='s");
    card += n;
    card += F("uoff' maxlength='255' placeholder='http://192.168.1.80/api/power' value='");
    card += htmlEscape(stage.offUrl);
    card += F("'></label><label class='full' data-json='");
    card += n;
    card += F("'>AUS-JSON<textarea name='s");
    card += n;
    card += F("joff' maxlength='1000' placeholder='{&quot;power&quot;:false}'>");
    card += htmlEscape(stage.offJson);
    card += F("</textarea></label></div><p class='hint'>GET ignoriert die JSON-Felder. Unterstützt werden lokale unverschlüsselte URLs mit <b>http://</b>.</p></div></div></details>");
    sendChunk(card);
  }

  part = F("<p class='hint'><b>Wichtig:</b> GPIOs dürfen Heizpatrone, Klimaanlage oder andere Netzlasten niemals direkt schalten. Verwende passend dimensionierte Relais, SSRs oder Schütze und lasse die Netzseite fachgerecht installieren.</p></section><section class='card'><h2>Pushover-Benachrichtigungen</h2><div class='grid'><label class='check full'><input type='checkbox' name='poEnabled' value='1'");
  if (config.pushoverEnabled) part += F(" checked");
  part += F("> Meldungen an Pushover aktivieren</label><div class='full'><p class='hint'>Benachrichtigt beim Start/Neustart, nach einem anhaltenden Modbus-Ausfall und nach Wiederherstellung der Verbindung. Für jedes Gerät bzw. jede Installation sollte in Pushover eine eigene Anwendung angelegt werden.</p></div><label>Application/API Token<input id='poToken' name='poToken' type='password' maxlength='30' pattern='[A-Za-z0-9]{30}' autocomplete='new-password' data-stored='");
  part += isPushoverCredential(config.pushoverAppToken) ? F("1") : F("0");
  part += F("' placeholder='");
  part += isPushoverCredential(config.pushoverAppToken)
      ? F("Gespeichert – leer lassen zum Beibehalten")
      : F("30-stelliger App-Token");
  part += F("'><p class='hint'>Wird nach dem Speichern nicht wieder im Browser angezeigt.</p></label><label>User-/Group-Key<input id='poUser' name='poUser' type='password' maxlength='30' pattern='[A-Za-z0-9]{30}' autocomplete='new-password' data-stored='");
  part += isPushoverCredential(config.pushoverUserKey) ? F("1") : F("0");
  part += F("' placeholder='");
  part += isPushoverCredential(config.pushoverUserKey)
      ? F("Gespeichert – leer lassen zum Beibehalten")
      : F("30-stelliger User- oder Group-Key");
  part += F("'><p class='hint'>Wird nach dem Speichern nicht wieder im Browser angezeigt.</p></label><label>Gerät (optional)<input id='poDevice' name='poDevice' maxlength='25' pattern='[A-Za-z0-9_-]{0,25}' value='");
  part += htmlEscape(config.pushoverDevice);
  part += F("' placeholder='z. B. marcus-phone'></label><label>Ausfall melden nach (s)<input name='poDelay' type='number' min='10' max='3600' value='");
  part += String(config.pushoverFailureDelaySeconds);
  part += F("'></label><label>Wiederholungsversuch nach Sendefehler (min)<input name='poRetry' type='number' min='1' max='1440' value='");
  part += String(config.pushoverRetryMinutes);
  part += F("'></label><div class='full'><label class='check'><input type='checkbox' name='poClear' value='1'> Gespeicherte Pushover-Zugangsdaten löschen</label></div><div class='full'><button id='poTest' class='button secondary' type='button'>Testnachricht senden</button><span id='poResult' class='result'>");
  part += htmlEscape(lastPushoverStatus);
  part += F("</span><p class='hint'>Die Testnachricht verwendet neue Eingaben direkt, ohne sie zu speichern; leere Felder verwenden bereits gespeicherte Zugangsdaten. Die Verbindung zu Pushover wird per HTTPS mit Zertifikatsprüfung aufgebaut. Ist ESP32, Router oder Internet stromlos, kann keine Sofortmeldung versendet werden; nach dem Neustart folgt die Startmeldung.</p></div></div></section><button class='button' type='submit'>Speichern und neu starten</button></form><section class='card info'><h2>Verbindung</h2><b>Access Point:</b> ");
  part += htmlEscape(accessPointSsid);
  part += F("<br><b>AP-Passwort:</b> ");
  part += ConfigDefaults::AP_PASSWORD;
  part += F("<br><b>AP-Adresse:</b> ");
  part += WiFi.softAPIP().toString();
  part += F("<br><b>Netzwerkadresse:</b> ");
  part += WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : String("nicht verbunden");
  part += F("<br><b>mDNS:</b> http://");
  part += ConfigDefaults::MDNS_NAME;
  part += F(".local</section><script>function toggleModbus(){const mode=document.querySelector('#mbMode').value,tcp=mode!=='1',rtu=mode!=='0';document.querySelector('#modbusTcpFields').classList.toggle('hidden',!tcp);document.querySelector('#modbusRtuFields').classList.toggle('hidden',!rtu);document.querySelector('#host').required=tcp}function toggleStage(i){const t=document.querySelector('[data-stage-type=\"'+i+'\"]');const api=t.value==='1';document.querySelector('#s'+i+'-gpio').classList.toggle('hidden',api);document.querySelector('#s'+i+'-api').classList.toggle('hidden',!api);const m=document.querySelector('[data-stage-method=\"'+i+'\"]');document.querySelectorAll('[data-json=\"'+i+'\"]').forEach(x=>x.classList.toggle('hidden',m.value==='0'))}document.querySelector('#mbMode').addEventListener('change',toggleModbus);toggleModbus();for(let i=1;i<=5;i++){document.querySelector('[data-stage-type=\"'+i+'\"]').addEventListener('change',()=>toggleStage(i));document.querySelector('[data-stage-method=\"'+i+'\"]').addEventListener('change',()=>toggleStage(i));toggleStage(i)}document.querySelector('#poTest').addEventListener('click',async()=>{const f=document.querySelector('#settingsForm'),out=document.querySelector('#poResult');out.textContent='Wird gesendet …';const body=new URLSearchParams({token:f.elements.poToken.value,user:f.elements.poUser.value,device:f.elements.poDevice.value});try{const r=await fetch('/api/pushover/test',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body});out.textContent=await r.text()}catch(e){out.textContent='Fehler: '+e.message}});document.querySelector('#settingsForm').addEventListener('submit',function(e){const used=new Set(),rtu=this.elements.mbMode.value!=='0';if(this.elements.poEnabled.checked&&!this.elements.poClear.checked){if(!this.elements.poToken.value&&this.elements.poToken.dataset.stored!=='1'||!this.elements.poUser.value&&this.elements.poUser.dataset.stored!=='1'){e.preventDefault();alert('Für aktive Pushover-Meldungen werden Application/API Token und User-/Group-Key benötigt.');return}}if(rtu){const rx=this.elements.rRx.value,tx=this.elements.rTx.value,de=this.elements.rDe.value;if(rx===tx||rx===de||tx===de){e.preventDefault();alert('RX, TX und DE/RE müssen unterschiedliche GPIOs verwenden.');return}used.add(rx);used.add(tx);if(de!=='255')used.add(de)}for(let i=1;i<=5;i++){if(!this.elements['s'+i+'e'].checked)continue;if(this.elements['s'+i+'t'].value==='0'){const gpio=this.elements['s'+i+'g'].value;if(used.has(gpio)){e.preventDefault();alert('GPIO '+gpio+' ist bereits durch RS485 oder eine andere aktive Stufe belegt.');return}used.add(gpio)}else if(!this.elements['s'+i+'uon'].value.startsWith('http://')||!this.elements['s'+i+'uoff'].value.startsWith('http://')){e.preventDefault();alert('Stufe '+i+': Für eine aktive Web-API werden eine EIN- und AUS-URL mit http:// benötigt.');return}}if(this.elements.rcen.checked){const rcEnabled=[1,2,3,4].filter(i=>this.elements['rc'+i+'e'].checked),rcMode=Number(this.elements.rcmode.value);if(rcEnabled.length!==rcMode){e.preventDefault();alert('Für den '+rcMode+'-Kontakt-Betrieb müssen genau '+rcMode+' Eingänge aktiviert sein.');return}if(rcMode===4&&!rcEnabled.some(i=>Number(this.elements['rc'+i+'p'].value)===100)){e.preventDefault();alert('Im 4-Kontakt-Betrieb muss ein Eingang auf 100 % eingestellt sein.');return}for(let i=1;i<=4;i++){if(!this.elements['rc'+i+'e'].checked)continue;const gpio=this.elements['rc'+i+'g'].value;if(used.has(gpio)){e.preventDefault();alert('Rundsteuer-Eingang '+i+': GPIO '+gpio+' ist bereits belegt.');return}used.add(gpio)}const kwp=[1,2,3,4].filter(i=>this.elements['pv'+i+'e'].checked).reduce((s,i)=>s+Number(this.elements['pv'+i+'k'].value||0),0);if(kwp<=0||kwp>65.535){e.preventDefault();alert('Für die Rundsteuerung muss die aktive Dachleistung größer 0 und höchstens 65,535 kWp sein.');return}}})</script></main></body></html>");
  sendChunk(part);
  sendChunk(String());
}

uint32_t boundedNumberArgument(const String &name, uint32_t fallback, uint32_t minimum, uint32_t maximum) {
  if (!server.hasArg(name)) return fallback;
  const String value = server.arg(name);
  if (value.isEmpty()) return fallback;
  char *end = nullptr;
  const unsigned long parsed = strtoul(value.c_str(), &end, 10);
  if (end == value.c_str() || *end != '\0' || parsed < minimum || parsed > maximum) return fallback;
  return static_cast<uint32_t>(parsed);
}

float boundedFloatArgument(const String &name, float fallback, float minimum, float maximum) {
  if (!server.hasArg(name)) return fallback;
  const String value = server.arg(name);
  if (value.isEmpty()) return fallback;
  char *end = nullptr;
  const float parsed = strtof(value.c_str(), &end);
  if (end == value.c_str() || *end != '\0' || !isfinite(parsed) || parsed < minimum || parsed > maximum) return fallback;
  return parsed;
}

void handleSave() {
  const bool bypassWasActive = config.forecastEnabled && config.forecastBypass;
  const bool rippleWasEnabled = config.rippleEnabled;
  const uint8_t previousMaxSoc = config.maxSoc;
  forceAllStagesOff(true);
  if (server.hasArg("clearWifi")) {
    config.wifiSsid = "";
    config.wifiPassword = "";
  } else {
    String submittedSsid = server.arg("ssid");
    submittedSsid.trim();
    if (submittedSsid != config.wifiSsid) {
      config.wifiSsid = submittedSsid;
      config.wifiPassword = server.arg("password");
    } else if (server.hasArg("password") && !server.arg("password").isEmpty()) {
      config.wifiPassword = server.arg("password");
    }
  }

  if (server.hasArg("poClear")) {
    config.pushoverEnabled = false;
    config.pushoverAppToken = "";
    config.pushoverUserKey = "";
    config.pushoverDevice = "";
  } else {
    config.pushoverEnabled = server.hasArg("poEnabled");
    String submittedToken = server.arg("poToken");
    String submittedUser = server.arg("poUser");
    String submittedDevice = server.arg("poDevice");
    submittedToken.trim();
    submittedUser.trim();
    submittedDevice.trim();
    if (isPushoverCredential(submittedToken)) config.pushoverAppToken = submittedToken;
    if (isPushoverCredential(submittedUser)) config.pushoverUserKey = submittedUser;
    config.pushoverDevice = isPushoverDevice(submittedDevice) ? submittedDevice : String();
  }
  config.pushoverFailureDelaySeconds = static_cast<uint16_t>(boundedNumberArgument(
      "poDelay", ConfigDefaults::PUSHOVER_FAILURE_DELAY_SECONDS, 10, 3600));
  config.pushoverRetryMinutes = static_cast<uint16_t>(boundedNumberArgument(
      "poRetry", ConfigDefaults::PUSHOVER_RETRY_MINUTES, 1, 1440));

  config.modbusHost = server.arg("host");
  config.modbusHost.trim();
  if (config.modbusHost.isEmpty()) config.modbusHost = "192.168.0.2";
  config.modbusPort = static_cast<uint16_t>(boundedNumberArgument("port", ConfigDefaults::MODBUS_PORT, 1, 65535));
  config.inverterUnit = static_cast<uint8_t>(boundedNumberArgument("invUnit", ConfigDefaults::MODBUS_UNIT, 1, 247));
  config.batteryTcpUnit = static_cast<uint8_t>(boundedNumberArgument(
      "batTcpUnit", ConfigDefaults::BATTERY_TCP_UNIT, 1, 247));
  config.batteryRtuUnit = static_cast<uint8_t>(boundedNumberArgument(
      "batRtuUnit", ConfigDefaults::BATTERY_RTU_UNIT, 1, 247));
  config.pollSeconds = static_cast<uint16_t>(boundedNumberArgument("poll", ConfigDefaults::POLL_SECONDS, 2, 300));
  config.modbusMode = static_cast<uint8_t>(boundedNumberArgument("mbMode", static_cast<uint8_t>(ModbusMode::TCP_ONLY), 0, 2));
  config.rs485Baud = boundedNumberArgument("rBaud", ConfigDefaults::RS485_BAUD, 1200, 115200);
  config.rs485RxPin = static_cast<uint8_t>(boundedNumberArgument("rRx", ConfigDefaults::RS485_RX_PIN, 0, 39));
  config.rs485TxPin = static_cast<uint8_t>(boundedNumberArgument("rTx", ConfigDefaults::RS485_TX_PIN, 0, 39));
  config.rs485DePin = static_cast<uint8_t>(boundedNumberArgument("rDe", ConfigDefaults::RS485_DE_PIN, 0, 255));
  const uint8_t forecastMode = static_cast<uint8_t>(boundedNumberArgument("forecastMode", 0, 0, 1));
  config.forecastEnabled = true;
  config.forecastBypass = forecastMode == 0;
  config.latitude = boundedFloatArgument("lat", config.latitude, -90.0f, 90.0f);
  config.longitude = boundedFloatArgument("lon", config.longitude, -180.0f, 180.0f);
  config.maxSoc = static_cast<uint8_t>(boundedNumberArgument(
      "maxsoc", 100, ConfigDefaults::MIN_MAX_SOC_PERCENT, 100));
  config.finishBufferMinutes = static_cast<uint16_t>(boundedNumberArgument("finbuffer", 75, 0, 360));
  config.forecastSafetyPercent = static_cast<uint8_t>(boundedNumberArgument("fcsafe", 80, 40, 100));
  config.pvSystemEfficiencyPercent = static_cast<uint8_t>(boundedNumberArgument("pveff", 95, 40, 100));
  config.batteryChargeEfficiencyPercent = static_cast<uint8_t>(boundedNumberArgument("bateff", 95, 50, 100));
  config.socStepPercent = static_cast<uint8_t>(boundedNumberArgument("socstep", 3, 1, 20));
  config.minimumWriteMinutes = static_cast<uint16_t>(boundedNumberArgument("writemin", 20, 5, 240));
  config.maximumWritesPerDay = static_cast<uint8_t>(boundedNumberArgument("writemax", 20, 1, 48));
  config.forecastFetchHours = static_cast<uint8_t>(boundedNumberArgument("fetchhrs", 3, 1, 12));
  config.forecastStaleHours = static_cast<uint8_t>(boundedNumberArgument("stalehrs", 6, 1, 24));
  config.learningPercent = static_cast<uint8_t>(boundedNumberArgument("learnpct", 20, 1, 100));
  config.eventLoadThresholdWatts = static_cast<uint16_t>(boundedNumberArgument("eventw", 1800, 250, 20000));
  for (size_t i = 0; i < PV_ARRAY_COUNT; ++i) {
    const String prefix = "pv" + String(i + 1);
    PvArrayConfig &array = config.pvArrays[i];
    array.enabled = server.hasArg(prefix + "e");
    array.name = server.arg(prefix + "n");
    array.peakKwp = boundedFloatArgument(prefix + "k", array.peakKwp, 0.0f, 1000.0f);
    array.tiltDegrees = static_cast<uint8_t>(boundedNumberArgument(prefix + "t", 30, 0, 90));
    const int32_t azimuth = static_cast<int32_t>(boundedFloatArgument(prefix + "a", 0.0f, -180.0f, 180.0f));
    array.azimuthDegrees = static_cast<int16_t>(azimuth);
  }

  config.rippleEnabled = server.hasArg("rcen");
  config.rippleSignalMode = static_cast<uint8_t>(boundedNumberArgument("rcmode", 4, 3, 4));
  for (size_t i = 0; i < RIPPLE_INPUT_COUNT; ++i) {
    const String prefix = "rc" + String(i + 1);
    RippleInputConfig &input = config.rippleInputs[i];
    input.enabled = server.hasArg(prefix + "e");
    input.gpio = static_cast<uint8_t>(boundedNumberArgument(prefix + "g", input.gpio, 0, 39));
    input.percent = static_cast<uint8_t>(boundedNumberArgument(prefix + "p", input.percent, 0, 100));
    input.activeLow = boundedNumberArgument(prefix + "l", input.activeLow ? 0 : 1, 0, 1) == 0;
  }

  for (size_t i = 0; i < SURPLUS_STAGE_COUNT; ++i) {
    const String number = String(i + 1);
    SurplusStageConfig &stage = surplusStages[i];
    stage.watts = static_cast<uint16_t>(boundedNumberArgument("s" + number + "w", 1000, 1, 20000));
    stage.onDelaySeconds = static_cast<uint16_t>(boundedNumberArgument("s" + number + "di", 30, 0, 3600));
    stage.offDelaySeconds = static_cast<uint16_t>(boundedNumberArgument("s" + number + "do", 30, 0, 3600));
    stage.enabled = server.hasArg("s" + number + "e");
    stage.type = static_cast<StageType>(boundedNumberArgument("s" + number + "t", 0, 0, 1));
    stage.gpio = static_cast<uint8_t>(boundedNumberArgument("s" + number + "g", stage.gpio, 0, 39));
    stage.method = static_cast<ApiMethod>(boundedNumberArgument("s" + number + "m", 1, 0, 2));
    stage.onUrl = server.arg("s" + number + "uon");
    stage.offUrl = server.arg("s" + number + "uoff");
    stage.onJson = server.arg("s" + number + "jon");
    stage.offJson = server.arg("s" + number + "joff");
  }
  sanitizeModbusConfig();
  sanitizeSurplusConfig();
  sanitizeForecastConfig();
  sanitizeRippleConfig();
  if (rippleWasEnabled && !config.rippleEnabled) {
    rippleReleasePending = true;
  } else if (config.rippleEnabled) {
    rippleReleasePending = false;
  }
  const bool bypassIsActive = config.forecastEnabled && config.forecastBypass;
  if (bypassIsActive && (!bypassWasActive || config.maxSoc != previousMaxSoc)) {
    // Der Auftrag bleibt im NVS erhalten, bis Schreiben und Rücklesen einmal
    // erfolgreich waren. Danach bleibt er aus, auch wenn die App den Wert ändert.
    forecastRestorePending = true;
  } else if (!bypassIsActive && bypassWasActive) {
    // Ein noch offener Bypass-Auftrag darf nicht in den Prognosemodus gelangen.
    forecastRestorePending = false;
  }
  saveConfig();
  commitLearningBlock();
  saveStoredProfile();
  saveStoredHistory();

  const String page = F("<!doctype html><html lang='de'><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'><meta http-equiv='refresh' content='6;url=/inverter'><title>Gespeichert</title><style>body{font:16px system-ui;background:#f3f5f7;color:#17212b;display:grid;place-items:center;min-height:100vh;margin:0}.box{background:white;padding:24px;border:1px solid #dfe4e8;border-radius:12px;max-width:440px}h1{font-size:21px;margin-top:0}</style></head><body><div class='box'><h1>Einstellungen gespeichert</h1><p>Der ESP32 startet neu. Die Seite versucht anschließend, den Monitor wieder zu öffnen.</p><p>Falls sich die Netzwerkadresse ändert, verbinde dich mit dem Access Point und öffne <b>192.168.4.1</b>.</p></div></body></html>");
  server.send(200, "text/html; charset=utf-8", page);
  restartAt = millis() + 1500;
}

void handlePushoverTest() {
  addNoCacheHeaders();
  String token = server.arg("token");
  String user = server.arg("user");
  String device = server.arg("device");
  token.trim();
  user.trim();
  device.trim();
  if (token.isEmpty()) token = config.pushoverAppToken;
  if (user.isEmpty()) user = config.pushoverUserKey;

  String status;
  const bool success = sendPushoverMessage(
      token, user, device, "Solar Prognose Monitor: Test",
      "Die Pushover-Verbindung funktioniert. Firmware "
          + String(ConfigDefaults::FIRMWARE_VERSION) + ", IP " + WiFi.localIP().toString() + ".",
      status);
  lastPushoverStatus = status;
  debugPrintln("Pushover-Test: " + status + ".");
  server.send(success ? 200 : 503, "text/plain; charset=utf-8", status);
}

void sendChunk(const String &text) {
  server.sendContent(text);
  delay(0);
}

void handleValuesApi() {
  size_t inverterValid = 0;
  size_t inverterTotal = 0;
  size_t batteryValid = 0;
  size_t batteryTotal = 0;
  for (size_t i = 0; i < REGISTER_COUNT; ++i) {
    const bool unsupported = registerIsUnsupported(registers[i]);
    if (registers[i].source == SourceGroup::BATTERY) {
      if (!unsupported) ++batteryTotal;
      if (registers[i].valid) ++batteryValid;
    } else {
      if (!unsupported) ++inverterTotal;
      if (registers[i].valid) ++inverterValid;
    }
  }
  size_t stageEnabled = 0;
  size_t stageOn = 0;
  for (size_t i = 0; i < SURPLUS_STAGE_COUNT; ++i) {
    if (surplusStages[i].enabled) ++stageEnabled;
    if (stageIsOn[i]) ++stageOn;
  }

  String prefix;
  prefix.reserve(1200);
  prefix = F("{\"status\":{\"wifiConnected\":");
  prefix += WiFi.status() == WL_CONNECTED ? F("true") : F("false");
  prefix += F(",\"wifiSsid\":\"");
  prefix += jsonEscape(WiFi.status() == WL_CONNECTED ? WiFi.SSID() : config.wifiSsid);
  prefix += F("\",\"stationIp\":\"");
  prefix += WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : String();
  prefix += F("\",\"apSsid\":\"");
  prefix += jsonEscape(accessPointSsid);
  prefix += F("\",\"apIp\":\"");
  prefix += WiFi.softAPIP().toString();
  prefix += F("\",\"modbusTransport\":\"");
  prefix += jsonEscape(lastModbusTransport);
  prefix += F("\",\"inverterOk\":");
  prefix += lastSuccessfulInverterCycle != 0 && inverterValid > 0 && lastInverterError == "OK" ? F("true") : F("false");
  prefix += F(",\"inverterError\":\"");
  prefix += jsonEscape(lastInverterError);
  prefix += F("\",\"inverterUnit\":");
  prefix += String(config.inverterUnit);
  prefix += F(",\"inverterValid\":");
  prefix += String(inverterValid);
  prefix += F(",\"inverterTotal\":");
  prefix += String(inverterTotal);
  prefix += F(",\"batteryOk\":");
  prefix += lastSuccessfulBatteryCycle != 0 && batteryValid > 0 && lastBatteryError == "OK" ? F("true") : F("false");
  prefix += F(",\"batteryError\":\"");
  prefix += jsonEscape(lastBatteryError);
  prefix += F("\",\"batteryTransport\":\"");
  prefix += jsonEscape(lastBatteryTransport);
  prefix += F("\",\"modbusMode\":");
  prefix += String(config.modbusMode);
  prefix += F(",\"batteryTcpUnit\":");
  prefix += String(config.batteryTcpUnit);
  prefix += F(",\"batteryRtuUnit\":");
  prefix += String(config.batteryRtuUnit);
  prefix += F(",\"batteryValid\":");
  prefix += String(batteryValid);
  prefix += F(",\"batteryTotal\":");
  prefix += String(batteryTotal);
  prefix += F(",\"exportPowerFresh\":");
  prefix += exportPowerIsFresh() ? F("true") : F("false");
  prefix += F(",\"exportPower\":");
  prefix += String(measuredExportPower());
  prefix += F(",\"availableSurplus\":");
  prefix += String(calculatedAvailableSurplus());
  prefix += F(",\"stageEnabled\":");
  prefix += String(stageEnabled);
  prefix += F(",\"stageOn\":");
  prefix += String(stageOn);
  prefix += F(",\"rippleEnabled\":");
  prefix += config.rippleEnabled ? F("true") : F("false");
  prefix += F(",\"rippleStatus\":\"");
  prefix += jsonEscape(rippleStatus);
  prefix += '"';
  prefix += F("},\"registers\":[");

  addNoCacheHeaders();
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "application/json; charset=utf-8", "");
  sendChunk(prefix);

  for (size_t i = 0; i < REGISTER_COUNT; ++i) {
    const RegisterDef &reg = registers[i];
    String item;
    item.reserve(260);
    if (i != 0) item += ',';
    item += F("{\"address\":");
    item += String(reg.address);
    item += F(",\"source\":\"");
    item += reg.source == SourceGroup::BATTERY ? F("battery") : F("inverter");
    item += F("\",\"view\":\"");
    item += reg.view == WebView::BATTERY ? F("battery") : F("inverter");
    item += F("\",\"name\":\"");
    item += jsonEscape(reg.name);
    item += F("\",\"description\":\"");
    item += jsonEscape(reg.description);
    item += F("\",\"unit\":\"");
    item += jsonEscape(reg.unit);
    item += F("\",\"valid\":");
    item += reg.valid ? F("true") : F("false");
    item += F(",\"unsupported\":");
    item += registerIsUnsupported(reg) ? F("true") : F("false");
    item += F(",\"display\":\"");
    item += formattedValue(reg);
    item += F("\"}");
    sendChunk(item);
  }
  const uint32_t installedWatts = configuredPvPeakWatts();
  String limitItem;
  limitItem.reserve(520);
  limitItem = F(",{\"address\":13073,\"source\":\"inverter\",\"view\":\"inverter\",\"name\":\"Export Power Limit\",\"description\":\"Maximale Einspeiseleistung (Holding, aktuell ");
  limitItem += holdingExportLimitValid ? String(holdingExportLimitWatts) : String("—");
  limitItem += F(" W von ");
  limitItem += installedWatts > 0 ? String(installedWatts) : String("—");
  limitItem += F(" W installierter PV-Leistung)\",\"unit\":\"%\",\"valid\":");
  limitItem += holdingExportLimitValid && installedWatts > 0 ? F("true") : F("false");
  limitItem += F(",\"unsupported\":false,\"display\":\"");
  if (holdingExportLimitValid && installedWatts > 0) {
    limitItem += String(static_cast<double>(holdingExportLimitWatts) * 100.0 / installedWatts, 1);
  }
  limitItem += F("\"},{\"address\":13086,\"source\":\"inverter\",\"view\":\"inverter\",\"name\":\"Export Power Limitation\",\"description\":\"Einspeisebegrenzung (Holding)\",\"unit\":\"\",\"valid\":");
  limitItem += holdingExportLimitValid ? F("true") : F("false");
  limitItem += F(",\"unsupported\":false,\"display\":\"");
  if (holdingExportLimitValid) {
    if (holdingExportLimitEnabledRaw == ConfigDefaults::EXPORT_LIMIT_ENABLED_RAW) {
      limitItem += F("EIN (0xAA)");
    } else if (holdingExportLimitEnabledRaw == ConfigDefaults::EXPORT_LIMIT_DISABLED_RAW) {
      limitItem += F("AUS (0x55)");
    } else {
      char rawText[12];
      snprintf(rawText, sizeof(rawText), "0x%04X", holdingExportLimitEnabledRaw);
      limitItem += rawText;
    }
  }
  limitItem += F("\"}");
  sendChunk(limitItem);
  sendChunk(F("]}"));
  sendChunk(String());
}

const char FORECAST_HTML[] PROGMEM = R"HTML(
<!doctype html><html lang="de"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Prognose · Solar Prognose Monitor</title>
<style>:root{--bg:#f3f5f7;--card:#fff;--text:#17212b;--muted:#64717d;--accent:#087f5b;--blue:#2673c9;--orange:#d97706;--line:#dfe4e8;--bad:#b42318}*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--text);font:15px system-ui,-apple-system,Segoe UI,sans-serif}header{background:var(--card);border-bottom:1px solid var(--line)}.bar{max-width:1120px;margin:auto;padding:14px 18px;display:flex;align-items:center;gap:18px}h1{font-size:20px;margin:0 auto 0 0}.nav{display:flex;gap:6px;overflow-x:auto}.nav a{white-space:nowrap;color:var(--text);text-decoration:none;padding:8px 10px;border-radius:8px}.nav a.active,.nav a:hover{background:#e6f4ef;color:#056044}main{max-width:1120px;margin:22px auto;padding:0 18px}.cards{display:grid;grid-template-columns:repeat(4,1fr);gap:12px;margin-bottom:16px}.card{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:16px;margin-bottom:16px}.metric b{display:block;font-size:24px;margin-top:5px}.muted{color:var(--muted)}.ok{color:var(--accent)}.bad{color:var(--bad)}.row{display:flex;gap:10px;flex-wrap:wrap;align-items:center}.button{border:0;border-radius:9px;background:var(--accent);color:#fff;padding:10px 14px;font:inherit;font-weight:650;cursor:pointer}.button.secondary{background:#66727d}canvas{display:block;width:100%;height:280px}table{width:100%;border-collapse:collapse}th,td{text-align:right;padding:8px;border-bottom:1px solid var(--line);white-space:nowrap}th:first-child,td:first-child{text-align:left}.scroll{overflow:auto}h2{font-size:17px;margin:0 0 14px}@media(max-width:800px){.cards{grid-template-columns:1fr 1fr}.bar{align-items:flex-start;flex-wrap:wrap}.nav{width:100%}}@media(max-width:480px){.cards{grid-template-columns:1fr}}</style></head><body>
<header><div class="bar"><h1>Solar Prognose Monitor</h1><nav class="nav"><a href="/inverter">Wechselrichter</a><a href="/battery">Batterie</a><a class="active" href="/forecast">Prognose</a><a href="/load-profile">Lastprofil</a><a href="/history">Verlauf</a><a href="/settings">Einstellungen</a><a href="/firmware">Firmware</a><a href="/about">About</a></nav></div></header>
<main><div class="cards" id="metrics"><section class="card metric"><span class="muted">Status</span><b>Wird geladen …</b></section></div><section class="card"><div class="row"><button id="refresh" class="button">Open-Meteo neu laden</button><span id="message" class="muted">Die Betriebsart wird ausschließlich unter Einstellungen geändert.</span></div></section><section class="card"><h2>PV-Prognose und gelerntes Lastprofil</h2><canvas id="energy"></canvas></section><section class="card"><h2>SOC-Fahrplan</h2><canvas id="soc"></canvas></section><section class="card"><h2>Open-Meteo-Werte je Dachfläche</h2><div class="scroll"><table><thead id="head"></thead><tbody id="rows"></tbody></table></div></section></main>
<script>
let state=null;const esc=s=>String(s).replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
const fmt=(n,d=1)=>Number.isFinite(Number(n))?Number(n).toFixed(d):'—';
const draw=(canvas,series,colors,labels,epochs,minValue=0,maxValue=null,markers=[])=>{const dpr=devicePixelRatio||1,w=canvas.clientWidth,h=canvas.clientHeight;canvas.width=w*dpr;canvas.height=h*dpr;const c=canvas.getContext('2d');c.scale(dpr,dpr);c.clearRect(0,0,w,h);const all=series.flatMap(x=>x.values).filter(Number.isFinite),max=maxValue??Math.max(minValue+1,...all),min=minValue,n=series[0]?.values.length||0,left=42,right=10,top=30,bottom=52,plotW=w-left-right,plotH=h-top-bottom;c.strokeStyle='#dfe4e8';c.fillStyle='#64717d';c.font='11px system-ui';c.textAlign='left';for(let i=0;i<=4;i++){const y=top+plotH*i/4;c.beginPath();c.moveTo(left,y);c.lineTo(w-right,y);c.stroke();c.fillText(fmt(max-(max-min)*i/4,0),2,y+4)}const maxTicks=w<520?4:7,step=Math.max(1,Math.ceil(Math.max(1,n-1)/(maxTicks-1))),ticks=[];for(let i=0;i<n;i+=step)ticks.push(i);if(n&&ticks[ticks.length-1]!==n-1)ticks.push(n-1);ticks.forEach(i=>{const x=left+plotW*(n<2?0:i/(n-1)),parts=String(labels[i]||'').split(' ');c.strokeStyle='#edf0f2';c.beginPath();c.moveTo(x,top);c.lineTo(x,top+plotH);c.stroke();c.fillStyle='#64717d';c.textAlign=i===0?'left':i===n-1?'right':'center';c.fillText(parts[0]||'',x,top+plotH+17);c.fillText(parts.slice(1).join(' '),x,top+plotH+31)});c.textAlign='left';series.forEach((s,j)=>{c.strokeStyle=colors[j];c.lineWidth=2;c.beginPath();let started=false;s.values.forEach((v,i)=>{if(!Number.isFinite(v))return;const x=left+plotW*(n<2?0:i/(n-1)),y=top+plotH-(v-min)/(max-min)*plotH;started?c.lineTo(x,y):c.moveTo(x,y);started=true});c.stroke()});if(n>1&&epochs.length===n){const first=Number(epochs[0]),last=Number(epochs[n-1]);markers.forEach((m,j)=>{const epoch=Number(m.epoch);if(!Number.isFinite(epoch)||epoch<first||epoch>last||last<=first)return;const x=left+plotW*(epoch-first)/(last-first);c.save();c.strokeStyle=m.color||'#b42318';c.fillStyle=m.color||'#b42318';c.setLineDash([5,4]);c.beginPath();c.moveTo(x,top);c.lineTo(x,top+plotH);c.stroke();c.setLineDash([]);c.fillText(m.label,x+4,top+12+j*13);c.restore()})}series.forEach((s,j)=>{c.fillStyle=colors[j];c.fillRect(left+j*150,8,12,3);c.fillText(s.name,left+18+j*150,13)})};
const render=d=>{state=d;const s=d.status,klass=s.fresh&&!s.bypass?'ok':(s.bypass?'muted':'bad'),labels=d.points.map(x=>x.time),epochs=d.points.map(x=>x.epoch),now={epoch:Math.floor(Date.now()/1000),label:'Jetzt',color:'#b42318'},goal={epoch:s.finishEpoch,label:'Ziel',color:'#6b4f9b'};document.querySelector('#metrics').innerHTML=`<section class="card metric"><span class="muted">Steuerung</span><b class="${klass}">${s.enabled?(s.bypass?'Bypass':'Prognose'):'Aus'}</b><small>${esc(s.text)}</small></section><section class="card metric"><span class="muted">Absoluter Batteriestand / freigegebener SOC</span><b>${fmt(s.currentSoc)} / ${fmt(s.holdingMaxSoc)} %</b><small>Quelle: 10743 · ${esc(s.socTransport)} · Fahrplan-Soll: ${fmt(s.requestedSoc)} %, Min: ${fmt(s.holdingMinSoc)} %</small></section><section class="card metric"><span class="muted">PV / gelernte Last</span><b>${fmt(s.totalPv)} / ${fmt(s.totalLoad)} kWh</b><small>Für Batterie: ${fmt(s.batteryEnergy)} kWh</small></section><section class="card metric"><span class="muted">Prognoseplan / min. Batteriestand zum Erreichen des Max-SOC</span><b>${fmt(s.plannedSoc)} / ${fmt(s.reachableSoc)} %</b><small>Fertig bis ${esc(s.finish)} · Regelwrites ${s.writes}/${s.maxWrites} · Sonderwrites ${s.priorityWrites}</small></section>`;draw(document.querySelector('#energy'),[{name:'PV kW',values:d.points.map(x=>x.pv)},{name:'Last kW',values:d.points.map(x=>x.load)},{name:'Akku kWh',values:d.points.map(x=>x.battery)}],['#087f5b','#d97706','#2673c9'],labels,epochs,0,null,[now]);draw(document.querySelector('#soc'),[{name:'Plan %',values:d.points.map(x=>x.plan)},{name:'Min. für Max-SOC %',values:d.points.map(x=>x.reach)},{name:'Freigabe %',values:d.points.map(x=>x.release)}],['#087f5b','#2673c9','#d97706'],labels,epochs,0,100,[now,goal]);document.querySelector('#head').innerHTML='<tr><th>Zeit</th><th>PV kW</th><th>Last kW</th><th>Akku kWh</th><th>Plan %</th><th>Min. %</th><th>Freigabe %</th>'+d.arrays.map(a=>`<th>${esc(a.name)} W/m²</th>`).join('')+'</tr>';document.querySelector('#rows').innerHTML=d.points.map(p=>`<tr><td>${esc(p.time)}</td><td>${fmt(p.pv,2)}</td><td>${fmt(p.load,2)}</td><td>${fmt(p.battery,2)}</td><td>${fmt(p.plan)}</td><td>${fmt(p.reach)}</td><td>${fmt(p.release)}</td>${p.gti.map(x=>`<td>${x}</td>`).join('')}</tr>`).join('')};
const load=async()=>{try{const r=await fetch('/api/forecast',{cache:'no-store'});if(!r.ok)throw Error('HTTP '+r.status);render(await r.json())}catch(e){document.querySelector('#message').textContent='Fehler: '+e.message}};document.querySelector('#refresh').addEventListener('click',async()=>{await fetch('/api/forecast/refresh',{method:'POST'});document.querySelector('#message').textContent='Abruf eingeplant …';setTimeout(load,1200)});load();setInterval(load,10000);addEventListener('resize',()=>state&&render(state));
</script></body></html>
)HTML";

const char LOAD_PROFILE_HTML[] PROGMEM = R"HTML(
<!doctype html><html lang="de"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Lastprofil · Solar Prognose Monitor</title><style>:root{--bg:#f3f5f7;--card:#fff;--text:#17212b;--muted:#64717d;--accent:#087f5b;--line:#dfe4e8;--bad:#b42318}*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--text);font:15px system-ui,-apple-system,Segoe UI,sans-serif}header{background:#fff;border-bottom:1px solid var(--line)}.bar{max-width:1120px;margin:auto;padding:14px 18px;display:flex;align-items:center;gap:18px}h1{font-size:20px;margin:0 auto 0 0}.nav{display:flex;gap:6px;overflow-x:auto}.nav a{white-space:nowrap;color:var(--text);text-decoration:none;padding:8px 10px;border-radius:8px}.nav a.active,.nav a:hover{background:#e6f4ef;color:#056044}main{max-width:1120px;margin:22px auto;padding:0 18px}.card{background:#fff;border:1px solid var(--line);border-radius:12px;padding:18px;margin-bottom:16px}.row{display:flex;gap:12px;align-items:center;flex-wrap:wrap}select,.button{padding:10px 13px;border:1px solid var(--line);border-radius:9px;background:#fff;font:inherit}.button{border:0;background:var(--accent);color:#fff;font-weight:650;cursor:pointer}.button.bad{background:var(--bad)}canvas{display:block;width:100%;height:330px}.muted{color:var(--muted)}table{width:100%;border-collapse:collapse}th,td{padding:8px;text-align:right;border-bottom:1px solid var(--line)}th:first-child,td:first-child{text-align:left}.scroll{overflow:auto}@media(max-width:800px){.bar{align-items:flex-start;flex-wrap:wrap}.nav{width:100%}}</style></head><body><header><div class="bar"><h1>Solar Prognose Monitor</h1><nav class="nav"><a href="/inverter">Wechselrichter</a><a href="/battery">Batterie</a><a href="/forecast">Prognose</a><a class="active" href="/load-profile">Lastprofil</a><a href="/history">Verlauf</a><a href="/settings">Einstellungen</a><a href="/firmware">Firmware</a><a href="/about">About</a></nav></div></header><main><section class="card"><div class="row"><label>Wochentag <select id="day"><option value="1">Montag</option><option value="2">Dienstag</option><option value="3">Mittwoch</option><option value="4">Donnerstag</option><option value="5">Freitag</option><option value="6">Samstag</option><option value="0">Sonntag</option></select></label><button id="reset" class="button bad">Lernprofil löschen</button><span id="status" class="muted"></span></div><p class="muted">Die Grundlast und wiederkehrende Großlasten werden getrennt gelernt. Bekannte eingeschaltete Überschuss-Stufen werden vor dem Lernen abgezogen.</p></section><section class="card"><canvas id="chart"></canvas></section><section class="card"><div class="scroll"><table><thead><tr><th>Zeit</th><th>Grundlast</th><th>Großlast-Anteil</th><th>Erwartet</th><th>Heute</th><th>Wahrscheinlichkeit</th></tr></thead><tbody id="rows"></tbody></table></div></section></main><script>
let data=null;const fmt=n=>Math.round(Number(n)||0);const draw=()=>{if(!data)return;const cv=document.querySelector('#chart'),dpr=devicePixelRatio||1,w=cv.clientWidth,h=cv.clientHeight;cv.width=w*dpr;cv.height=h*dpr;const c=cv.getContext('2d');c.scale(dpr,dpr);const series=[data.expected,data.today,data.base],colors=['#087f5b','#d97706','#2673c9'],names=['Erwartet','Heute','Grundlast'],max=Math.max(500,...series.flat()),left=44,right=10,top=28,bottom=45,plotW=w-left-right,plotH=h-top-bottom;c.clearRect(0,0,w,h);c.font='11px system-ui';c.textAlign='left';for(let j=0;j<=4;j++){const y=top+plotH*j/4;c.strokeStyle='#dfe4e8';c.beginPath();c.moveTo(left,y);c.lineTo(w-right,y);c.stroke();c.fillStyle='#64717d';c.fillText(Math.round(max*(1-j/4))+' W',2,y+4)}const ticks=w<520?[0,24,48,72,95]:[0,16,32,48,64,80,95];ticks.forEach(i=>{const x=left+plotW*i/95;c.strokeStyle='#edf0f2';c.beginPath();c.moveTo(x,top);c.lineTo(x,top+plotH);c.stroke();c.fillStyle='#64717d';c.textAlign=i===0?'left':i===95?'right':'center';c.fillText(data.labels[i],x,top+plotH+18)});c.textAlign='left';series.forEach((s,j)=>{c.strokeStyle=colors[j];c.lineWidth=2;c.beginPath();s.forEach((v,i)=>{const x=left+plotW*i/95,y=top+plotH-v/max*plotH;i?c.lineTo(x,y):c.moveTo(x,y)});c.stroke();c.fillStyle=colors[j];c.fillRect(left+j*120,8,12,3);c.fillText(names[j],left+18+j*120,13)})};const load=async()=>{const day=document.querySelector('#day').value,r=await fetch('/api/load-profile?day='+day,{cache:'no-store'});data=await r.json();document.querySelector('#status').textContent='Gelernte Tage: '+data.learnedDays+' · aktuelle 15-Minuten-Position: '+data.currentSlot;document.querySelector('#rows').innerHTML=data.labels.map((t,i)=>`<tr><td>${t}</td><td>${fmt(data.base[i])} W</td><td>${fmt(data.event[i])} W</td><td>${fmt(data.expected[i])} W</td><td>${fmt(data.today[i])} W</td><td>${data.probability[i]} %</td></tr>`).join('');draw()};document.querySelector('#day').addEventListener('change',load);document.querySelector('#reset').addEventListener('click',async()=>{if(!confirm('Das vollständig gelernte Lastprofil wirklich löschen?'))return;await fetch('/api/load-profile/reset',{method:'POST'});load()});load();setInterval(load,15000);addEventListener('resize',draw);
</script></body></html>
)HTML";

const char HISTORY_HTML[] PROGMEM = R"HTML(
<!doctype html><html lang="de"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Verlauf · Solar Prognose Monitor</title><style>
:root{--bg:#f3f5f7;--card:#fff;--text:#17212b;--muted:#64717d;--accent:#087f5b;--line:#dfe4e8;--bad:#b42318}*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--text);font:15px system-ui,-apple-system,Segoe UI,sans-serif}
header{background:#fff;border-bottom:1px solid var(--line)}.bar{max-width:1180px;margin:auto;padding:14px 18px;display:flex;align-items:center;gap:18px}h1{font-size:20px;margin:0 auto 0 0}.nav{display:flex;gap:6px;overflow-x:auto}.nav a{white-space:nowrap;color:var(--text);text-decoration:none;padding:8px 10px;border-radius:8px}.nav a.active,.nav a:hover{background:#e6f4ef;color:#056044}
main{max-width:1180px;margin:22px auto;padding:0 18px}.card{background:#fff;border:1px solid var(--line);border-radius:12px;padding:16px;margin-bottom:16px}.tools,.legend{display:flex;gap:12px;align-items:center;flex-wrap:wrap}.legend label{display:flex;gap:6px;align-items:center}.dot{width:11px;height:11px;border-radius:50%;display:inline-block}.muted{color:var(--muted)}select{padding:9px 11px;border:1px solid var(--line);border-radius:8px;background:#fff;font:inherit}.metrics{display:grid;grid-template-columns:repeat(6,1fr);gap:10px;margin:16px 0}.metric{padding:12px;border:1px solid var(--line);border-radius:10px}.metric span{display:block;color:var(--muted);font-size:12px}.metric b{display:block;font-size:19px;margin-top:4px}canvas{display:block;width:100%;height:410px}.empty{padding:60px 10px;text-align:center;color:var(--muted)}
@media(max-width:900px){.metrics{grid-template-columns:repeat(3,1fr)}.bar{align-items:flex-start;flex-wrap:wrap}.nav{width:100%}}@media(max-width:520px){.metrics{grid-template-columns:repeat(2,1fr)}canvas{height:350px}}
</style></head><body><header><div class="bar"><h1>Solar Prognose Monitor</h1><nav class="nav"><a href="/inverter">Wechselrichter</a><a href="/battery">Batterie</a><a href="/forecast">Prognose</a><a href="/load-profile">Lastprofil</a><a class="active" href="/history">Verlauf</a><a href="/settings">Einstellungen</a><a href="/firmware">Firmware</a><a href="/about">About</a></nav></div></header>
<main><section class="card"><div class="tools"><label>Ansicht <select id="range"><option value="24" selected>Tag · letzte 24 Stunden</option><option value="168">Woche · letzte 7 Tage</option><option value="744">Monat · letzte 31 Tage</option></select></label><span id="status" class="muted">Verlauf wird geladen …</span></div><div class="metrics" id="metrics"></div><div class="legend">
<label><input type="checkbox" data-key="pv" checked><i class="dot" style="background:#18a66a"></i>PV</label>
<label><input type="checkbox" data-key="load" checked><i class="dot" style="background:#d9a400"></i>Verbrauch</label>
<label><input type="checkbox" data-key="export" checked><i class="dot" style="background:#d63b8c"></i>Einspeisung</label>
<label><input type="checkbox" data-key="import" checked><i class="dot" style="background:#d04a3a"></i>Netzbezug</label>
<label><input type="checkbox" data-key="battery" checked><i class="dot" style="background:#2673c9"></i>Batterieleistung</label>
<label><input type="checkbox" data-key="soc" checked><i class="dot" style="background:#087f5b"></i>Batterie-SOC</label>
</div></section><section class="card"><canvas id="chart"></canvas><div id="empty" class="empty" hidden>Noch keine Messpunkte vorhanden. Der erste Punkt wird nach einer erfolgreichen Modbus-Abfrage angelegt.</div></section></main>
<script>
let data=null;const defs=[{key:'pv',name:'PV',color:'#18a66a',axis:'power'},{key:'load',name:'Verbrauch',color:'#d9a400',axis:'power'},{key:'export',name:'Einspeisung',color:'#d63b8c',axis:'power'},{key:'import',name:'Netzbezug',color:'#d04a3a',axis:'power'},{key:'battery',name:'Batterieleistung',color:'#2673c9',axis:'power'},{key:'soc',name:'SOC',color:'#087f5b',axis:'soc'}];
const enabled=k=>document.querySelector('[data-key="'+k+'"]').checked,fmtW=v=>Number.isFinite(v)?Math.round(v).toLocaleString('de-DE')+' W':'—',fmtSoc=v=>Number.isFinite(v)?v.toFixed(1)+' %':'—';
const renderMetrics=points=>{const p=points.length?points[points.length-1]:null,items=[['PV',p&&p.pv,'w'],['Verbrauch',p&&p.load,'w'],['Einspeisung',p&&p.export,'w'],['Netzbezug',p&&p.import,'w'],['Batterieleistung',p&&p.battery,'w'],['Batterie-SOC',p&&p.soc,'soc']];document.querySelector('#metrics').innerHTML=items.map(x=>'<div class="metric"><span>'+x[0]+'</span><b>'+(x[2]==='soc'?fmtSoc(x[1]):fmtW(x[1]))+'</b></div>').join('')};
const draw=()=>{const points=data?.points||[],cv=document.querySelector('#chart'),empty=document.querySelector('#empty');renderMetrics(points);if(points.length<2){cv.hidden=true;empty.hidden=false;return}cv.hidden=false;empty.hidden=true;const dpr=devicePixelRatio||1,w=cv.clientWidth,h=cv.clientHeight;cv.width=w*dpr;cv.height=h*dpr;const c=cv.getContext('2d');c.scale(dpr,dpr);c.clearRect(0,0,w,h);const active=defs.filter(x=>enabled(x.key)),powerValues=active.filter(x=>x.axis==='power').flatMap(x=>points.map(p=>p[x.key])).filter(Number.isFinite),powerMax=Math.max(1000,...powerValues),roundedMax=Math.ceil(powerMax/1000)*1000,left=58,right=48,top=30,bottom=55,plotW=w-left-right,plotH=h-top-bottom;c.font='11px system-ui';for(let i=0;i<=4;i++){const y=top+plotH*i/4;c.strokeStyle='#dfe4e8';c.beginPath();c.moveTo(left,y);c.lineTo(w-right,y);c.stroke();c.fillStyle='#64717d';c.textAlign='right';const watts=roundedMax*(1-i/4);c.fillText(watts>=1000?(watts/1000).toFixed(watts%1000?1:0)+' kW':Math.round(watts)+' W',left-6,y+4);c.textAlign='left';c.fillText(Math.round(100*(1-i/4))+' %',w-right+6,y+4)}const maxTicks=w<560?4:7,step=Math.max(1,Math.ceil((points.length-1)/(maxTicks-1))),ticks=[];for(let i=0;i<points.length;i+=step)ticks.push(i);if(ticks[ticks.length-1]!==points.length-1)ticks.push(points.length-1);ticks.forEach(i=>{const x=left+plotW*i/(points.length-1),parts=String(points[i].time).split(' ');c.strokeStyle='#edf0f2';c.beginPath();c.moveTo(x,top);c.lineTo(x,top+plotH);c.stroke();c.fillStyle='#64717d';c.textAlign=i===0?'left':i===points.length-1?'right':'center';c.fillText(parts[0]||'',x,top+plotH+18);c.fillText(parts.slice(1).join(' '),x,top+plotH+32)});active.forEach(s=>{c.strokeStyle=s.color;c.lineWidth=s.key==='soc'?2.5:2;c.beginPath();let started=false;points.forEach((p,i)=>{const v=p[s.key];if(!Number.isFinite(v)){started=false;return}const x=left+plotW*i/(points.length-1),ratio=s.axis==='soc'?v/100:v/roundedMax,y=top+plotH-Math.max(0,Math.min(1,ratio))*plotH;started?c.lineTo(x,y):c.moveTo(x,y);started=true});c.stroke()});c.textAlign='left';c.fillStyle='#64717d';c.fillText('Leistung',2,13);c.textAlign='right';c.fillText('SOC',w-2,13)};
const load=async()=>{try{const hours=document.querySelector('#range').value,r=await fetch('/api/history?hours='+hours,{cache:'no-store'});if(!r.ok)throw Error('HTTP '+r.status);data=await r.json();const last=data.points.length?' · zuletzt '+data.points[data.points.length-1].time:'',used=data.storageTotal?' · Speicher '+Math.round(data.storageUsed/1024)+'/'+Math.round(data.storageTotal/1024)+' KB':'';document.querySelector('#status').textContent=data.points.length+' Punkte · '+data.intervalMinutes+'-Minuten-Darstellung'+last+used+' · '+(data.persistent?'dauerhaft gespeichert':'nur im RAM');draw()}catch(e){document.querySelector('#status').textContent='Fehler: '+e.message}};document.querySelector('#range').addEventListener('change',load);document.querySelectorAll('[data-key]').forEach(x=>x.addEventListener('change',draw));load();setInterval(load,60000);addEventListener('resize',draw);
</script></body></html>
)HTML";

void handleForecastPage() {
  addNoCacheHeaders();
  server.send_P(200, "text/html; charset=utf-8", FORECAST_HTML);
}

void handleLoadProfilePage() {
  addNoCacheHeaders();
  server.send_P(200, "text/html; charset=utf-8", LOAD_PROFILE_HTML);
}

void handleHistoryPage() {
  addNoCacheHeaders();
  server.send_P(200, "text/html; charset=utf-8", HISTORY_HTML);
}

void handleForecastApi() {
  addNoCacheHeaders();
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "application/json; charset=utf-8", "");
  String out;
  out.reserve(1700);
  out = F("{\"status\":{\"enabled\":"); out += config.forecastEnabled ? F("true") : F("false");
  out += F(",\"bypass\":"); out += config.forecastBypass ? F("true") : F("false");
  out += F(",\"valid\":"); out += forecast.valid ? F("true") : F("false");
  out += F(",\"fresh\":"); out += forecastIsFresh() ? F("true") : F("false");
  out += F(",\"text\":\""); out += jsonEscape(forecast.status); out += F("\",\"timezone\":\""); out += jsonEscape(forecast.timezoneName);
  out += F("\",\"fetched\":\""); out += formatLocalTime(forecast.fetchedAt, true); out += F("\",\"sunrise\":\""); out += formatLocalTime(forecast.sunrise);
  out += F("\",\"sunset\":\""); out += formatLocalTime(forecast.sunset); out += F("\",\"finish\":\""); out += formatLocalTime(forecast.finishAt);
  out += F("\",\"finishEpoch\":"); out += String(static_cast<unsigned long>(forecast.finishAt));
  out += F(",\"currentSoc\":"); out += isfinite(currentBatterySoc()) ? String(currentBatterySoc(), 1) : F("null");
  out += F(",\"socTransport\":\""); out += jsonEscape(lastBatteryTransport); out += '"';
  out += F(",\"plannedSoc\":"); out += String(forecast.plannedSoc, 1); out += F(",\"reachableSoc\":"); out += String(forecast.reachableSoc, 1);
  out += F(",\"requestedSoc\":"); out += String(forecast.requestedSoc, 1); out += F(",\"holdingMaxSoc\":"); out += holdingSocValid ? String(holdingMaxSocRaw / 10.0f, 1) : F("null");
  out += F(",\"holdingMinSoc\":"); out += holdingSocValid ? String(holdingMinSocRaw / 10.0f, 1) : F("null");
  out += F(",\"writes\":"); out += String(maxSocWritesToday); out += F(",\"maxWrites\":"); out += String(effectiveMaxSocWritesPerDay());
  out += F(",\"priorityWrites\":"); out += String(priorityMaxSocWritesToday);
  out += F(",\"totalPv\":"); out += String(forecast.totalPvKwh, 2); out += F(",\"totalLoad\":"); out += String(forecast.totalLearnedLoadKwh, 2);
  out += F(",\"batteryEnergy\":"); out += String(forecast.totalBatteryEnergyKwh, 2); out += F("},\"arrays\":[");
  sendChunk(out);
  bool first = true;
  for (size_t i = 0; i < PV_ARRAY_COUNT; ++i) {
    if (!config.pvArrays[i].enabled) continue;
    String item = first ? "" : ","; first = false;
    item += F("{\"index\":"); item += String(i); item += F(",\"name\":\""); item += jsonEscape(config.pvArrays[i].name);
    item += F("\",\"kwp\":"); item += String(config.pvArrays[i].peakKwp, 2); item += F(",\"tilt\":"); item += String(config.pvArrays[i].tiltDegrees);
    item += F(",\"azimuth\":"); item += String(config.pvArrays[i].azimuthDegrees); item += '}'; sendChunk(item);
  }
  sendChunk(F("],\"points\":["));
  for (size_t i = 0; i < forecast.pointCount; ++i) {
    const ForecastPoint &point = forecast.points[i];
    String item = i == 0 ? "" : ",";
    item += F("{\"time\":\""); item += formatLocalTime(point.epoch, true); item += F("\",\"epoch\":"); item += String(static_cast<unsigned long>(point.epoch));
    item += F(",\"pv\":"); item += String(point.pvKw, 3); item += F(",\"load\":"); item += String(point.learnedLoadKw, 3);
    item += F(",\"battery\":"); item += String(point.batteryEnergyKwh, 3); item += F(",\"plan\":"); item += String(point.plannedSoc, 1);
    item += F(",\"reach\":"); item += String(point.reachableSoc, 1); item += F(",\"release\":");
    item += String(quantizedForecastSoc(max(point.plannedSoc, point.reachableSoc)), 1);
    item += F(",\"gti\":[");
    bool firstRoof = true;
    for (size_t roof = 0; roof < PV_ARRAY_COUNT; ++roof) {
      if (!config.pvArrays[roof].enabled) continue;
      if (!firstRoof) item += ','; firstRoof = false; item += String(point.gti[roof]);
    }
    item += F("]}"); sendChunk(item);
  }
  sendChunk(F("]}")); sendChunk(String());
}

void handleLoadProfileApi() {
  const uint8_t day = static_cast<uint8_t>(boundedNumberArgument("day", profileDayFor(time(nullptr)), 0, 6));
  addNoCacheHeaders(); server.setContentLength(CONTENT_LENGTH_UNKNOWN); server.send(200, "application/json; charset=utf-8", "");
  String out = F("{\"day\":"); out += String(day); out += F(",\"learnedDays\":"); out += String(loadProfile.learnedDays[day]);
  out += F(",\"currentSlot\":"); out += String(profileSlotFor(time(nullptr))); out += F(",\"labels\":[");
  for (size_t slot = 0; slot < LOAD_PROFILE_SLOTS; ++slot) { if (slot) out += ','; char label[8]; snprintf(label, sizeof(label), "\"%02u:%02u\"", static_cast<unsigned>(slot / 4), static_cast<unsigned>((slot % 4) * 15)); out += label; }
  out += F("],\"base\":["); sendChunk(out);
  for (size_t slot = 0; slot < LOAD_PROFILE_SLOTS; ++slot) { String value = slot ? "," : ""; value += String(loadProfile.baseWatts[day][slot]); sendChunk(value); }
  sendChunk(F("],\"event\":["));
  for (size_t slot = 0; slot < LOAD_PROFILE_SLOTS; ++slot) { String value = slot ? "," : ""; value += String(static_cast<uint32_t>(loadProfile.eventWatts[day][slot]) * loadProfile.eventProbability[day][slot] / 100U); sendChunk(value); }
  sendChunk(F("],\"probability\":["));
  for (size_t slot = 0; slot < LOAD_PROFILE_SLOTS; ++slot) { String value = slot ? "," : ""; value += String(loadProfile.eventProbability[day][slot]); sendChunk(value); }
  sendChunk(F("],\"expected\":["));
  for (size_t slot = 0; slot < LOAD_PROFILE_SLOTS; ++slot) { String value = slot ? "," : ""; value += String(loadProfile.baseWatts[day][slot] + static_cast<uint32_t>(loadProfile.eventWatts[day][slot]) * loadProfile.eventProbability[day][slot] / 100U); sendChunk(value); }
  sendChunk(F("],\"today\":["));
  const uint8_t today = profileDayFor(time(nullptr));
  for (size_t slot = 0; slot < LOAD_PROFILE_SLOTS; ++slot) { String value = slot ? "," : ""; value += String(day == today ? todayLoadWatts[slot] : 0); sendChunk(value); }
  sendChunk(F("]}")); sendChunk(String());
}

void appendHistoryPowerJson(String &json, const char *name, int16_t decawatts) {
  json += F(",\"");
  json += name;
  json += F("\":");
  json += decawatts == HISTORY_INVALID_POWER
      ? F("null")
      : String(static_cast<int32_t>(decawatts) * 10L);
}

void sendHistorySampleJson(const HistorySample &sample, bool &first) {
  String item;
  item.reserve(200);
  if (!first) item += ',';
  first = false;
  item += F("{\"epoch\":");
  item += String(sample.epoch);
  item += F(",\"time\":\"");
  item += formatLocalTime(sample.epoch, true);
  item += '"';
  appendHistoryPowerJson(item, "pv", sample.pvDecawatts);
  appendHistoryPowerJson(item, "load", sample.loadDecawatts);
  if (sample.gridDecawatts == HISTORY_INVALID_POWER) {
    item += F(",\"export\":null,\"import\":null");
  } else if (sample.gridDecawatts >= 0) {
    item += F(",\"export\":");
    item += String(static_cast<int32_t>(sample.gridDecawatts) * 10L);
    item += F(",\"import\":0");
  } else {
    item += F(",\"export\":0,\"import\":");
    item += String(-static_cast<int32_t>(sample.gridDecawatts) * 10L);
  }
  appendHistoryPowerJson(item, "battery", sample.batteryDecawatts);
  item += F(",\"soc\":");
  item += sample.socTenths == HISTORY_INVALID_SOC
      ? F("null")
      : String(sample.socTenths / 10.0f, 1);
  item += '}';
  sendChunk(item);
}

void handleHistoryApi() {
  const uint16_t hours = static_cast<uint16_t>(boundedNumberArgument("hours", 24, 1,
      static_cast<long>(ConfigDefaults::HISTORY_RETENTION_DAYS) * 24L));
  const uint32_t bucketSeconds = hours <= 48 ? 300UL : (hours <= 168 ? 900UL : 3600UL);
  saveStoredHistory();
  addNoCacheHeaders();
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "application/json; charset=utf-8", "");
  String header = F("{\"persistent\":");
  header += historyStorageReady && historyPersistenceOk ? F("true") : F("false");
  header += F(",\"intervalMinutes\":");
  header += String(bucketSeconds / 60UL);
  header += F(",\"retentionDays\":");
  header += String(ConfigDefaults::HISTORY_RETENTION_DAYS);
  header += F(",\"storageUsed\":");
  header += historyStorageReady ? String(static_cast<unsigned int>(LittleFS.usedBytes())) : F("0");
  header += F(",\"storageTotal\":");
  header += historyStorageReady ? String(static_cast<unsigned int>(LittleFS.totalBytes())) : F("0");
  header += F(",\"points\":[");
  sendChunk(header);

  const uint32_t now = clockIsValid() ? static_cast<uint32_t>(time(nullptr)) : 0;
  const uint32_t oldestAllowed = now > static_cast<uint32_t>(hours) * 3600UL
      ? now - static_cast<uint32_t>(hours) * 3600UL : 0;
  bool first = true;
  struct HistoryAccumulator {
    uint32_t bucket = 0;
    int64_t pv = 0, load = 0, grid = 0, battery = 0, soc = 0;
    uint16_t pvCount = 0, loadCount = 0, gridCount = 0, batteryCount = 0, socCount = 0;
    bool active = false;
  } aggregate;

  auto flushAggregate = [&]() {
    if (!aggregate.active) return;
    HistorySample averaged = {};
    averaged.epoch = aggregate.bucket + bucketSeconds / 2UL;
    averaged.pvDecawatts = aggregate.pvCount ? static_cast<int16_t>(aggregate.pv / aggregate.pvCount) : HISTORY_INVALID_POWER;
    averaged.loadDecawatts = aggregate.loadCount ? static_cast<int16_t>(aggregate.load / aggregate.loadCount) : HISTORY_INVALID_POWER;
    averaged.gridDecawatts = aggregate.gridCount ? static_cast<int16_t>(aggregate.grid / aggregate.gridCount) : HISTORY_INVALID_POWER;
    averaged.batteryDecawatts = aggregate.batteryCount ? static_cast<int16_t>(aggregate.battery / aggregate.batteryCount) : HISTORY_INVALID_POWER;
    averaged.socTenths = aggregate.socCount ? static_cast<uint16_t>(aggregate.soc / aggregate.socCount) : HISTORY_INVALID_SOC;
    sendHistorySampleJson(averaged, first);
    aggregate = HistoryAccumulator();
  };

  auto consumeSample = [&](const HistorySample &sample) {
    if (sample.epoch == 0 || sample.epoch < oldestAllowed || (now != 0 && sample.epoch > now + 300UL)) return;
    const uint32_t bucket = sample.epoch - sample.epoch % bucketSeconds;
    if (aggregate.active && aggregate.bucket != bucket) flushAggregate();
    if (!aggregate.active) {
      aggregate.active = true;
      aggregate.bucket = bucket;
    }
    if (sample.pvDecawatts != HISTORY_INVALID_POWER) { aggregate.pv += sample.pvDecawatts; ++aggregate.pvCount; }
    if (sample.loadDecawatts != HISTORY_INVALID_POWER) { aggregate.load += sample.loadDecawatts; ++aggregate.loadCount; }
    if (sample.gridDecawatts != HISTORY_INVALID_POWER) { aggregate.grid += sample.gridDecawatts; ++aggregate.gridCount; }
    if (sample.batteryDecawatts != HISTORY_INVALID_POWER) { aggregate.battery += sample.batteryDecawatts; ++aggregate.batteryCount; }
    if (sample.socTenths != HISTORY_INVALID_SOC) { aggregate.soc += sample.socTenths; ++aggregate.socCount; }
  };

  if (historyStorageReady) {
    String names[40];
    const size_t fileCount = collectHistoryFiles(names, 40);
    for (size_t i = 0; i < fileCount; ++i) {
      File file = LittleFS.open(names[i], FILE_READ);
      HistoryFileHeader fileHeader = {};
      if (!readHistoryHeader(file, fileHeader)) {
        file.close();
        continue;
      }
      HistorySample sample = {};
      while (file.read(reinterpret_cast<uint8_t *>(&sample), sizeof(sample)) == sizeof(sample)) {
        consumeSample(sample);
      }
      file.close();
      delay(0);
    }
  } else {
    const size_t oldestIndex = (historyData.writeIndex + HISTORY_POINT_COUNT - historyData.count)
                             % HISTORY_POINT_COUNT;
    for (size_t offset = 0; offset < historyData.count; ++offset) {
      consumeSample(historyData.samples[(oldestIndex + offset) % HISTORY_POINT_COUNT]);
    }
  }
  flushAggregate();
  sendChunk(F("]}"));
  sendChunk(String());
}

void handleForecastRefresh() {
  nextForecastFetchAt = millis(); forecast.status = "Manueller Open-Meteo-Abruf eingeplant";
  addNoCacheHeaders(); server.send(202, "application/json", "{\"queued\":true}");
}

void handleLoadProfileReset() {
  resetLoadProfile(); saveStoredProfile();
  addNoCacheHeaders(); server.send(200, "application/json", "{\"reset\":true}");
}

const char FIRMWARE_HTML[] PROGMEM = R"HTML(
<!doctype html><html lang="de"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Firmware · Solar Prognose Monitor</title><style>:root{--bg:#f3f5f7;--card:#fff;--text:#17212b;--muted:#64717d;--accent:#087f5b;--line:#dfe4e8;--bad:#b42318;--warn-bg:#fff7e0;--warn-line:#e9b949}*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--text);font:15px system-ui,-apple-system,Segoe UI,sans-serif}header{background:#fff;border-bottom:1px solid var(--line)}.bar{max-width:1120px;margin:auto;padding:14px 18px;display:flex;align-items:center;gap:18px}h1{font-size:20px;margin:0 auto 0 0}.nav{display:flex;gap:6px;overflow-x:auto}.nav a{white-space:nowrap;color:var(--text);text-decoration:none;padding:8px 10px;border-radius:8px}.nav a.active,.nav a:hover{background:#e6f4ef;color:#056044}main{max-width:820px;margin:22px auto;padding:0 18px}.card{background:#fff;border:1px solid var(--line);border-radius:12px;padding:18px;margin-bottom:16px}.hint{color:var(--muted);line-height:1.55}.button{border:0;border-radius:9px;background:var(--accent);color:#fff;padding:11px 16px;font:inherit;font-weight:650;cursor:pointer}input[type=file]{display:block;width:100%;padding:10px;border:1px solid var(--line);border-radius:8px;margin:12px 0}.override{display:flex;gap:10px;align-items:flex-start;padding:12px;margin:12px 0;border:1px solid var(--warn-line);border-radius:9px;background:var(--warn-bg);line-height:1.45}.override input{margin-top:3px}.override small{display:block;color:#765b00;margin-top:3px}progress{width:100%;height:18px;margin:12px 0}.bad{color:var(--bad)}@media(max-width:800px){.bar{align-items:flex-start;flex-wrap:wrap}.nav{width:100%}}</style></head><body><header><div class="bar"><h1>Solar Prognose Monitor</h1><nav class="nav"><a href="/inverter">Wechselrichter</a><a href="/battery">Batterie</a><a href="/forecast">Prognose</a><a href="/load-profile">Lastprofil</a><a href="/history">Verlauf</a><a href="/settings">Einstellungen</a><a class="active" href="/firmware">Firmware</a><a href="/about">About</a></nav></div></header><main><section class="card"><h2>Firmware über Browser installieren</h2><p class="hint">Wähle die kompilierte <b>.bin</b>-Datei für das verwendete ESP32-Modell, dessen Flashgröße und das bereits installierte Partitionsschema. Vor dem Update werden Lernprofil und LittleFS-Verlauf gespeichert und zurückgelesen. Schlägt diese Prüfung fehl, wird das Update standardmäßig abgebrochen. Vor dem Schreiben werden alle Ausgänge ausgeschaltet. Nach erfolgreicher Prüfung startet der ESP32 neu.</p><form id="form"><input id="file" type="file" accept=".bin,application/octet-stream" required><label class="override"><input id="forceUpdate" type="checkbox"><span><b>Notfallfreigabe: Update trotz fehlgeschlagener Sicherung zulassen</b><small>Nur verwenden, wenn ein Update sonst nicht mehr möglich ist. Lernprofil oder Verlauf können dabei verloren gehen.</small></span></label><button class="button">Firmware hochladen</button><progress id="progress" value="0" max="100"></progress><div id="result"></div></form></section><section class="card"><h2>Hinweis zur Partitionierung</h2><p class="hint"><b>Die erstmalige Installation der mitgelieferten Partitionstabelle muss einmalig per USB erfolgen.</b> Stelle „Erase All Flash Before Sketch Upload“ auf <b>Disabled</b>, damit Einstellungen, Lernprofil und der bisherige 24-Stunden-Verlauf automatisch migriert werden können. Eine USB-Komplett-/Factory-Datei löscht diese Daten und ist nur für Neuinstallationen gedacht. Danach funktionieren Browserupdates wieder normal. Die 4-MB- und 8-MB-Dateien dürfen nicht vertauscht werden. ArduinoOTA ist nicht enthalten.</p></section></main><script>const form=document.querySelector('#form'),result=document.querySelector('#result'),progress=document.querySelector('#progress'),force=document.querySelector('#forceUpdate');form.addEventListener('submit',e=>{e.preventDefault();const warning=force.checked?'ACHTUNG: Das Update darf auch fortfahren, wenn Lernprofil oder Verlauf nicht sicher gespeichert werden konnten. Datenverlust ist möglich. Trotzdem installieren?':'Firmware jetzt installieren? Lernprofil und Verlauf werden vorher gespeichert und geprüft. Alle Ausgänge werden ausgeschaltet.';if(!confirm(warning))return;const xhr=new XMLHttpRequest();xhr.open('POST','/update?force='+(force.checked?'1':'0'));xhr.upload.onprogress=x=>{if(x.lengthComputable)progress.value=Math.round(x.loaded/x.total*100)};xhr.onload=()=>{result.textContent=xhr.responseText;result.className=xhr.status===200?'':'bad'};xhr.onerror=()=>{result.textContent='Netzwerkfehler beim Upload.';result.className='bad'};const data=new FormData();data.append('firmware',document.querySelector('#file').files[0]);xhr.send(data)})</script></body></html>
)HTML";

const char ABOUT_HTML[] PROGMEM = R"HTML(
<!doctype html><html lang="de"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>About · Solar Prognose Monitor</title><style>:root{--bg:#f3f5f7;--text:#17212b;--muted:#64717d;--accent:#087f5b;--line:#dfe4e8}*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--text);font:15px system-ui,-apple-system,Segoe UI,sans-serif}header{background:#fff;border-bottom:1px solid var(--line)}.bar{max-width:1120px;margin:auto;padding:14px 18px;display:flex;align-items:center;gap:18px}h1{font-size:20px;margin:0 auto 0 0}.nav{display:flex;gap:6px;overflow-x:auto}.nav a{white-space:nowrap;color:var(--text);text-decoration:none;padding:8px 10px;border-radius:8px}.nav a.active,.nav a:hover{background:#e6f4ef;color:#056044}main{max-width:1120px;margin:22px auto;padding:0 18px}.card{background:#fff;border:1px solid var(--line);border-radius:12px;padding:18px;margin-bottom:16px}table{width:100%;border-collapse:collapse}th,td{text-align:left;padding:9px;border-bottom:1px solid var(--line)}th{width:180px;color:var(--muted)}.row{display:flex;gap:12px;align-items:center;flex-wrap:wrap}.check{display:flex;gap:8px;align-items:center}.button{border:0;border-radius:9px;background:#66727d;color:#fff;padding:10px 14px;font:inherit;font-weight:650;cursor:pointer}.muted{color:var(--muted)}.legal{line-height:1.6}.legal a{color:var(--accent)}pre{height:50vh;overflow:auto;background:#111820;color:#d8f5e8;border-radius:9px;padding:13px;white-space:pre-wrap;word-break:break-word;font:12px ui-monospace,SFMono-Regular,Consolas,monospace}@media(max-width:800px){.bar{align-items:flex-start;flex-wrap:wrap}.nav{width:100%}}</style></head><body><header><div class="bar"><h1>Solar Prognose Monitor</h1><nav class="nav"><a href="/inverter">Wechselrichter</a><a href="/battery">Batterie</a><a href="/forecast">Prognose</a><a href="/load-profile">Lastprofil</a><a href="/history">Verlauf</a><a href="/settings">Einstellungen</a><a href="/firmware">Firmware</a><a class="active" href="/about">About</a></nav></div></header><main><section class="card"><h2>About</h2><table><tr><th>Produkt</th><td>Solar Prognose Monitor</td></tr><tr><th>Firmware-Version</th><td><b>{{VERSION}}</b></td></tr><tr><th>Hersteller</th><td>{{MANUFACTURER}}</td></tr><tr><th>Lizenz</th><td>{{LICENSE}}</td></tr><tr><th>Chip</th><td>{{CHIP}}</td></tr><tr><th>Flash-Speicher</th><td>{{FLASH}}</td></tr><tr><th>Aktiver OTA-Slot</th><td>{{APP}}</td></tr><tr><th>System-NVS</th><td>{{NVS}}</td></tr><tr><th>Profil-NVS</th><td>{{PROFILE_NVS}}</td></tr><tr><th>Verlaufsspeicher</th><td>{{HISTORY}}</td></tr><tr><th>Quellcode / Projekt</th><td><a href="{{PROJECT_URL}}">GitHub-Repository</a></td></tr></table></section><section class="card legal"><h2>Lizenz und Haftung</h2><p>Diese Software ist für erlaubte nichtkommerzielle Zwecke unter der <b>PolyForm Noncommercial License 1.0.0</b> (<code>PolyForm-Noncommercial-1.0.0</code>) verfügbar. Kommerzielle oder gewerbliche Nutzung benötigt eine separate schriftliche Genehmigung oder Lizenzvereinbarung. <a href="{{PROJECT_URL}}/blob/main/LICENSE">Lizenztext</a> · <a href="{{PROJECT_URL}}/blob/main/COMMERCIAL-LICENSE.md">kommerzielle Lizenzierung</a></p><p>Die Software wird ohne Gewährleistung bereitgestellt. Installation, Konfiguration und Nutzung erfolgen auf eigene Gefahr. Arbeiten an Netzspannung und leistungsführenden Anlagenteilen dürfen nur durch entsprechend qualifizierte Fachkräfte erfolgen.</p><p>Dies ist ein unabhängiges, source-available Projekt und steht in keiner Verbindung zu Sungrow Power Supply Co., Ltd. Es wird von Sungrow weder unterstützt noch gesponsert oder empfohlen. Produktnamen, Logos und Marken gehören ihren jeweiligen Rechteinhabern.</p></section><section class="card"><h2>Web-Debug</h2><div class="row"><label class="check"><input id="toggle" type="checkbox"> Programmmeldungen im RAM puffern</label><button id="clear" class="button">Puffer leeren</button><span class="muted">Maximal 12.000 Zeichen. Frühe Boot-ROM-Ausgaben können nicht erfasst werden.</span></div><pre id="log">Wird geladen …</pre></section></main><script>const toggle=document.querySelector('#toggle'),log=document.querySelector('#log');const load=async()=>{const r=await fetch('/api/debug',{cache:'no-store'}),t=await r.text();log.textContent=t;log.scrollTop=log.scrollHeight};toggle.addEventListener('change',async()=>{await fetch('/api/debug/toggle',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'enabled='+(toggle.checked?'1':'0')});load()});document.querySelector('#clear').addEventListener('click',async()=>{await fetch('/api/debug/clear',{method:'POST'});load()});fetch('/api/debug/status').then(r=>r.json()).then(x=>{toggle.checked=x.enabled;load()});setInterval(load,2000)</script></body></html>
)HTML";

void handleFirmwarePage() {
  String page = FPSTR(FIRMWARE_HTML);
  page.replace(
      F("Stelle „Erase All Flash Before Sketch Upload“ auf <b>Disabled</b>, damit Einstellungen, Lernprofil und der bisherige 24-Stunden-Verlauf automatisch migriert werden können. Eine USB-Komplett-/Factory-Datei löscht diese Daten und ist nur für Neuinstallationen gedacht."),
      F("Für eine Neuinstallation auf einem frischen ESP32 ist Version 1.0.1 oder neuer erforderlich; Version 1.0.0 konnte dort wegen eines falschen App-Offsets nicht starten. Eine USB-Komplett-/Factory-Datei ist nur für Neuinstallationen gedacht und löscht vorhandene Daten. Bereits laufende Geräte erhalten die passende normale Update-Datei über diese Seite."));
  addNoCacheHeaders();
  server.send(200, "text/html; charset=utf-8", page);
}

void handleAboutPage() {
  String page = FPSTR(ABOUT_HTML);
  const esp_partition_t *running = esp_ota_get_running_partition();
  const esp_partition_t *nvs = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS, "nvs");
  const esp_partition_t *profileNvs = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS,
                                                               ConfigDefaults::PROFILE_PARTITION_LABEL);
  const esp_partition_t *history = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY,
                                                            ConfigDefaults::HISTORY_PARTITION_LABEL);
  page.replace("{{VERSION}}", ConfigDefaults::FIRMWARE_VERSION);
  page.replace("{{MANUFACTURER}}", ConfigDefaults::MANUFACTURER);
  page.replace("{{LICENSE}}", ConfigDefaults::LICENSE_TEXT);
  page.replace("{{PROJECT_URL}}", ConfigDefaults::PROJECT_URL);
  page.replace("{{CHIP}}", String(ESP.getChipModel()) + " · " + String(ESP.getChipCores()) + " Kerne");
  page.replace("{{FLASH}}", String(ESP.getFlashChipSize() / (1024UL * 1024UL)) + " MB");
  page.replace("{{APP}}", running ? String(running->label) + " · " + String(running->size / 1024UL) + " KB" : String("nicht erkannt"));
  page.replace("{{NVS}}", nvs ? String(nvs->size / 1024UL) + " KB" : String("nicht erkannt"));
  page.replace("{{PROFILE_NVS}}", profileNvs ? String(profileNvs->size / 1024UL) + " KB" : String("nicht erkannt"));
  page.replace("{{HISTORY}}", history ? String(history->size / 1024UL) + " KB · "
      + String(historyStorageReady ? LittleFS.usedBytes() / 1024UL : 0) + " KB belegt" : String("nicht vorhanden"));
  addNoCacheHeaders();
  server.send(200, "text/html; charset=utf-8", page);
}

void handleLegacyDebugPage() {
  server.sendHeader("Location", "/about", true);
  server.send(302, "text/plain", "");
}

#if 0  // Alte kombinierte Firmware-/ArduinoOTA-Seite
void handleFirmwarePageLegacy() {
  String page;
  page.reserve(9000);
  page = F("<!doctype html><html lang='de'><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'><title>Firmware · Solar Prognose Monitor</title><style>:root{--bg:#f3f5f7;--card:#fff;--text:#17212b;--muted:#64717d;--accent:#087f5b;--line:#dfe4e8;--bad:#b42318}*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--text);font:15px system-ui,-apple-system,Segoe UI,sans-serif}header{background:var(--card);border-bottom:1px solid var(--line)}.bar{max-width:820px;margin:auto;padding:14px 18px;display:flex;align-items:center;gap:18px}h1{font-size:20px;margin:0 auto 0 0}.nav{display:flex;gap:8px}.nav a{color:var(--text);text-decoration:none;padding:8px 11px;border-radius:8px}.nav a.active,.nav a:hover{background:#e6f4ef;color:#056044}main{max-width:820px;margin:22px auto;padding:0 18px}.card{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:18px;margin-bottom:16px}h2{font-size:17px;margin:0 0 14px}.hint{color:var(--muted);font-size:13px;line-height:1.5}.check{display:flex;gap:9px;align-items:center}.button{border:0;border-radius:9px;background:var(--accent);color:#fff;padding:10px 15px;font:inherit;font-weight:650;cursor:pointer}.button.secondary{background:#66727d}.row{display:flex;gap:12px;align-items:center;flex-wrap:wrap}input[type=file]{display:block;width:100%;padding:10px;border:1px solid var(--line);border-radius:8px;margin:12px 0}progress{width:100%;height:18px;margin:12px 0}pre{height:340px;overflow:auto;background:#111820;color:#d8f5e8;border-radius:9px;padding:13px;white-space:pre-wrap;word-break:break-word;font:12px ui-monospace,SFMono-Regular,Consolas,monospace}.result{min-height:24px;margin-top:10px}.bad{color:var(--bad)}@media(max-width:650px){.bar{flex-wrap:wrap}.nav{width:100%;overflow-x:auto}}</style></head><body><header><div class='bar'><h1>Solar Prognose Monitor</h1><nav class='nav'><a href='/inverter'>Wechselrichter</a><a href='/battery'>Batterie</a><a href='/settings'>Einstellungen</a><a class='active' href='/firmware'>Firmware</a></nav></div></header><main><section class='card'><h2>Firmware über Browser installieren</h2><p class='hint'>Wähle eine für dieses ESP32-Modell und dessen Partitionstabelle kompilierte <b>.bin</b>-Datei. Vor dem Schreiben werden alle Überschuss-Stufen ausgeschaltet. Nach erfolgreichem Upload startet der ESP32 automatisch neu.</p><form id='uploadForm'><input id='firmwareFile' name='firmware' type='file' accept='.bin,application/octet-stream' required><button class='button' type='submit'>Firmware hochladen</button><progress id='progress' value='0' max='100'></progress><div id='uploadResult' class='result'></div></form></section><section class='card'><h2>ArduinoOTA</h2><p>Netzwerkname: <b>");
  page += ConfigDefaults::MDNS_NAME;
  page += F("</b> · OTA-Passwort: <b>");
  page += config.otaPassword.isEmpty() ? F("nicht gesetzt") : F("gesetzt");
  page += F("</b></p><p class='hint'>Der Port erscheint bei unterstützten Arduino-IDE-Installationen als Netzwerk-Port. Das Passwort wird unter Einstellungen verwaltet. ArduinoOTA dient nur zum Hochladen; für Laufzeitmeldungen nutze Web-Debug.</p></section><section class='card'><h2>Web-Debug</h2><div class='row'><label class='check'><input id='debugToggle' type='checkbox'");
  if (webDebugEnabled) page += F(" checked");
  page += F("> Serial-Ausgaben im Browser puffern</label><button id='clearDebug' class='button secondary' type='button'>Puffer leeren</button></div><p class='hint'>Es werden bis zu 12.000 Zeichen der Programmmeldungen im RAM gehalten. Sehr frühe Boot- und Absturzmeldungen vor dem Start des Webservers können nicht erfasst werden.</p><pre id='debugLog'>Wird geladen …</pre></section><script>const toggle=document.querySelector('#debugToggle'),log=document.querySelector('#debugLog'),result=document.querySelector('#uploadResult'),progress=document.querySelector('#progress');async function setDebug(){const body='enabled='+(toggle.checked?'1':'0');const r=await fetch('/api/debug/toggle',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body});if(!r.ok)throw Error('HTTP '+r.status);await loadDebug()}async function loadDebug(){if(!toggle.checked){log.textContent='Web-Debug ist deaktiviert.';return}try{const r=await fetch('/api/debug',{cache:'no-store'});log.textContent=await r.text();log.scrollTop=log.scrollHeight}catch(e){log.textContent='Debugfehler: '+e.message}}toggle.addEventListener('change',()=>setDebug().catch(e=>alert(e.message)));document.querySelector('#clearDebug').addEventListener('click',async()=>{await fetch('/api/debug/clear',{method:'POST'});loadDebug()});document.querySelector('#uploadForm').addEventListener('submit',e=>{e.preventDefault();if(!confirm('Firmware jetzt installieren? Alle Ausgänge werden ausgeschaltet.'))return;const xhr=new XMLHttpRequest();xhr.open('POST','/update');xhr.upload.onprogress=e=>{if(e.lengthComputable)progress.value=Math.round(e.loaded/e.total*100)};xhr.onload=()=>{result.textContent=xhr.responseText;result.className='result '+(xhr.status===200?'':'bad')};xhr.onerror=()=>{result.textContent='Netzwerkfehler beim Upload.';result.className='result bad'};const data=new FormData();data.append('firmware',document.querySelector('#firmwareFile').files[0]);xhr.send(data)});loadDebug();setInterval(loadDebug,2000)</script></main></body></html>");

  addNoCacheHeaders();
  server.send(200, "text/html; charset=utf-8", page);
}

#endif

void handleDebugToggle() {
  webDebugEnabled = server.arg("enabled") == "1";
  if (!webDebugEnabled) webDebugBuffer = "";
  preferences.begin("sungrow", false);
  preferences.putBool("webdebug", webDebugEnabled);
  preferences.end();
  if (webDebugEnabled) debugPrintln("Web-Debug aktiviert.");
  addNoCacheHeaders();
  server.send(200, "application/json", webDebugEnabled ? "{\"enabled\":true}" : "{\"enabled\":false}");
}

void handleDebugLog() {
  addNoCacheHeaders();
  server.send(200, "text/plain; charset=utf-8", webDebugEnabled ? webDebugBuffer : String("Web-Debug ist deaktiviert."));
}

void handleDebugClear() {
  webDebugBuffer = "";
  if (webDebugEnabled) debugPrintln("Web-Debugpuffer geleert.");
  addNoCacheHeaders();
  server.send(200, "text/plain; charset=utf-8", "OK");
}

void handleDebugStatus() {
  addNoCacheHeaders();
  server.send(200, "application/json", webDebugEnabled ? "{\"enabled\":true}" : "{\"enabled\":false}");
}

void handleFirmwareUpload() {
  HTTPUpload &upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    firmwareUpdateInProgress = true;
    browserUpdateSuccess = false;
    browserUpdateRejected = false;
    browserUpdateForced = server.arg("force") == "1";
    browserUpdatePersistenceWarning = "";
    browserUpdateMessage = "Upload wird vorbereitet";
    // Auch den noch nicht abgeschlossenen Viertelstundenblock übernehmen und
    // das vollständige Lernprofil sichern, bevor die App-Partition verändert
    // wird. Ein normales Browser-Update lässt die NVS-Partition unangetastet.
    commitLearningBlock();
    learningLoadSum = 0;
    learningLoadSamples = 0;
    activeLearningDay = 255;
    activeLearningSlot = 255;
    const bool profileSaved = saveStoredProfile();
    const bool historySaved = saveStoredHistory(true);
    if (!profileSaved || !historySaved) {
      if (!profileSaved && !historySaved) browserUpdatePersistenceWarning = "Lernprofil und Verlauf";
      else if (!profileSaved) browserUpdatePersistenceWarning = "Lernprofil";
      else browserUpdatePersistenceWarning = "Verlauf";

      if (!browserUpdateForced) {
        browserUpdateRejected = true;
        firmwareUpdateInProgress = false;
        browserUpdateMessage = "Update abgebrochen: " + browserUpdatePersistenceWarning
            + " konnten nicht nachweislich gespeichert werden. Prüfe den Speicher oder aktiviere bewusst die Notfallfreigabe.";
        debugPrintln(browserUpdateMessage);
        return;
      }
      debugPrintln("Notfallfreigabe aktiv: Update wird trotz nicht verifizierter Sicherung von "
                   + browserUpdatePersistenceWarning + " fortgesetzt.");
    }
    forceAllStagesOff(true);
    modbusClient.stop();
    pollingCycleActive = false;
    debugPrintf("Browser-Update gestartet: %s (%u Byte).\n", upload.filename.c_str(), static_cast<unsigned int>(upload.totalSize));
    if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
      browserUpdateMessage = String("Update konnte nicht gestartet werden: ") + Update.errorString();
      firmwareUpdateInProgress = false;
      debugPrintln(browserUpdateMessage);
    }
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (browserUpdateRejected) return;
    if (!Update.hasError() && Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
      browserUpdateMessage = String("Schreibfehler: ") + Update.errorString();
      debugPrintln(browserUpdateMessage);
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (browserUpdateRejected) return;
    if (!Update.hasError() && Update.end(true)) {
      browserUpdateSuccess = true;
      browserUpdateMessage = browserUpdatePersistenceWarning.isEmpty()
          ? "Firmware erfolgreich installiert. Lernprofil und Verlauf wurden vorher verifiziert. Der ESP32 startet neu."
          : "Firmware erfolgreich installiert (Notfallfreigabe: " + browserUpdatePersistenceWarning
              + " konnten vorher nicht verifiziert werden). Der ESP32 startet neu.";
      debugPrintf("Browser-Update abgeschlossen: %u Byte.\n", static_cast<unsigned int>(upload.totalSize));
    } else {
      browserUpdateMessage = String("Firmware-Update fehlgeschlagen: ") + Update.errorString();
      firmwareUpdateInProgress = false;
      debugPrintln(browserUpdateMessage);
    }
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    if (!browserUpdateRejected) Update.abort();
    browserUpdateMessage = "Firmware-Upload wurde abgebrochen.";
    firmwareUpdateInProgress = false;
    debugPrintln(browserUpdateMessage);
  }
}

void handleFirmwareUploadFinished() {
  addNoCacheHeaders();
  if (browserUpdateSuccess) {
    server.send(200, "text/plain; charset=utf-8", browserUpdateMessage);
    restartAt = millis() + 1500;
  } else {
    if (browserUpdateMessage.isEmpty()) browserUpdateMessage = "Es wurde keine gültige Firmware empfangen.";
    server.send(500, "text/plain; charset=utf-8", browserUpdateMessage);
    firmwareUpdateInProgress = false;
  }
}

#if 0  // ArduinoOTA wurde zugunsten des Browser-Updates entfernt.
void startArduinoOta() {
  ArduinoOTA.setHostname(ConfigDefaults::MDNS_NAME);
  if (!config.otaPassword.isEmpty()) ArduinoOTA.setPassword(config.otaPassword.c_str());
  ArduinoOTA.onStart([]() {
    firmwareUpdateInProgress = true;
    otaLastProgressPercent = 255;
    forceAllStagesOff(true);
    modbusClient.stop();
    pollingCycleActive = false;
    debugPrintln("ArduinoOTA-Update gestartet; alle Stufen sind AUS.");
  });
  ArduinoOTA.onEnd([]() {
    debugPrintln("ArduinoOTA-Update abgeschlossen; Neustart folgt.");
  });
  ArduinoOTA.onProgress([](unsigned int progressValue, unsigned int total) {
    if (total == 0) return;
    const uint8_t percent = static_cast<uint8_t>((static_cast<uint64_t>(progressValue) * 100ULL) / total);
    if (percent / 10 != otaLastProgressPercent / 10) {
      otaLastProgressPercent = percent;
      debugPrintf("ArduinoOTA: %u %%\n", percent);
    }
  });
  ArduinoOTA.onError([](ota_error_t error) {
    firmwareUpdateInProgress = false;
    debugPrintf("ArduinoOTA-Fehler %u.\n", static_cast<unsigned int>(error));
  });
  ArduinoOTA.begin();
  MDNS.addService("http", "tcp", 80);
  debugPrintf("ArduinoOTA und mDNS bereit: %s.local\n", ConfigDefaults::MDNS_NAME);
}
#endif

void startMdns() {
  if (WiFi.status() != WL_CONNECTED || mdnsStarted) return;
  if (MDNS.begin(ConfigDefaults::MDNS_NAME)) {
    MDNS.addService("http", "tcp", 80);
    mdnsStarted = true;
    debugPrintf("mDNS bereit: http://%s.local\n", ConfigDefaults::MDNS_NAME);
  } else {
    debugPrintln("mDNS konnte nicht gestartet werden.");
  }
}

void handleNotFound() {
  server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/inverter", true);
  server.send(302, "text/plain", "");
}

void startWebServer() {
  server.on("/", HTTP_GET, handleIndex);
  server.on("/inverter", HTTP_GET, handleIndex);
  server.on("/battery", HTTP_GET, handleIndex);
  server.on("/forecast", HTTP_GET, handleForecastPage);
  server.on("/load-profile", HTTP_GET, handleLoadProfilePage);
  server.on("/history", HTTP_GET, handleHistoryPage);
  server.on("/settings", HTTP_GET, handleSettings);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/firmware", HTTP_GET, handleFirmwarePage);
  server.on("/update", HTTP_POST, handleFirmwareUploadFinished, handleFirmwareUpload);
  server.on("/api/values", HTTP_GET, handleValuesApi);
  server.on("/api/forecast", HTTP_GET, handleForecastApi);
  server.on("/api/forecast/refresh", HTTP_POST, handleForecastRefresh);
  server.on("/api/load-profile", HTTP_GET, handleLoadProfileApi);
  server.on("/api/load-profile/reset", HTTP_POST, handleLoadProfileReset);
  server.on("/api/history", HTTP_GET, handleHistoryApi);
  server.on("/api/pushover/test", HTTP_POST, handlePushoverTest);
  server.on("/about", HTTP_GET, handleAboutPage);
  server.on("/debug", HTTP_GET, handleLegacyDebugPage);
  server.on("/api/debug", HTTP_GET, handleDebugLog);
  server.on("/api/debug/status", HTTP_GET, handleDebugStatus);
  server.on("/api/debug/toggle", HTTP_POST, handleDebugToggle);
  server.on("/api/debug/clear", HTTP_POST, handleDebugClear);
  server.on("/generate_204", HTTP_ANY, handleIndex);
  server.on("/hotspot-detect.html", HTTP_ANY, handleIndex);
  server.onNotFound(handleNotFound);
  server.begin();
  debugPrintln("Webserver gestartet.");
}

void solarPrognoseMonitorSetup() {
  Serial.begin(115200);
  delay(200);
  debugPrintln();
  debugPrintf("Solar Prognose Monitor %s startet ...\n", ConfigDefaults::FIRMWARE_VERSION);
  loadConfig();
  loadStoredProfile();
  loadStoredHistory();
  initializeSurplusStages();
  initializeRippleInputs();
  initializeRs485();
  startNetwork();
  if (WiFi.status() == WL_CONNECTED) {
    configTime(0, 0, "pool.ntp.org", "time.cloudflare.com", "time.google.com");
    timeSyncStarted = true;
  }
  startMdns();
  startWebServer();
  if (config.forecastEnabled && config.forecastBypass) {
    forecast.status = forecastRestorePending
        ? "Bypass: einmaliger Max-SOC-Schreibauftrag wartet auf Ausführung"
        : "Bypass aktiv: keine regelmäßigen Max-SOC-Schreibzugriffe";
  } else {
    forecast.status = config.forecastEnabled
        ? "Warte auf Uhrzeit und Open-Meteo"
        : "Prognosesteuerung deaktiviert";
  }
  nextPollAt = millis();
  nextForecastFetchAt = millis();
  nextForecastControlAt = millis() + 30000UL;
}

void solarPrognoseMonitorLoop() {
  dnsServer.processNextRequest();
  server.handleClient();

  if (!firmwareUpdateInProgress && !config.wifiSsid.isEmpty() && WiFi.status() != WL_CONNECTED && millis() - lastWifiAttempt >= ConfigDefaults::WIFI_RETRY_MS) {
    lastWifiAttempt = millis();
    debugPrintln("WLAN wird erneut verbunden ...");
    WiFi.disconnect();
    WiFi.begin(config.wifiSsid.c_str(), config.wifiPassword.c_str());
  }

  if (!firmwareUpdateInProgress && WiFi.status() == WL_CONNECTED) {
    if (!timeSyncStarted) {
      configTime(0, 0, "pool.ntp.org", "time.cloudflare.com", "time.google.com");
      timeSyncStarted = true;
    }
    startMdns();
  }

  if (!firmwareUpdateInProgress) {
    servicePolling();
    serviceRippleControl();
    controlSurplusStages();
    serviceLoadProfile();
    serviceHistory();
    serviceForecastCharging();
    servicePushover();
  }

  if (restartAt != 0 && timeReached(restartAt)) {
    delay(100);
    ESP.restart();
  }
  delay(1);
}
