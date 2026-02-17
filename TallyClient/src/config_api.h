#pragma once

#include <Arduino.h>

#include <functional>

namespace config_api {

struct ClientConfig {
  String apiPassword;
  uint8_t listenInput;
  uint16_t keepAliveSeconds;
  uint16_t restartTimeoutSeconds;
  String syslogServerDns;
};

void begin(ClientConfig* runtimeConfig,
           std::function<void()> onListenInputChanged,
           std::function<void()> onKeepAliveChanged,
           std::function<void()> onSyslogServerChanged);

void handleClient();

}  // namespace config_api
