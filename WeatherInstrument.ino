#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include "BMP085.h"
#include "BH1750.h"
#include "Config.h"

WiFiClient clientThingSpeak;
WiFiClientSecure clientSefinek;

#define DHT11_Data_Pin D7
#define SDA_BH1750_BMP180_Pin D2
#define SCL_BH1750_BMP180_Pin D1

int currentTemperature    = 0;
int currentHumidity       = 0;
int currentPressure       = 0;
float currentPressure_hPa = 0;
int currentLight          = 0;

long readTime             = 0;
long uploadTimeThingSpeak = 0;
long uploadTimeSefinek    = 0;

Adafruit_BMP085 bmpSensor;
BH1750 lightSensor;

void checkConfig() {
  if (strlen(WIFI_SSID) == 0) {
    Serial.println(F("[CONFIG ERROR] WIFI_SSID is empty."));
    while (true);
  }

  if (strlen(WIFI_PASSWORD) == 0) {
    Serial.println(F("[CONFIG ERROR] WIFI_PASSWORD is empty."));
    while (true);
  }

  if (strlen(THINGSPEAK_SECRET) == 0) {
    Serial.println(F("[CONFIG ERROR] THINGSPEAK_SECRET is empty."));
    while (true);
  }
}

void setup() {
  Serial.begin(115200);
  checkConfig();

  Wire.begin(SDA_BH1750_BMP180_Pin, SCL_BH1750_BMP180_Pin);
  lightSensor.begin();

  if (!bmpSensor.begin()) {
    Serial.println(F("[BOOT] BMP180 not found. Halting."));
    while (true);
  }
  Serial.println(F("[BOOT] BMP180 ready"));

  Serial.print(F("[BOOT] Connecting to Wi-Fi"));
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println(F("\n[BOOT] Wi-Fi connected"));

  clientThingSpeak.setTimeout(300000);
  clientSefinek.setTimeout(300000);
  clientSefinek.setInsecure();
}

void readTemperatureHumidity() {
  int bits[40] = {0};
  unsigned long time1;
  unsigned int loopCnt;

retry:
  delay(2000);
  pinMode(DHT11_Data_Pin, OUTPUT);
  digitalWrite(DHT11_Data_Pin, LOW); delay(20);
  digitalWrite(DHT11_Data_Pin, HIGH); delayMicroseconds(40);
  digitalWrite(DHT11_Data_Pin, LOW);
  pinMode(DHT11_Data_Pin, INPUT);

  loopCnt = 10000;
  while (digitalRead(DHT11_Data_Pin) != HIGH)
    if (--loopCnt == 0) { Serial.println(F("[DHT11] No HIGH")); goto retry; }

  loopCnt = 30000;
  while (digitalRead(DHT11_Data_Pin) != LOW)
    if (--loopCnt == 0) { Serial.println(F("[DHT11] No LOW")); goto retry; }

  for (int i = 0; i < 40; i++) {
    while (digitalRead(DHT11_Data_Pin) == LOW);
    time1 = micros();
    while (digitalRead(DHT11_Data_Pin) == HIGH);
    bits[i] = (micros() - time1 > 50) ? 1 : 0;
  }

  currentHumidity = 0;
  currentTemperature = 0;
  for (int i = 0; i < 8; i++) currentHumidity = (currentHumidity << 1) | bits[i];
  for (int i = 16; i < 24; i++) currentTemperature = (currentTemperature << 1) | bits[i];

  Serial.printf("[SENSORS] Temp: %d°C, Humidity: %d%%\n", currentTemperature, currentHumidity);
}

void readLight() {
  currentLight = lightSensor.readLightLevel();
  Serial.printf("[SENSORS] Light: %d lx\n", currentLight);
}

void readAtmosphere() {
  currentPressure = bmpSensor.readPressure();
  currentPressure_hPa = currentPressure / 100.0;
  Serial.printf("[SENSORS] Pressure: %.2f hPa\n", currentPressure_hPa);
}

void parseHttpBody(WiFiClient& client) {
  unsigned long start = millis();
  bool bodyReached = false;

  while (client.connected() || client.available()) {
    if (client.available()) {
      String line = client.readStringUntil('\n');
      if (!bodyReached && (line == "\r" || line.length() == 0)) {
        bodyReached = true;
        continue;
      }
      break;
    }

    if (millis() - start > 3000) break;
  }

  client.stop();
}

void uploadDataToThingSpeak() {
  if (!clientThingSpeak.connect(THINGSPEAK_HOST, THINGSPEAK_PORT)) {
    Serial.println(F("[ThingSpeak] Connection failed"));
    return;
  }

  String url = "/update?api_key=" + String(THINGSPEAK_SECRET) +
               "&field1=" + currentTemperature +
               "&field2=" + currentHumidity +
               "&field3=" + currentLight +
               "&field4=" + String(currentPressure_hPa, 2);

  clientThingSpeak.print("GET " + url + " HTTP/1.1\r\n" +
                         "Host: " + String(THINGSPEAK_HOST) + "\r\n" +
                         "Connection: close\r\n\r\n");

  parseHttpBody(clientThingSpeak);
}

void uploadDataToSefinek() {
  if (!clientSefinek.connect(SEFINEK_HOST, SEFINEK_PORT)) {
    Serial.println(F("[Sefinek] Connection failed"));
    return;
  }

  String postData = "temperature=" + String(currentTemperature) +
                    "&humidity=" + currentHumidity +
                    "&light=" + currentLight +
                    "&pressure=" + String(currentPressure_hPa, 2);

  clientSefinek.print(F("POST /api/v2/ideaspark/sensors HTTP/1.1\r\n"));
  clientSefinek.print("Host: " + String(SEFINEK_HOST) + "\r\n");
  clientSefinek.print("X-API-Key: " + String(SEFINEK_SECRET) + "\r\n");
  clientSefinek.print(F("Content-Type: application/x-www-form-urlencoded\r\n"));
  clientSefinek.print("Content-Length: " + String(postData.length()) + "\r\n");
  clientSefinek.print(F("Connection: close\r\n\r\n"));
  clientSefinek.print(postData);

  parseHttpBody(clientSefinek);
}

void loop() {
  if (millis() - readTime > 10000) {
    readTemperatureHumidity();
    readLight();
    readAtmosphere();
    readTime = millis();
  }

  if (millis() - uploadTimeThingSpeak > 60000) {
    Serial.println(F("[ThingSpeak] Uploading..."));
    uploadDataToThingSpeak();
    uploadTimeThingSpeak = millis();
  }

  if (millis() - uploadTimeSefinek > 40000 && strlen(SEFINEK_SECRET) != 0) {
    Serial.println(F("[Sefinek] Uploading..."));
    uploadDataToSefinek();
    uploadTimeSefinek = millis();
  }
}
