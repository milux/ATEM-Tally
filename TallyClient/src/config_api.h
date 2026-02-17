#pragma once

#include <Arduino.h>

#include <functional>

namespace config_api {

struct ClientConfig {
  String apiPassword;
  uint8_t listenInput;
  uint16_t keepAliveSeconds;
  uint16_t restartTimeoutSeconds;
};

void begin(ClientConfig* runtimeConfig,
           std::function<void()> onListenInputChanged,
           std::function<void()> onKeepAliveChanged);

void handleClient();

}  // namespace config_api
