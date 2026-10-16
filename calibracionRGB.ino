#include <Wire.h>
#include <SparkFun_APDS9960.h>

// Instancia del sensor
SparkFun_APDS9960 apds = SparkFun_APDS9960();

// Pines I2C para el ESP32-C6 SuperMini (ajusta si usaste otros)
const int I2C_SDA = 6;
const int I2C_SCL = 7;

// Variables para almacenar las lecturas
uint16_t red = 0;
uint16_t green = 0;
uint16_t blue = 0;
uint16_t ambient = 0;

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println("\n========================================");
  Serial.println("   MODO CALIBRACION - APDS-9960 & ESP32  ");
  Serial.println("========================================");

  // Inicializar I2C con los pines definidos
  Wire.begin(I2C_SDA, I2C_SCL); 
  
  // Inicializar el sensor
  if (apds.init()) {
    Serial.println("-> Sensor APDS-9960 inicializado correctamente.");
  } else {
    Serial.println("-> ¡Error al inicializar el sensor!");
  }

  // Activar el sensor de luz
  if (apds.enableLightSensor(false)) {
    Serial.println("-> Sensor de luz activo. ¡Listo para medir!\n");
  } else {
    Serial.println("-> ¡Error al activar el sensor de luz!");
  }
  
  delay(500);
}

void loop() {
  // Leer los valores de los canales de color y luz ambiental
  if (!apds.readRedLight(red) ||
      !apds.readGreenLight(green) ||
      !apds.readBlueLight(blue) ||
      !apds.readAmbientLight(ambient)) {
    Serial.println("Error al leer los valores de luz.");
  } else {
    // Imprimir formato limpio y ordenado para ver las escalas fácilmente
    Serial.print("Ambiente (C): ");
    Serial.print(ambient);
    Serial.print("\t|\t");
    
    Serial.print("Rojo (R): ");
    Serial.print(red);
    Serial.print("\t|\t");
    
    Serial.print("Verde (G): ");
    Serial.print(green);
    Serial.print("\t|\t");
    
    Serial.print("Azul (B): ");
    Serial.println(blue);
  }
  
  delay(400); // Pausa de 400ms entre lecturas para que el monitor serie sea fácil de leer
}