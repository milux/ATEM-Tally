#include <Arduino.h>
#include <WiFi.h>

extern "C" {
#include "esp_ota_ops.h"
#include "esp_partition.h"
}

#include "config_api.h"
#include "log_output.h"
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
unsigned long lastKeepAliveReplyAt = static_cast<unsigned long>(-1);
unsigned short lastKeepAliveWarningRemainingSeconds = static_cast<unsigned short>(-1);

unsigned long connectAttemptStartedAt = 0;
unsigned long lastConnectAttemptAt = 0;

unsigned long restartTimeoutMs = 0;
unsigned long keepAliveIntervalMs = 0;

static void updateTimingsFromConfig() {
  restartTimeoutMs = static_cast<unsigned long>(config.restartTimeoutSeconds) * 1000UL;
  keepAliveIntervalMs = static_cast<unsigned long>(config.keepAliveSeconds) * 1000UL;
}

static void printPartitionInfo(const esp_partition_t* partition, const char* prefix) {
  if (partition == nullptr) {
    LogSerial.printf("%s<none>\n", prefix);
    return;
  }

  LogSerial.printf(
      "%slabel=%s subtype=%d address=0x%08x size=0x%08x\n",
      prefix,
      partition->label,
      static_cast<int>(partition->subtype),
      partition->address,
      partition->size);
}

static void printOtaPartitionDiagnostics() {
  LogSerial.println("OTA: partition diagnostics");

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
    LogSerial.printf(
        "  app[%d]: label=%s subtype=%d address=0x%08x size=0x%08x\n",
        appPartitionCount,
        partition->label,
        static_cast<int>(partition->subtype),
        partition->address,
        partition->size);
    iterator = esp_partition_next(iterator);
  }

  LogSerial.printf("  app partitions: %d\n", appPartitionCount);
}

void setup() {
  pinMode(PIN_PREVIEW, OUTPUT);
  pinMode(PIN_PROGRAM, OUTPUT);

  LogSerial.begin(115200);

  LogSerial.println();
  LogSerial.println();

  printOtaPartitionDiagnostics();

  // Scan available open WiFis until the tally monitor is found
  for (bool scanSuccessful = false; !scanSuccessful; ) {
    int n = WiFi.scanNetworks();
    for (int i = 0; i < n; ++i) {
      if (WiFi.encryptionType(i) == WIFI_AUTH_OPEN) {
        String ssid = WiFi.SSID(i);
        LogSerial.print("Trying open network ");
        LogSerial.print(ssid);
        WiFi.begin(ssid.c_str());
        // ~ 5 seconds WiFi connect timeout
        for (int i = 0; WiFi.status() != WL_CONNECTED && i < 50; ++i) {
          delay(100);
          LogSerial.print(".");
        }
        if (WiFi.status() == WL_CONNECTED) {
          LogSerial.println(" connected!");
          int res = WiFi.hostByName(TALLY_DNS, tallyIp);
          if (res == 1) {
            LogSerial.print("Tally Server IP address found: ");
            LogSerial.println(tallyIp);
            scanSuccessful = true;
            break;
          } else {
            LogSerial.print("Error code: ");
            LogSerial.println(res);
          }
        } else {
          LogSerial.println(" timed out, skipping...");
          continue;
        }
      } else {
        LogSerial.print("Skipping encrypted network ");
        LogSerial.println(WiFi.SSID(i));
      }
    }
  }

  LogSerial.print("Local IP address: ");
  LogSerial.println(WiFi.localIP());

  config_api::begin(
      &config,
      []() {
        LogSerial.setListenInput(config.listenInput);
        if (client.connected()) {
          client.stop();
        }
        connectAttemptStartedAt = 0;
        lastConnectAttemptAt = 0;
      },
      []() {
        updateTimingsFromConfig();
        connectAttemptStartedAt = 0;
        lastConnectAttemptAt = 0;
      },
      []() {
        LogSerial.setSyslogServer(config.syslogServerDns);
      });

  updateTimingsFromConfig();

  LogSerial.setListenInput(config.listenInput);
  LogSerial.setSyslogServer(config.syslogServerDns);

  ota_signature::confirmPendingOtaImage();

  ota::setupPushOta();
}

// the loop function runs over and over again forever
void loop() {
  LogSerial.flush();

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

    LogSerial.print("Connecting to ");
    LogSerial.print(tallyIp);
    LogSerial.print(":");
    LogSerial.println(PORT);

    if (!client.connect(tallyIp, PORT)) {
      if (now - connectAttemptStartedAt >= restartTimeoutMs) {
        LogSerial.println("Connection timeout exceeded, restarting...");
        ESP.restart();
      }
      return;
    } else {
      LogSerial.println("Connection established.");
    }
    connectAttemptStartedAt = 0;
    lastConnectAttemptAt = 0;

    uint8_t buf[] = {0xffu, 0x01u, config.listenInput};
    client.write(buf, 3);
    lastKeepAliveSentAt = now;
    lastKeepAliveReplyAt = static_cast<unsigned long>(-1);
    lastKeepAliveWarningRemainingSeconds = static_cast<unsigned short>(-1);
  } else {
    const unsigned long now = millis();

    while (client.available() >= 2) {
      uint8_t buf[2];
      int res = client.read(buf, 2);
      if (res < 2) {
        LogSerial.println("Error during message read, restarting...");
        ESP.restart();
      }

      if (buf[0] == 0xffu && buf[1] == 0xffu) {
        lastKeepAliveReplyAt = now;
        continue;
      }

      if (buf[0] != config.listenInput) {
        LogSerial.println("Unexpected input in message read, ignoring...");
        continue;
      }

      LogSerial.println(buf[1]);
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

    if (lastKeepAliveReplyAt < lastKeepAliveSentAt && (lastKeepAliveSentAt - lastKeepAliveReplyAt >= keepAliveIntervalMs)) {
      const unsigned long elapsedSinceReplyMs = now - lastKeepAliveReplyAt;
      if (elapsedSinceReplyMs >= restartTimeoutMs) {
        LogSerial.println("Keep-alive reply timeout exceeded, restarting...");
        ESP.restart();
      }

      const unsigned long remainingMs = restartTimeoutMs - elapsedSinceReplyMs;
      const unsigned short remainingSeconds = (remainingMs + 999UL) / 1000UL;
      if (remainingSeconds != lastKeepAliveWarningRemainingSeconds) {
        LogSerial.print("Warning: keep-alive reply missing; restart in ");
        LogSerial.print(remainingSeconds);
        LogSerial.println("s if no reply is received.");
        lastKeepAliveWarningRemainingSeconds = remainingSeconds;
      }
    } else {
      lastKeepAliveWarningRemainingSeconds = static_cast<unsigned short>(-1);
    }

    // Send keep-alive packages
    if (now - lastKeepAliveSentAt >= keepAliveIntervalMs) {
      uint8_t keepAlive[] = {0xffu, 0xffu};
      client.write(keepAlive, 2);
      lastKeepAliveSentAt = now;
    }
  }
}
