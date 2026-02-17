#include <Arduino.h>
#include <WiFi.h>

extern "C" {
#include "esp_ota_ops.h"
#include "esp_partition.h"
}

#include "config_api.h"
#include "ota_push.h"
#include "ota_signature.h"

const int INACTIVE = 0;
const int PREVIEW = 1;
const int PROGRAM = 2;
const int PREVIEW_PROGRAM = 3;
const int PIN_PREVIEW = 2;
const int PIN_PROGRAM = 15;

const char* TALLY_DNS = "tally.internal";
const int PORT = 7411;
const bool USE_PREVIEW = true;
const unsigned long CONNECT_RETRY_INTERVAL_MS = 1000UL;

IPAddress tallyIp;
WiFiClient client;
config_api::ClientConfig config;

unsigned long lastKeepAliveSentAt = 0;
unsigned long lastKeepAliveReplyAt = 0;
unsigned long connectAttemptStartedAt = 0;
unsigned long lastConnectAttemptAt = 0;

static void printPartitionInfo(const esp_partition_t* partition, const char* prefix) {
  if (partition == nullptr) {
    Serial.printf("%s<none>\n", prefix);
    return;
  }

  Serial.printf(
      "%slabel=%s subtype=%d address=0x%08x size=0x%08x\n",
      prefix,
      partition->label,
      static_cast<int>(partition->subtype),
      partition->address,
      partition->size);
}

static void printOtaPartitionDiagnostics() {
  Serial.println("OTA: partition diagnostics");

  const esp_partition_t* running = esp_ota_get_running_partition();
  printPartitionInfo(running, "  running: ");

  const esp_partition_t* boot = esp_ota_get_boot_partition();
  printPartitionInfo(boot, "  boot:    ");

  const esp_partition_t* next = esp_ota_get_next_update_partition(nullptr);
  printPartitionInfo(next, "  next:    ");

  esp_partition_iterator_t iterator =
      esp_partition_find(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, nullptr);
  int appPartitionCount = 0;
  while (iterator != nullptr) {
    const esp_partition_t* partition = esp_partition_get(iterator);
    ++appPartitionCount;
    Serial.printf(
        "  app[%d]: label=%s subtype=%d address=0x%08x size=0x%08x\n",
        appPartitionCount,
        partition->label,
        static_cast<int>(partition->subtype),
        partition->address,
        partition->size);
    iterator = esp_partition_next(iterator);
  }

  Serial.printf("  app partitions: %d\n", appPartitionCount);
}

void setup() {
  pinMode(PIN_PREVIEW, OUTPUT);
  pinMode(PIN_PROGRAM, OUTPUT);

  Serial.begin(115200);

  Serial.println();
  Serial.println();

  printOtaPartitionDiagnostics();

  // Scan available open WiFis until the tally monitor is found
  for (bool scanSuccessful = false; !scanSuccessful; ) {
    int n = WiFi.scanNetworks();
    for (int i = 0; i < n; ++i) {
      if (WiFi.encryptionType(i) == WIFI_AUTH_OPEN) {
        String ssid = WiFi.SSID(i);
        Serial.print("Trying open network ");
        Serial.print(ssid);
        WiFi.begin(ssid.c_str());
        // ~ 5 seconds WiFi connect timeout
        for (int i = 0; WiFi.status() != WL_CONNECTED && i < 50; ++i) {
          delay(100);
          Serial.print(".");
        }
        if (WiFi.status() == WL_CONNECTED) {
          Serial.println(" connected!");
          int res = WiFi.hostByName(TALLY_DNS, tallyIp);
          if (res == 1) {
            Serial.print("Found IP address: ");
            Serial.println(tallyIp);
            scanSuccessful = true;
            break;
          } else {
            Serial.print("Error code: ");
            Serial.println(res);
          }
        } else {
          Serial.println(" timed out, skipping...");
          continue;
        }
      } else {
        Serial.print("Skipping encrypted network ");
        Serial.println(WiFi.SSID(i));
      }
    }
  }

  Serial.print("Local IP address: ");
  Serial.println(WiFi.localIP());

  config_api::begin(
      &config,
      []() {
        if (client.connected()) {
          client.stop();
        }
        connectAttemptStartedAt = 0;
        lastConnectAttemptAt = 0;
      },
      []() {
        const unsigned long now = millis();
        lastKeepAliveSentAt = now;
        lastKeepAliveReplyAt = now;
        connectAttemptStartedAt = 0;
        lastConnectAttemptAt = 0;
      });

  ota_signature::confirmPendingOtaImage();

  ota::setupPushOta();
}

