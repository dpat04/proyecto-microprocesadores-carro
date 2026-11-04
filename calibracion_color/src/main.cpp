#include <Arduino.h>
#include <Wire.h>
#include <SparkFun_APDS9960.h>

// ======================================================
// SENSOR APDS-9960
// ======================================================

SparkFun_APDS9960 apds;

// Pines I2C del ESP32-S3
const int I2C_SDA = 8;
const int I2C_SCL = 9;


// ======================================================
// VARIABLES DE LECTURA
// ======================================================

uint16_t red = 0;
uint16_t green = 0;
uint16_t blue = 0;
uint16_t ambient = 0;


// ======================================================
// VARIABLES DE CONFIRMACION
// ======================================================

// Último color que vio el sensor
String ultimoColor = "NINGUNO";

// Cantidad de lecturas consecutivas del mismo color
int repeticiones = 0;

// Número de lecturas iguales necesarias para confirmar
const int CONFIRMACIONES_NECESARIAS = 3;


// ======================================================
// CONTADORES
// ======================================================

int contadorRojo = 0;
int contadorVerde = 0;
int contadorAzul = 0;


// ======================================================
// FUNCION PARA DETECTAR COLOR
// ======================================================

String detectarColor(uint16_t c,
                     uint16_t r,
                     uint16_t g,
                     uint16_t b) {

  // --------------------------------------------------
  // MUY POCA LUZ
  // --------------------------------------------------

  if (c < 8) {
    return "NINGUNO";
  }


  // --------------------------------------------------
  // BLANCO
  //
  // Medición aproximada:
  // C = 63
  // R = 22-23
  // G = 41
  // B = 31-32
  // --------------------------------------------------

  if (c >= 50) {
    return "BLANCO";
  }


  // --------------------------------------------------
  // ROJO
  //
  // Medición aproximada:
  // C = 24
  // R = 19
  // G = 17
  // B = 16-17
  //
  // En rojo, R domina sobre G y B.
  // --------------------------------------------------

  if (c >= 18 && c <= 30 &&
      r > g &&
      r > b) {

    return "ROJO";
  }


  // --------------------------------------------------
  // DIFERENCIA ENTRE VERDE Y AZUL
  // --------------------------------------------------

  int diferenciaGB = (int)g - (int)b;


  // --------------------------------------------------
  // VERDE
  //
  // Medición:
  // C ≈ 32
  // R ≈ 15
  // G ≈ 25
  // B ≈ 20
  //
  // G - B ≈ 5
  // --------------------------------------------------

  if (g > r &&
      diferenciaGB >= 4) {

    return "VERDE";
  }


  // --------------------------------------------------
  // AZUL
  //
  // Medición actual:
  // C ≈ 31
  // R ≈ 15
  // G ≈ 23
  // B ≈ 21
  //
  // G - B ≈ 2
  // --------------------------------------------------

  if (g > r &&
      b >= r &&
      diferenciaGB <= 3) {

    return "AZUL";
  }


  // --------------------------------------------------
  // NINGUNA COINCIDENCIA
  // --------------------------------------------------

  return "DESCONOCIDO";
}


// ======================================================
// SETUP
// ======================================================

void setup() {

  // Comunicación con la computadora
  Serial.begin(115200);

  delay(1500);

  Serial.println();
  Serial.println("========================================");
  Serial.println("       SENSOR DE COLOR APDS-9960");
  Serial.println("========================================");


  // ==================================================
  // INICIAR I2C
  // ==================================================

  Wire.begin(I2C_SDA, I2C_SCL);

  Serial.println("I2C iniciado");
  Serial.println("SDA -> GPIO 8");
  Serial.println("SCL -> GPIO 9");


  // ==================================================
  // INICIAR SENSOR
  // ==================================================

  if (apds.init()) {

    Serial.println("APDS-9960 detectado correctamente.");

  } else {

    Serial.println("ERROR: APDS-9960 no detectado.");

    while (1) {
      delay(1000);
    }
  }


  // ==================================================
  // ACTIVAR SENSOR RGB
  // ==================================================

  if (apds.enableLightSensor(false)) {

    Serial.println("Sensor RGB activado correctamente.");

  } else {

    Serial.println("ERROR: No se pudo activar el sensor RGB.");
  }


  Serial.println();
  Serial.println("Iniciando lecturas...");
  Serial.println();

  delay(500);
}


