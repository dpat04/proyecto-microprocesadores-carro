#include <Arduino.h>

void setup() {
  Serial.begin(115200);

  delay(1000);

  Serial.println("ESP32-S3 iniciada correctamente");
}

void loop() {
  Serial.println("ESP32 funcionando");

  delay(1000);
}