// the loop function runs over and over again forever
void loop() {
  ota::handlePushOta();

  // Restarts forbidden during OTA update
  if (ota::isPushOtaInProgress()) {
    return;
  }

  config_api::handleClient();

  if (!client.connected()) {
    const unsigned long now = millis();
    if (connectAttemptStartedAt == 0) {
      connectAttemptStartedAt = now;
    }

    // Throttle connection attempts when the tally monitor is not available
    if (now - lastConnectAttemptAt < CONNECT_RETRY_INTERVAL_MS) {
      return;
    }
    lastConnectAttemptAt = now;

    Serial.print("Connecting to ");
    Serial.print(tallyIp);
    Serial.print(":");
    Serial.println(PORT);

    if (!client.connect(tallyIp, PORT)) {
      const unsigned long restartTimeoutMs = static_cast<unsigned long>(config.restartTimeoutSeconds) * 1000UL;
      if (now - connectAttemptStartedAt >= restartTimeoutMs) {
        Serial.println("Connection timeout exceeded, restarting...");
        ESP.restart();
      }
      return;
    } else {
      Serial.println("Connection established.");
    }
    connectAttemptStartedAt = 0;
    lastConnectAttemptAt = 0;

    uint8_t buf[] = {0xffu, 0x01u, config.listenInput};
    client.write(buf, 3);
    lastKeepAliveSentAt = now;
    lastKeepAliveReplyAt = now;
  } else {
    const unsigned long now = millis();
    const unsigned long restartTimeoutMs = static_cast<unsigned long>(config.restartTimeoutSeconds) * 1000UL;

    // Handle keep-alive logic
    const unsigned long keepAliveIntervalMs = static_cast<unsigned long>(config.keepAliveSeconds) * 1000UL;
    if (now - lastKeepAliveSentAt >= keepAliveIntervalMs) {
      uint8_t keepAlive[] = {0xffu, 0xffu};
      client.write(keepAlive, 2);
      lastKeepAliveSentAt = now;
    }

    if (now - lastKeepAliveReplyAt >= restartTimeoutMs) {
      Serial.println("Keep-alive reply timeout exceeded, restarting...");
      ESP.restart();
    }

    // Handle tally status messages
    while (client.available() >= 2) {
      uint8_t buf[2];
      int res = client.read(buf, 2);
      if (res < 2) {
        Serial.println("Error during message read, restarting...");
        ESP.restart();
      }

      if (buf[0] == 0xffu && buf[1] == 0xffu) {
        lastKeepAliveReplyAt = now;
        continue;
      }

      if (buf[0] != config.listenInput) {
        Serial.println("Unexpected input in message read, ignoring...");
        continue;
      }

      Serial.println(buf[1]);
      switch (buf[1]) {
        case INACTIVE:
          digitalWrite(PIN_PREVIEW, LOW);
          digitalWrite(PIN_PROGRAM, LOW);
          break;
        case PREVIEW:
          digitalWrite(PIN_PREVIEW, USE_PREVIEW ? HIGH : LOW);
          digitalWrite(PIN_PROGRAM, LOW);
          break;
        case PROGRAM:
          digitalWrite(PIN_PREVIEW, LOW);
          digitalWrite(PIN_PROGRAM, HIGH);
          break;
        case PREVIEW_PROGRAM:
          digitalWrite(PIN_PREVIEW, HIGH);
          digitalWrite(PIN_PROGRAM, HIGH);
          break;
      }
    }
  }
}