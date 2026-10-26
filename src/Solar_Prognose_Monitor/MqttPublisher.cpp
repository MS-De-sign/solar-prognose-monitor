/*
 * Solar Prognose Monitor
 * Copyright (C) 2026 Marcus Sonntag / MS-De-sign
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

#include "MqttPublisher.h"

namespace {

bool appendByte(uint8_t *buffer, size_t capacity, size_t &length, uint8_t value) {
  if (length >= capacity) return false;
  buffer[length++] = value;
  return true;
}

bool appendMqttString(uint8_t *buffer, size_t capacity, size_t &length,
                      const String &value) {
  if (value.length() > 65535 || length + 2 + value.length() > capacity) return false;
  buffer[length++] = static_cast<uint8_t>((value.length() >> 8) & 0xFF);
  buffer[length++] = static_cast<uint8_t>(value.length() & 0xFF);
  memcpy(buffer + length, value.c_str(), value.length());
  length += value.length();
  return true;
}

}  // namespace

MqttPublisher::MqttPublisher()
    : port_(1883), connected_(false), pingOutstanding_(false), connectedAt_(0),
      lastActivityAt_(0), pingSentAt_(0) {}

void MqttPublisher::configure(const String &host, uint16_t port, const String &clientId,
                              const String &username, const String &password,
                              const String &availabilityTopic) {
  const bool changed = host_ != host || port_ != port || clientId_ != clientId
      || username_ != username || password_ != password
      || availabilityTopic_ != availabilityTopic;
  if (changed && connected_) disconnect(true);
  host_ = host;
  port_ = port;
  clientId_ = clientId;
  username_ = username;
  password_ = password;
  availabilityTopic_ = availabilityTopic;
}

bool MqttPublisher::writeAll(const uint8_t *data, size_t length) {
  size_t written = 0;
  while (written < length && client_.connected()) {
    const size_t part = client_.write(data + written, length - written);
    if (part == 0) {
      delay(1);
      if (!client_.connected()) break;
      continue;
    }
    written += part;
  }
  if (written != length) {
    markDisconnected("MQTT-Verbindung beim Senden abgebrochen");
    return false;
  }
  lastActivityAt_ = millis();
  return true;
}

bool MqttPublisher::writeRemainingLength(size_t length) {
  uint8_t encoded[4];
  size_t count = 0;
  do {
    uint8_t digit = static_cast<uint8_t>(length % 128U);
    length /= 128U;
    if (length > 0) digit |= 0x80;
    if (count >= sizeof(encoded)) return false;
    encoded[count++] = digit;
  } while (length > 0);
  return writeAll(encoded, count);
}

bool MqttPublisher::readByte(uint8_t &value, uint32_t timeoutMs) {
  const uint32_t started = millis();
  while (client_.connected() && millis() - started < timeoutMs) {
    if (client_.available()) {
      value = static_cast<uint8_t>(client_.read());
      lastActivityAt_ = millis();
      return true;
    }
    delay(1);
  }
  return false;
}

bool MqttPublisher::connect() {
  if (connected()) return true;
  disconnect(false);
  if (host_.isEmpty() || port_ == 0 || clientId_.isEmpty() || availabilityTopic_.isEmpty()) {
    lastError_ = "MQTT-Konfiguration unvollständig";
    return false;
  }
  client_.setTimeout(RESPONSE_TIMEOUT_MS);
  client_.setNoDelay(true);
  if (!client_.connect(host_.c_str(), port_, static_cast<int32_t>(RESPONSE_TIMEOUT_MS))) {
    lastError_ = "MQTT-Broker nicht erreichbar";
    return false;
  }

  uint8_t body[CONNECT_PACKET_CAPACITY];
  size_t length = 0;
  const String protocol = "MQTT";
  if (!appendMqttString(body, sizeof(body), length, protocol)
      || !appendByte(body, sizeof(body), length, 4)) {
    markDisconnected("MQTT-CONNECT konnte nicht aufgebaut werden");
    return false;
  }

  uint8_t flags = 0x02 | 0x04 | 0x20;  // Clean Session, Last Will, Will Retain.
  if (!username_.isEmpty()) flags |= 0x80;
  if (!username_.isEmpty() && !password_.isEmpty()) flags |= 0x40;
  if (!appendByte(body, sizeof(body), length, flags)
      || !appendByte(body, sizeof(body), length, KEEPALIVE_SECONDS >> 8)
      || !appendByte(body, sizeof(body), length, KEEPALIVE_SECONDS & 0xFF)
      || !appendMqttString(body, sizeof(body), length, clientId_)
      || !appendMqttString(body, sizeof(body), length, availabilityTopic_)
      || !appendMqttString(body, sizeof(body), length, String("offline"))
      || (!username_.isEmpty() && !appendMqttString(body, sizeof(body), length, username_))
      || (!username_.isEmpty() && !password_.isEmpty()
          && !appendMqttString(body, sizeof(body), length, password_))) {
    markDisconnected("MQTT-Zugangsdaten sind zu lang");
    return false;
  }

  const uint8_t fixedHeader = 0x10;
  if (!writeAll(&fixedHeader, 1) || !writeRemainingLength(length)
      || !writeAll(body, length)) return false;

  uint8_t response[4];
  for (size_t i = 0; i < sizeof(response); ++i) {
    if (!readByte(response[i], RESPONSE_TIMEOUT_MS)) {
      markDisconnected("Keine MQTT-CONNACK-Antwort");
      return false;
    }
  }
  if (response[0] != 0x20 || response[1] != 0x02 || response[2] != 0x00
      || response[3] != 0x00) {
    const uint8_t code = response[3];
    markDisconnected("MQTT-Anmeldung abgelehnt, Code " + String(code));
    return false;
  }

  connected_ = true;
  pingOutstanding_ = false;
  connectedAt_ = millis();
  lastActivityAt_ = millis();
  lastError_ = "";
  if (!publish(availabilityTopic_, "online", true)) return false;
  return true;
}

void MqttPublisher::disconnect(bool graceful) {
  if (client_.connected() && graceful) {
    if (connected_ && !availabilityTopic_.isEmpty()) {
      publish(availabilityTopic_, "offline", true);
    }
    const uint8_t packet[2] = {0xE0, 0x00};
    client_.write(packet, sizeof(packet));
    client_.flush();
  }
  client_.stop();
  connected_ = false;
  pingOutstanding_ = false;
  connectedAt_ = 0;
}

void MqttPublisher::markDisconnected(const String &reason) {
  lastError_ = reason;
  client_.stop();
  connected_ = false;
  pingOutstanding_ = false;
  connectedAt_ = 0;
}

bool MqttPublisher::publish(const String &topic, const String &payload, bool retained) {
  if (!connected()) {
    markDisconnected("MQTT ist nicht verbunden");
    return false;
  }
  if (topic.isEmpty() || topic.length() > 65535) {
    lastError_ = "Ungültiges MQTT-Topic";
    return false;
  }
  const size_t remaining = 2 + topic.length() + payload.length();
  const uint8_t header = retained ? 0x31 : 0x30;
  const uint8_t topicLength[2] = {
      static_cast<uint8_t>((topic.length() >> 8) & 0xFF),
      static_cast<uint8_t>(topic.length() & 0xFF)};
  if (!writeAll(&header, 1) || !writeRemainingLength(remaining)
      || !writeAll(topicLength, sizeof(topicLength))
      || !writeAll(reinterpret_cast<const uint8_t *>(topic.c_str()), topic.length())
      || !writeAll(reinterpret_cast<const uint8_t *>(payload.c_str()), payload.length())) {
    return false;
  }
  lastError_ = "";
  return true;
}

void MqttPublisher::drainIncoming() {
  while (client_.available()) {
    const int header = client_.read();
    if (header < 0) return;
    size_t remaining = 0;
    size_t multiplier = 1;
    uint8_t digit = 0;
    do {
      if (!readByte(digit, 50)) return;
      remaining += (digit & 0x7F) * multiplier;
      multiplier *= 128;
    } while ((digit & 0x80) && multiplier <= 128UL * 128UL * 128UL);
    while (remaining-- > 0) {
      uint8_t ignored = 0;
      if (!readByte(ignored, 50)) return;
    }
    if ((header & 0xF0) == 0xD0) pingOutstanding_ = false;
  }
}

void MqttPublisher::loop() {
  if (!connected_) return;
  if (!client_.connected()) {
    markDisconnected("MQTT-Verbindung getrennt");
    return;
  }
  drainIncoming();
  const uint32_t now = millis();
  if (pingOutstanding_) {
    if (now - pingSentAt_ > 10000UL) markDisconnected("MQTT-PING ohne Antwort");
    return;
  }
  if (now - lastActivityAt_ >= (KEEPALIVE_SECONDS * 500UL)) {
    const uint8_t ping[2] = {0xC0, 0x00};
    if (writeAll(ping, sizeof(ping))) {
      pingOutstanding_ = true;
      pingSentAt_ = now;
    }
  }
}

bool MqttPublisher::connected() {
  return connected_ && client_.connected();
}

const String &MqttPublisher::lastError() const {
  return lastError_;
}

uint32_t MqttPublisher::connectedAt() const {
  return connectedAt_;
}

uint32_t MqttPublisher::lastActivityAt() const {
  return lastActivityAt_;
}
