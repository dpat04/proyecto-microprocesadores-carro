// =====================================================================
// Carro con seguimiento de pista - ESP32-S3 (Grupo 6, MT-7003)
// Etapa 1: movimiento controlado desde la Raspberry Pi por I2C
//
// ESP32 = esclavo I2C (direccion 0x42) en Wire1: SDA = GPIO17, SCL = GPIO18
// Raspberry Pi 5 = maestro (pin 3 SDA, pin 5 SCL, pin 6 GND)
//
// PROTOCOLO
//  Pi -> ESP (escritura, 5 bytes): [CMD, a1, a2, a3, CHK]   CHK = CMD^a1^a2^a3
//     0x01 INICIO
//     0x02 FIN
//     0x03 CONFIG  a1 = accion ROJO, a2 = accion VERDE, a3 = accion AZUL
//                  (0 = freno, 1 = retroceso, 2 = cambio de velocidad)
//     0x04 RUTA    a1 = 0 izquierda, 1 derecha
//     0x05 VEL     a1 = 0 baja, 1 alta
//     0x06 MOVER   a1 = direccion (modo manual, solo con el carro en marcha)
//                  0 adelante, 1 atras, 2 curva izq, 3 curva der,
//                  4 giro en su lugar izq, 5 giro en su lugar der
//  ESP -> Pi (lectura, 8 bytes):
//     [estado, error, cuentaR, cuentaG, cuentaB, Vbat(decimas de V), velAlta, CHK]
//     CHK = XOR de los 7 bytes anteriores
// =====================================================================
#include <Wire.h>

// ---------------- Pines ----------------
const int PWMA = 14, AIN1 = 12, AIN2 = 13;   // motor izquierdo (A)
const int PWMB = 41, BIN1 = 39, BIN2 = 40;   // motor derecho (B)
const int STBY = 11;
const int CLP  = 16;                         // switch de choque
const int PIN_VBAT = 2;                      // divisor 100k / 27k
const int I2C_SDA = 17, I2C_SCL = 18;
const uint8_t I2C_DIR = 0x42;

// ---------------- Ajustes ----------------
const bool  INV_IZQ = false;
const bool  INV_DER = true;                  // el derecho se invierte por software
const int   VEL_BAJA = 45;                   // % (ajustar segun la zona muerta)
const int   VEL_ALTA = 90;                   // % (~el doble)
const float FACTOR_VBAT = 127.0f / 27.0f;    // calibrar con el multimetro
const unsigned long TIMEOUT_PI_MS = 1500;    // si la Pi deja de hablar, el carro frena

// ---------------- Protocolo ----------------
enum : uint8_t { CMD_INICIO = 0x01, CMD_FIN = 0x02, CMD_CONFIG = 0x03, CMD_RUTA = 0x04,
                 CMD_VEL = 0x05, CMD_MOVER = 0x06 };
enum : uint8_t { DETENIDO = 0, AVANZANDO = 1, FRENANDO = 2, RETROCEDIENDO = 3 };
enum : uint8_t { ERR_OK = 0, ERR_CONFIG_EN_MOV = 1, ERR_CHECKSUM = 2, ERR_CMD = 3,
                 ERR_CHOQUE = 4, ERR_SIN_PI = 5, ERR_NO_INICIADO = 6 };
enum : uint8_t { DIR_ADELANTE = 0, DIR_ATRAS = 1, DIR_IZQ = 2, DIR_DER = 3,
                 DIR_GIRO_IZQ = 4, DIR_GIRO_DER = 5 };

// ---------------- Datos compartidos con los callbacks I2C ----------------
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
volatile uint8_t rxBuf[5];
volatile uint8_t rxLen = 0;
volatile bool    rxNuevo = false;
volatile unsigned long ultimoContactoPi = 0;
uint8_t txBuf[8] = {0};

// ---------------- Interrupcion del switch CLP ----------------
volatile bool choque = false;
volatile unsigned long ultimoToque = 0;

// ---------------- Estado del carro ----------------
uint8_t estado  = DETENIDO;
uint8_t error   = ERR_OK;
uint8_t accion[3] = {0, 1, 2};   // R, G, B
uint8_t ruta    = 0;             // 0 izq, 1 der
uint8_t velAlta = 0;
uint8_t direccion = DIR_ADELANTE;
uint8_t conteo[3] = {0, 0, 0};   // R, G, B (se llenara con el sensor de color)

