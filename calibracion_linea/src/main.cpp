#include <Arduino.h>

// Pines donde están conectados los sensores de línea
const int S1 = 4;
const int S2 = 5;
const int S3 = 6;
const int S4 = 7;
const int S5 = 15;

void setup() {

    // Inicia la comunicación entre el ESP32 y la computadora
    Serial.begin(115200);

    // Los cinco GPIO reciben señales desde el sensor
    pinMode(S1, INPUT);
    pinMode(S2, INPUT);
    pinMode(S3, INPUT);
    pinMode(S4, INPUT);
    pinMode(S5, INPUT);

    // Esperamos un momento para que el puerto serial esté listo
    delay(1000);

    Serial.println("=== CALIBRACION SENSOR DE LINEA ===");
}

void loop() {

    // Leemos el estado actual de cada sensor
    int valorS1 = !digitalRead(S1);
    int valorS2 = !digitalRead(S2);
    int valorS3 = !digitalRead(S3);
    int valorS4 = !digitalRead(S4);
    int valorS5 = !digitalRead(S5);


    // Mostramos las cinco lecturas en una sola línea
    Serial.print("S1=");
    Serial.print(valorS1);

    Serial.print("   S2=");
    Serial.print(valorS2);

    Serial.print("   S3=");
    Serial.print(valorS3);

    Serial.print("   S4=");
    Serial.print(valorS4);

    Serial.print("   S5=");
    Serial.println(valorS5);

    // Esperamos 300 ms antes de volver a medir
    delay(300);
}