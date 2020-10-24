#include <Arduino.h>
#include <WiFi.h>

const int INACTIVE = 0;
const int PREVIEW = 1;
const int PROGRAM = 2;
const int PIN_PREVIEW = 2;
const int PIN_PROGRAM = 15;

const char *SSID = "FCG Regensburg";
const IPAddress IP(192, 168, 77, 30);
const int PORT = 7411;
const bool USE_PREVIEW = true;
const uint8_t LISTEN_INPUT = 3;

WiFiClient client;

void setup() {
  pinMode(PIN_PREVIEW, OUTPUT);
  pinMode(PIN_PROGRAM, OUTPUT);

  Serial.begin(115200);

  Serial.println();
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(SSID);

  WiFi.begin(SSID);

  while (WiFi.status() != WL_CONNECTED) {
    delay(50);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi connected");

  Serial.print("Local IP address: ");
  Serial.println(WiFi.localIP());
}

// the loop function runs over and over again forever
void loop() {
  if (!client.connected()) {
    Serial.print("Connecting to ");
    Serial.print(IP);
    Serial.print(":");
    Serial.println(PORT);
    if (!client.connect(IP, PORT)) {
      Serial.println("Connection failed.");
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