// ======================================================
// LOOP
// ======================================================

void loop() {

  // ==================================================
  // 1. LEER SENSOR
  // ==================================================

  bool lecturaCorrecta =
      apds.readRedLight(red) &&
      apds.readGreenLight(green) &&
      apds.readBlueLight(blue) &&
      apds.readAmbientLight(ambient);


  if (!lecturaCorrecta) {

    Serial.println("ERROR leyendo APDS-9960.");

    delay(100);

    return;
  }


  // ==================================================
  // 2. DETECTAR COLOR
  // ==================================================

  String colorActual =
      detectarColor(ambient, red, green, blue);


  // ==================================================
  // 3. MOSTRAR LECTURAS
  // ==================================================

  Serial.print("C=");
  Serial.print(ambient);

  Serial.print("  R=");
  Serial.print(red);

  Serial.print("  G=");
  Serial.print(green);

  Serial.print("  B=");
  Serial.print(blue);


  // Mostrar diferencia G-B
  int diferenciaGB = (int)green - (int)blue;

  Serial.print("  G-B=");
  Serial.print(diferenciaGB);


  Serial.print("  ---> ");
  Serial.println(colorActual);


  // ==================================================
  // 4. COMPROBAR SI EL COLOR SE REPITE
  // ==================================================

  if (colorActual == ultimoColor) {

    repeticiones++;

  } else {

    ultimoColor = colorActual;

    repeticiones = 1;
  }


  // ==================================================
  // 5. CONFIRMAR DESPUES DE 3 LECTURAS
  // ==================================================

  if (repeticiones == CONFIRMACIONES_NECESARIAS) {


    // ------------------------------------------------
    // ROJO
    // ------------------------------------------------

    if (colorActual == "ROJO") {

      contadorRojo++;

      Serial.println();
      Serial.println(">>>>>>>>>>>>>>>>>>>>>>>>>>>>>");
      Serial.println(">>>    ROJO CONFIRMADO    <<<");
      Serial.println(">>>>>>>>>>>>>>>>>>>>>>>>>>>>>");
    }


    // ------------------------------------------------
    // VERDE
    // ------------------------------------------------

    else if (colorActual == "VERDE") {

      contadorVerde++;

      Serial.println();
      Serial.println(">>>>>>>>>>>>>>>>>>>>>>>>>>>>>");
      Serial.println(">>>   VERDE CONFIRMADO    <<<");
      Serial.println(">>>>>>>>>>>>>>>>>>>>>>>>>>>>>");
    }


    // ------------------------------------------------
    // AZUL
    // ------------------------------------------------

    else if (colorActual == "AZUL") {

      contadorAzul++;

      Serial.println();
      Serial.println(">>>>>>>>>>>>>>>>>>>>>>>>>>>>>");
      Serial.println(">>>    AZUL CONFIRMADO    <<<");
      Serial.println(">>>>>>>>>>>>>>>>>>>>>>>>>>>>>");
    }


    // ==================================================
    // MOSTRAR CONTADORES
    // ==================================================

    if (colorActual == "ROJO" ||
        colorActual == "VERDE" ||
        colorActual == "AZUL") {

      Serial.println();

      Serial.print("Conteo -> ");

      Serial.print("Rojo: ");
      Serial.print(contadorRojo);

      Serial.print(" | Verde: ");
      Serial.print(contadorVerde);

      Serial.print(" | Azul: ");
      Serial.println(contadorAzul);

      Serial.println();
    }
  }


  // ==================================================
  // 6. ESPERAR 100 ms
  // ==================================================

  delay(100);
}