// =====================================================================
// INTERRUPCION: flanco del switch de choque (con antirrebote de 300 ms)
void IRAM_ATTR isrChoque() {
  unsigned long t = millis();
  if (t - ultimoToque > 300) {
    choque = true;
    ultimoToque = t;
  }
}

// Callbacks I2C: deben ser CORTOS. Solo copian datos; el loop los procesa.
void onReceive(int len) {
  uint8_t tmp[5] = {0};
  int n = 0;
  while (Wire1.available()) {
    uint8_t b = Wire1.read();
    if (n < 5) tmp[n] = b;
    n++;
  }
  ultimoContactoPi = millis();
  if (n == 0) return;                       // sondeo vacio (por ejemplo i2cdetect)
  portENTER_CRITICAL(&mux);
  for (int i = 0; i < 5; i++) rxBuf[i] = tmp[i];
  rxLen = (n > 255) ? 255 : n;
  rxNuevo = true;
  portEXIT_CRITICAL(&mux);
}

void onRequest() {
  uint8_t tmp[8];
  portENTER_CRITICAL(&mux);
  memcpy(tmp, txBuf, 8);
  portEXIT_CRITICAL(&mux);
  Wire1.write(tmp, 8);
  ultimoContactoPi = millis();
}

// ---------------- Motores ----------------
void motor(int pwmPin, int in1, int in2, int vel) {
  vel = constrain(vel, -100, 100);
  if (vel > 0)      { digitalWrite(in1, HIGH); digitalWrite(in2, LOW);  }
  else if (vel < 0) { digitalWrite(in1, LOW);  digitalWrite(in2, HIGH); }
  else              { digitalWrite(in1, LOW);  digitalWrite(in2, LOW);  }  // rueda libre
  ledcWrite(pwmPin, abs(vel) * 1023 / 100);
}
void izq(int v) { motor(PWMA, AIN1, AIN2, INV_IZQ ? -v : v); }
void der(int v) { motor(PWMB, BIN1, BIN2, INV_DER ? -v : v); }
void frenoActivo() {                         // IN1 = IN2 = HIGH -> freno en corto
  digitalWrite(AIN1, HIGH); digitalWrite(AIN2, HIGH);
  digitalWrite(BIN1, HIGH); digitalWrite(BIN2, HIGH);
}
int velocidadActual() { return velAlta ? VEL_ALTA : VEL_BAJA; }

// Aplica a los motores la direccion y la velocidad actuales
void aplicarMovimiento() {
  int v = velocidadActual();
  switch (direccion) {
    case DIR_ADELANTE: izq(v);  der(v);  break;
    case DIR_ATRAS:    izq(-v); der(-v); break;
    case DIR_IZQ:      izq(0);  der(v);  break;   // curva: la rueda interna se detiene
    case DIR_DER:      izq(v);  der(0);  break;
    case DIR_GIRO_IZQ: izq(-v); der(v);  break;   // gira sobre su propio eje
    case DIR_GIRO_DER: izq(v);  der(-v); break;
  }
  estado = (direccion == DIR_ATRAS) ? RETROCEDIENDO : AVANZANDO;
}

void detener(uint8_t codigoError) {
  frenoActivo();
  estado = DETENIDO;
  error  = codigoError;
}

