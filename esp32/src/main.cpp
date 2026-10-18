#include <Arduino.h>
#include <Wire.h>

// Configuración I2C
const uint8_t I2C_ADDRESS = 0x28;
const int SDA_PIN = 8;
const int SCL_PIN = 9;


// Se ejecuta cuando la Raspberry ENVÍA datos al ESP32
void recibirDatos(int cantidad) {

    Serial.print("Recibidos por I2C: ");

    while (Wire.available()) {
        uint8_t dato = Wire.read();

        Serial.print("0x");
        Serial.print(dato, HEX);
        Serial.print(" ");
    }

    Serial.println();
}


// Se ejecuta cuando la Raspberry PIDE datos al ESP32
void enviarDatos() {

    uint8_t respuesta = 0xA5;

    Wire.write(respuesta);

    Serial.println("Raspberry solicito dato -> enviado 0xA5");
}


void setup() {

    Serial.begin(115200);

    delay(2000);

    Serial.println("Iniciando ESP32-S3...");

    // Definimos qué funciones atenderán las comunicaciones
    Wire.onReceive(recibirDatos);
    Wire.onRequest(enviarDatos);

    // ESP32 como dispositivo I2C en dirección 0x28
    bool iniciado = Wire.begin(
        I2C_ADDRESS,
        SDA_PIN,
        SCL_PIN,
        100000
    );

    if (iniciado) {
        Serial.println("I2C iniciado correctamente");
        Serial.println("Direccion: 0x28");
        Serial.println("SDA: GPIO8");
        Serial.println("SCL: GPIO9");
    }
    else {
        Serial.println("ERROR al iniciar I2C");
    }
}


void loop() {

    delay(1000);

}