#include <Arduino.h>
#include <Wire.h>
#include <SparkFun_APDS9960.h>

// Objeto que representa al sensor APDS-9960
SparkFun_APDS9960 apds;

// Pines I2C que realmente usamos en nuestro ESP32-S3
const int I2C_SDA = 8;
const int I2C_SCL = 9;

// Variables donde guardaremos las lecturas
uint16_t red = 0;
uint16_t green = 0;
uint16_t blue = 0;
uint16_t ambient = 0;

void setup() {

  // Comunicación con la computadora
  Serial.begin(115200);
  delay(1500);

  Serial.println();
  Serial.println("========================================");
  Serial.println("     CALIBRACION SENSOR APDS-9960");
  Serial.println("========================================");

  // Iniciar I2C usando los GPIO reales del carro
  Wire.begin(I2C_SDA, I2C_SCL);

  // Intentar detectar e inicializar el APDS-9960
  if (apds.init()) {
    Serial.println("Sensor APDS-9960 detectado correctamente.");
  } else {
    Serial.println("ERROR: No se pudo detectar el APDS-9960.");
    while (1) {
      delay(1000);
    }
  }

  // Activar lectura de luz/color
  if (apds.enableLightSensor(false)) {
    Serial.println("Sensor RGB activado.");
  } else {
    Serial.println("ERROR: No se pudo activar el sensor RGB.");
  }

  delay(500);
}

void loop() {

  // Leer los cuatro canales del sensor
  bool lecturaCorrecta =
      apds.readRedLight(red) &&
      apds.readGreenLight(green) &&
      apds.readBlueLight(blue) &&
      apds.readAmbientLight(ambient);

  if (!lecturaCorrecta) {

    Serial.println("Error leyendo el sensor.");

  } else {

    Serial.print("C=");
    Serial.print(ambient);

    Serial.print("   R=");
    Serial.print(red);

    Serial.print("   G=");
    Serial.print(green);

    Serial.print("   B=");
    Serial.println(blue);
  }

  delay(400);
}