// ---------------- Procesamiento de comandos ----------------
void procesarComando() {
  uint8_t c[5];
  uint8_t len;
  portENTER_CRITICAL(&mux);
  for (int i = 0; i < 5; i++) c[i] = rxBuf[i];
  len = rxLen;
  rxNuevo = false;
  portEXIT_CRITICAL(&mux);

  if (len != 5 || (uint8_t)(c[0] ^ c[1] ^ c[2] ^ c[3]) != c[4]) {
    error = ERR_CHECKSUM;
    Serial.println("Trama invalida (longitud o checksum)");
    return;
  }

  switch (c[0]) {
    case CMD_INICIO:
      error     = ERR_OK;
      direccion = DIR_ADELANTE;
      aplicarMovimiento();
      Serial.println("INICIO");
      break;

    case CMD_FIN:
      detener(ERR_OK);
      Serial.println("FIN");
      break;

    case CMD_CONFIG:
      if (estado != DETENIDO) {               // regla del enunciado (segunda barrera)
        error = ERR_CONFIG_EN_MOV;
        Serial.println("CONFIG rechazada: el carro esta en movimiento");
        break;
      }
      if (c[1] > 2 || c[2] > 2 || c[3] > 2) { error = ERR_CMD; break; }
      accion[0] = c[1]; accion[1] = c[2]; accion[2] = c[3];
      error = ERR_OK;
      Serial.printf("CONFIG  R=%d  G=%d  B=%d\n", c[1], c[2], c[3]);
      break;

    case CMD_RUTA:
      ruta  = c[1] ? 1 : 0;
      error = ERR_OK;
      Serial.printf("RUTA %s\n", ruta ? "derecha" : "izquierda");
      break;

    case CMD_VEL:
      velAlta = c[1] ? 1 : 0;
      error   = ERR_OK;
      if (estado != DETENIDO) aplicarMovimiento();
      Serial.printf("VELOCIDAD %s\n", velAlta ? "alta" : "baja");
      break;

    case CMD_MOVER: {
      if (c[1] > DIR_GIRO_DER) { error = ERR_CMD; break; }
      if (estado == DETENIDO) {               // el movimiento solo arranca con INICIO
        error = ERR_NO_INICIADO;
        Serial.println("MOVER rechazado: presione INICIO primero");
        break;
      }
      bool ibaAtras  = (direccion == DIR_ATRAS);
      bool vaAtras   = (c[1] == DIR_ATRAS);
      if (ibaAtras != vaAtras) {              // cambio adelante <-> atras: freno breve
        frenoActivo();                        // para no exigir de golpe al driver
        delay(150);
      }
      direccion = c[1];
      error = ERR_OK;
      aplicarMovimiento();
      Serial.printf("MOVER direccion %d\n", direccion);
      break;
    }

    default:
      error = ERR_CMD;
      Serial.printf("Comando desconocido 0x%02X\n", c[0]);
  }
}

// ---------------- Trama de estado para la Pi ----------------
void actualizarTx() {
  uint32_t mv = analogReadMilliVolts(PIN_VBAT);
  long dv = lroundf(mv * FACTOR_VBAT / 100.0f);       // decimas de voltio
  uint8_t vbat = (uint8_t)constrain(dv, 0L, 255L);
  uint8_t t[8] = { estado, error, conteo[0], conteo[1], conteo[2], vbat, velAlta, 0 };
  for (int i = 0; i < 7; i++) t[7] ^= t[i];
  portENTER_CRITICAL(&mux);
  memcpy(txBuf, t, 8);
  portEXIT_CRITICAL(&mux);
}

// =====================================================================
void setup() {
  Serial.begin(115200);

  pinMode(AIN1, OUTPUT); pinMode(AIN2, OUTPUT);
  pinMode(BIN1, OUTPUT); pinMode(BIN2, OUTPUT);
  pinMode(STBY, OUTPUT);
  digitalWrite(STBY, LOW);
  ledcAttach(PWMA, 20000, 10);               // 20 kHz, 10 bits (API core 3.x)
  ledcAttach(PWMB, 20000, 10);
  izq(0); der(0);
  digitalWrite(STBY, HIGH);

  pinMode(CLP, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(CLP), isrChoque, FALLING);

  actualizarTx();
  Wire1.onReceive(onReceive);
  Wire1.onRequest(onRequest);
  Wire1.begin(I2C_DIR, I2C_SDA, I2C_SCL, 100000);   // esclavo I2C

  ultimoContactoPi = millis();
  Serial.println("ESP32 listo: esclavo I2C en 0x42 (SDA 17, SCL 18)");
}

void loop() {
  if (rxNuevo) procesarComando();

  // Choque detectado por interrupcion
  if (choque) {
    choque = false;
    if (estado != DETENIDO) {
      detener(ERR_CHOQUE);
      Serial.println("CHOQUE: carro detenido");
    }
  }

  // Seguridad: si la Pi deja de comunicarse, el carro se detiene
  unsigned long uc = ultimoContactoPi;      // leer ANTES de millis() evita un falso timeout
  if (estado != DETENIDO && millis() - uc > TIMEOUT_PI_MS) {
    detener(ERR_SIN_PI);
    Serial.println("Sin comunicacion con la Pi: carro detenido");
  }

  static unsigned long tTx = 0;
  if (millis() - tTx >= 50) {               // refresca la trama cada 50 ms
    tTx = millis();
    actualizarTx();
  }
  delay(2);
}
