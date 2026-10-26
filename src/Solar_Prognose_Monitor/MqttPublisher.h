/*
 * Solar Prognose Monitor
 * Copyright (C) 2026 Marcus Sonntag / MS-De-sign
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

#pragma once

#include <Arduino.h>
#include <WiFiClient.h>

// Kleiner ausschließlich sendender MQTT-3.1.1-Client. Es werden bewusst
// weder SUBSCRIBE noch eingehende Steuerbefehle unterstützt.
class MqttPublisher {
 public:
  MqttPublisher();

  void configure(const String &host, uint16_t port, const String &clientId,
                 const String &username, const String &password,
                 const String &availabilityTopic);
  bool connect();
  void disconnect(bool graceful = true);
  void loop();
  bool publish(const String &topic, const String &payload, bool retained);

  bool connected();
  const String &lastError() const;
  uint32_t connectedAt() const;
  uint32_t lastActivityAt() const;

 private:
  static constexpr uint16_t KEEPALIVE_SECONDS = 30;
  static constexpr uint32_t RESPONSE_TIMEOUT_MS = 1500;
  static constexpr size_t CONNECT_PACKET_CAPACITY = 768;

  WiFiClient client_;
  String host_;
  uint16_t port_;
  String clientId_;
  String username_;
  String password_;
  String availabilityTopic_;
  String lastError_;
  bool connected_;
  bool pingOutstanding_;
  uint32_t connectedAt_;
  uint32_t lastActivityAt_;
  uint32_t pingSentAt_;

  bool writeAll(const uint8_t *data, size_t length);
  bool writeRemainingLength(size_t length);
  bool readByte(uint8_t &value, uint32_t timeoutMs);
  void markDisconnected(const String &reason);
  void drainIncoming();
};
