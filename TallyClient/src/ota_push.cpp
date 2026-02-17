#include "ota_push.h"

#include <Arduino.h>
#include <ArduinoOTA.h>
#include <Update.h>
#include <WiFi.h>

#include "ota_signature.h"

namespace ota {

static volatile bool pushOtaInProgress = false;
static volatile unsigned int lastOtaTotalBytes = 0;

static const char* otaErrorName(ota_error_t error) {
  switch (error) {
    case OTA_AUTH_ERROR:
      return "auth";
    case OTA_BEGIN_ERROR:
      return "begin";
    case OTA_CONNECT_ERROR:
      return "connect";
    case OTA_RECEIVE_ERROR:
      return "receive";
    case OTA_END_ERROR:
      return "end";
    default:
      return "unknown";
  }
}

void setupPushOta() {
  ArduinoOTA.setHostname("tallyclient");
  ArduinoOTA.setRebootOnSuccess(false);

#ifdef OTA_UPLOAD_PASSWORD
  if (strlen(OTA_UPLOAD_PASSWORD) > 0) {
    ArduinoOTA.setPassword(OTA_UPLOAD_PASSWORD);
  }
#endif

  ArduinoOTA.onStart([]() {
    pushOtaInProgress = true;
    lastOtaTotalBytes = 0;
    const char* updateType = (ArduinoOTA.getCommand() == U_FLASH) ? "firmware" : "filesystem";
    Serial.printf("OTA start (%s)\n", updateType);
  });

  ArduinoOTA.onEnd([]() {
    pushOtaInProgress = false;
    Serial.println("OTA transfer complete. Verifying staged image before reboot...");

    if (!ota_signature::verifyStagedOtaImage(static_cast<size_t>(lastOtaTotalBytes))) {
      Serial.println("OTA rejected: staged signature validation failed. Staying on current firmware.");
      return;
    }

    Serial.println("OTA staged image verified. Rebooting into new firmware...");
    delay(200);
    ESP.restart();
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    lastOtaTotalBytes = total;
    Serial.printf("OTA progress: %u%%\r", (progress * 100U) / total);
  });

  ArduinoOTA.onError([](ota_error_t error) {
    pushOtaInProgress = false;
    Serial.printf("OTA error[%u:%s]\n", error, otaErrorName(error));
    Serial.printf("Update error: %s\n", Update.errorString());
  });

  ArduinoOTA.begin();
  Serial.println("OTA push listener started.");
}

void handlePushOta() {
  ArduinoOTA.handle();
}

bool isPushOtaInProgress() {
  return pushOtaInProgress;
}

}  // namespace ota
