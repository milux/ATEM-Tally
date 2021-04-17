#include <Arduino.h>
#include <WiFi.h>

const int INACTIVE = 0;
const int PREVIEW = 1;
const int PROGRAM = 2;
const int PIN_PREVIEW = 2;
const int PIN_PROGRAM = 15;

const char* TALLY_DNS = "tally.local";
const int PORT = 7411;
const bool USE_PREVIEW = true;
const uint8_t LISTEN_INPUT = 3;

IPAddress tallyIp;
WiFiClient client;

void setup() {
  pinMode(PIN_PREVIEW, OUTPUT);
  pinMode(PIN_PROGRAM, OUTPUT);

  Serial.begin(115200);

  Serial.println();
  Serial.println();

  int n = WiFi.scanNetworks();
  for (int i = 0; i < n; ++i) {
    if (WiFi.encryptionType(i) == WIFI_AUTH_OPEN) {
      String ssid = WiFi.SSID(i);
      Serial.print("Trying open network ");
      Serial.print(ssid);
      Serial.print(":");
      WiFi.begin(ssid.c_str());
      while (WiFi.status() != WL_CONNECTED) {
        delay(50);
        Serial.print(".");
      }
      Serial.println(" connected!");
      int err = WiFi.hostByName(TALLY_DNS, tallyIp);
      if (err == 1) {
        Serial.print("Found IP address: ");
        Serial.println(tallyIp);
        break;
      } else {
        Serial.print("Error code: ");
        Serial.println(err);
      }
    } else {
      Serial.print("Skipping encrypted network ");
      Serial.println(WiFi.SSID(i));
    }

    delay(10000);
  }

  Serial.print("Local IP address: ");
  Serial.println(WiFi.localIP());
}

// the loop function runs over and over again forever
void loop() {
  if (!client.connected()) {
    Serial.print("Connecting to ");
    Serial.print(tallyIp);
    Serial.print(":");
    Serial.println(PORT);
    if (!client.connect(tallyIp, PORT)) {
      Serial.println("Connection failed.");
      delay(5000);
      return;
    } else {
      Serial.println("Connection established.");
    }
    client.write(LISTEN_INPUT);
  }

  while (client.available()) {
    int state = client.read();
    Serial.println(state);
    switch (state) {
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
    }
  }

  delay(50);
}