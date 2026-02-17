#include <Arduino.h>
#include <WiFi.h>

extern "C" {
#include "esp_ota_ops.h"
#include "esp_partition.h"
}

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
const uint8_t LISTEN_INPUT = 2;

IPAddress tallyIp;
WiFiClient client;

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

      delay(10000);
    }
  }

  Serial.print("Local IP address: ");
  Serial.println(WiFi.localIP());

  ota_signature::confirmPendingOtaImage();

  ota::setupPushOta();
}

// the loop function runs over and over again forever
void loop() {
  ota::handlePushOta();

  if (ota::isPushOtaInProgress()) {
    delay(10);
    return;
  }

  if (!client.connected()) {
    Serial.print("Connecting to ");
    Serial.print(tallyIp);
    Serial.print(":");
    Serial.println(PORT);
    if (!client.connect(tallyIp, PORT)) {
      // We want to reboot here so that WiFi network scanning is re-attempted
      Serial.println("Connection failed, restarting...");
      ESP.restart();
    } else {
      Serial.println("Connection established.");
    }
    uint8_t buf[] = {0xffu, 0x01u, LISTEN_INPUT};
    client.write(buf, 3);
  }

  while (client.available() >= 2) {
    uint8_t buf[2];
    int res = client.read(buf, 2);
    if (res < 2 || buf[0] != LISTEN_INPUT) {
      Serial.println("Error during message read, restarting...");
      ESP.restart();
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

  delay(50);
}