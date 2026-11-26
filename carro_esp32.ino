// =====================================================================
// Carro con seguimiento de pista - ESP32-S3 (Grupo 6, MT-7003)
// Etapa 2: SEGUIMIENTO DE LINEA (control PD) + control desde la Raspberry Pi
//
// ESP32 = esclavo I2C (direccion 0x42) en Wire1: SDA = GPIO17, SCL = GPIO18
// Raspberry Pi 5 = maestro (pin 3 SDA, pin 5 SCL, pin 6 GND)
//
// Usa la calibracion del sensor de linea guardada por prueba_sensores.ino
// (comando l). Si no existe, el carro no arranca en modo seguimiento.
//
// INTERRUPCIONES
//   - Timer de hardware a 100 Hz: marca el instante de cada paso del control
//   - Flanco del switch CLP: deteccion de choque
//   - Callbacks I2C (onReceive / onRequest): comunicacion con la Pi
//
// PROTOCOLO
//  Pi -> ESP (escritura, 5 bytes): [CMD, a1, a2, a3, CHK]   CHK = CMD^a1^a2^a3
//     0x01 INICIO  arranca el seguimiento de linea
//     0x02 FIN
//     0x03 CONFIG  a1 = accion ROJO, a2 = accion VERDE, a3 = accion AZUL
//                  (0 = freno, 1 = retroceso, 2 = cambio de velocidad)
//     0x04 RUTA    a1 = 0 izquierda, 1 derecha (rama a tomar en la bifurcacion)
//     0x05 VEL     a1 = 0 baja, 1 alta
//     0x06 MOVER   a1 = direccion -> pasa a MODO MANUAL (solo con el carro en marcha)
//                  0 adelante, 1 atras, 2 curva izq, 3 curva der, 4 giro izq, 5 giro der
//     0x07 AJUSTE  a1 = Kp, a2 = Kd, a3 = velocidad baja en % (la alta es el doble)
//  ESP -> Pi (lectura, 8 bytes):
//     [estado, error, cuentaR, cuentaG, cuentaB, Vbat(decimas de V), banderas, CHK]
//     banderas: bit7 = velocidad alta, bit6 = modo manual, bit5 = bifurcacion vista,
//               bits 4..0 = sensores sobre la linea (bit k = posicion k, 0 = extremo izquierdo)
//     CHK = XOR de los 7 bytes anteriores
// =====================================================================
#include <Wire.h>
#include <Preferences.h>

// ---------------- Pines ----------------
const int PWMA = 14, AIN1 = 12, AIN2 = 13;   // motor izquierdo (A)
const int PWMB = 41, BIN1 = 39, BIN2 = 40;   // motor derecho (B)
const int STBY = 11;
const int CLP  = 16;                         // switch de choque
const int PIN_VBAT = 2;                      // divisor 100k / 27k
const int PIN_S[5] = {4, 5, 6, 7, 15};       // sensor de linea S1..S5
const int I2C_SDA = 17, I2C_SCL = 18;
const uint8_t I2C_DIR = 0x42;

// ---------------- Ajustes fijos ----------------
const bool  INV_IZQ = false;
const bool  INV_DER = true;                  // el derecho se invierte por software
const float FACTOR_VBAT = 127.0f / 27.0f;    // calibrar con el multimetro
const unsigned long TIMEOUT_PI_MS = 1500;    // si la Pi deja de hablar, el carro frena
const uint32_t PERIODO_CONTROL_US = 10000;   // 10 ms -> control a 100 Hz
const unsigned long TIEMPO_PERDIDA_MS = 800; // sin ver la linea mas que esto -> se detiene
const int   MIN_RUEDA = -40;                 // en curvas cerradas una rueda puede ir hacia atras

// ---------------- Ajustes que se cambian desde la Pi (se guardan) ----------------
uint8_t kp      = 15;    // % de correccion por unidad de error
uint8_t kd      = 20;    // % de correccion por unidad de cambio del error
uint8_t velBaja = 45;    // %; la velocidad alta es el doble (maximo 100)

// ---------------- Protocolo ----------------
enum : uint8_t { CMD_INICIO = 0x01, CMD_FIN = 0x02, CMD_CONFIG = 0x03, CMD_RUTA = 0x04,
                 CMD_VEL = 0x05, CMD_MOVER = 0x06, CMD_AJUSTE = 0x07 };
enum : uint8_t { DETENIDO = 0, AVANZANDO = 1, FRENANDO = 2, RETROCEDIENDO = 3 };
enum : uint8_t { ERR_OK = 0, ERR_CONFIG_EN_MOV = 1, ERR_CHECKSUM = 2, ERR_CMD = 3,
                 ERR_CHOQUE = 4, ERR_SIN_PI = 5, ERR_NO_INICIADO = 6,
                 ERR_SIN_CAL = 7, ERR_LINEA_PERDIDA = 8 };
enum : uint8_t { DIR_ADELANTE = 0, DIR_ATRAS = 1, DIR_IZQ = 2, DIR_DER = 3,
                 DIR_GIRO_IZQ = 4, DIR_GIRO_DER = 5 };
enum : uint8_t { MODO_LINEA = 0, MODO_MANUAL = 1 };

// ---------------- Datos compartidos con los callbacks I2C ----------------
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
volatile uint8_t rxBuf[5];
volatile uint8_t rxLen = 0;
volatile bool    rxNuevo = false;
volatile unsigned long ultimoContactoPi = 0;
uint8_t txBuf[8] = {0};

// ---------------- Interrupciones ----------------
volatile bool choque = false;
volatile unsigned long ultimoToque = 0;
volatile bool tickControl = false;
hw_timer_t* timerControl = nullptr;

// ---------------- Estado del carro ----------------
uint8_t estado    = DETENIDO;
uint8_t error     = ERR_OK;
uint8_t modo      = MODO_LINEA;
uint8_t accion[3] = {0, 1, 2};   // R, G, B
uint8_t ruta      = 0;           // 0 izq, 1 der
uint8_t velAlta   = 0;
uint8_t direccion = DIR_ADELANTE;
uint8_t conteo[3] = {0, 0, 0};   // R, G, B (siguiente etapa: sensor de color)

// ---------------- Sensor de linea ----------------
uint8_t nivelPiso[5] = {0, 0, 0, 0, 0};
uint8_t orden[5]     = {0, 1, 2, 3, 4};      // orden[k] = canal en la posicion k (0 = izquierda)
bool    lineaCal     = false;
uint8_t patron       = 0;                    // bit k = posicion k sobre la linea
bool    bifurcacion  = false;
float   ePrev        = 0;
unsigned long tUltimaLinea = 0;

Preferences prefs;

// =====================================================================
// INTERRUPCIONES
void IRAM_ATTR isrChoque() {                 // flanco del switch CLP (antirrebote 300 ms)
  unsigned long t = millis();
  if (t - ultimoToque > 300) {
    choque = true;
    ultimoToque = t;
  }
}

void IRAM_ATTR isrTimer() {                  // cada 10 ms: toca un paso del control
  tickControl = true;
}

// Callbacks I2C: CORTOS. Solo copian datos; el loop los procesa.
void onReceive(int len) {
  uint8_t tmp[5] = {0};
  int n = 0;
  while (Wire1.available()) {
    uint8_t b = Wire1.read();
    if (n < 5) tmp[n] = b;
    n++;
  }
  ultimoContactoPi = millis();
  if (n == 0) return;                        // sondeo vacio (por ejemplo i2cdetect)
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

// =====================================================================
// Memoria no volatil
bool cargarCalLinea() {
  prefs.begin("linea", true);
  bool ok = prefs.getBool("ok", false);
  if (ok) {
    prefs.getBytes("piso", nivelPiso, 5);
    prefs.getBytes("orden", orden, 5);
  }
  prefs.end();
  return ok;
}

void cargarAjustes() {
  prefs.begin("pid", true);
  kp      = prefs.getUChar("kp", kp);
  kd      = prefs.getUChar("kd", kd);
  velBaja = prefs.getUChar("vb", velBaja);
  prefs.end();
}

void guardarAjustes() {
  prefs.begin("pid", false);
  prefs.putUChar("kp", kp);
  prefs.putUChar("kd", kd);
  prefs.putUChar("vb", velBaja);
  prefs.end();
}

// =====================================================================
// Motores
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
int velocidadActual() { return velAlta ? min(100, 2 * (int)velBaja) : velBaja; }

void detener(uint8_t codigoError) {
  frenoActivo();
  estado = DETENIDO;
  error  = codigoError;
}

// Modo manual: aplica la direccion y la velocidad actuales
void aplicarMovimiento() {
  int v = velocidadActual();
  switch (direccion) {
    case DIR_ADELANTE: izq(v);  der(v);  break;
    case DIR_ATRAS:    izq(-v); der(-v); break;
    case DIR_IZQ:      izq(0);  der(v);  break;
    case DIR_DER:      izq(v);  der(0);  break;
    case DIR_GIRO_IZQ: izq(-v); der(v);  break;
    case DIR_GIRO_DER: izq(v);  der(-v); break;
  }
  estado = (direccion == DIR_ATRAS) ? RETROCEDIENDO : AVANZANDO;
}

// =====================================================================
// SEGUIMIENTO DE LINEA (se ejecuta cada 10 ms, marcado por el timer)
void pasoControl() {
  // 1) Leer los 5 sensores en orden fisico (izquierda -> derecha)
  bool linea[5];
  uint8_t p = 0;
  for (int k = 0; k < 5; k++) {
    int ch = orden[k];
    linea[k] = (digitalRead(PIN_S[ch]) != nivelPiso[ch]);
    if (linea[k]) p |= (1 << k);
  }
  patron = p;

  // 2) Agrupar sensores contiguos: 2 o mas grupos = bifurcacion
  int gIni[3], gFin[3], ng = 0;
  for (int k = 0; k < 5; k++) {
    if (!linea[k]) continue;
    if (k == 0 || !linea[k - 1]) { if (ng < 3) gIni[ng] = k; }
    if (k == 4 || !linea[k + 1]) { if (ng < 3) { gFin[ng] = k; ng++; } }
  }
  bifurcacion = (ng >= 2);

  if (lineaCal == false || estado == DETENIDO || modo != MODO_LINEA) return;

  // 3) Error de posicion: -2 (linea a la izquierda) ... +2 (linea a la derecha)
  float e;
  if (ng == 0) {
    // Sin linea (por ejemplo, un segmento de color que el IR ve como piso):
    // se mantiene el ultimo error durante un tiempo; si no reaparece, se detiene.
    if (millis() - tUltimaLinea > TIEMPO_PERDIDA_MS) {
      detener(ERR_LINEA_PERDIDA);
      Serial.println("LINEA PERDIDA: carro detenido");
      return;
    }
    e = ePrev;
  } else {
    tUltimaLinea = millis();
    int g = 0;
    if (ng >= 2) g = (ruta == 0) ? 0 : ng - 1;   // en la Y: grupo de la izquierda o de la derecha
    int suma = 0, n = 0;
    for (int k = gIni[g]; k <= gFin[g]; k++) { suma += (k - 2); n++; }
    e = (float)suma / n;
  }

  // 4) Control PD
  float de = e - ePrev;
  ePrev = e;
  float corr = kp * e + kd * de;
  int base = velocidadActual();

  // e > 0 -> la linea esta a la derecha -> girar a la derecha -> rueda izquierda mas rapida
  int vi = constrain((int)lroundf(base + corr), MIN_RUEDA, 100);
  int vd = constrain((int)lroundf(base - corr), MIN_RUEDA, 100);
  izq(vi);
  der(vd);
}

// =====================================================================
// Procesamiento de comandos
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
      if (!lineaCal) {
        error = ERR_SIN_CAL;
        Serial.println("INICIO rechazado: no hay calibracion del sensor de linea");
        break;
      }
      modo  = MODO_LINEA;
      error = ERR_OK;
      ePrev = 0;
      tUltimaLinea = millis();
      estado = AVANZANDO;
      Serial.println("INICIO: seguimiento de linea");
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
      if (estado != DETENIDO && modo == MODO_MANUAL) aplicarMovimiento();
      Serial.printf("VELOCIDAD %s\n", velAlta ? "alta" : "baja");
      break;

    case CMD_MOVER: {
      if (c[1] > DIR_GIRO_DER) { error = ERR_CMD; break; }
      if (estado == DETENIDO) {
        error = ERR_NO_INICIADO;
        Serial.println("MOVER rechazado: presione INICIO primero");
        break;
      }
      bool ibaAtras = (modo == MODO_MANUAL && direccion == DIR_ATRAS);
      bool vaAtras  = (c[1] == DIR_ATRAS);
      if (ibaAtras != vaAtras) { frenoActivo(); delay(150); }
      modo      = MODO_MANUAL;
      direccion = c[1];
      error     = ERR_OK;
      aplicarMovimiento();
      Serial.printf("MANUAL direccion %d\n", direccion);
      break;
    }

    case CMD_AJUSTE:
      kp      = c[1];
      kd      = c[2];
      velBaja = constrain(c[3], 15, 60);
      guardarAjustes();
      error = ERR_OK;
      Serial.printf("AJUSTE  Kp=%d  Kd=%d  velBaja=%d%%  (alta=%d%%)\n",
                    kp, kd, velBaja, min(100, 2 * (int)velBaja));
      break;

    default:
      error = ERR_CMD;
      Serial.printf("Comando desconocido 0x%02X\n", c[0]);
  }
}

// =====================================================================
// Trama de estado para la Pi
void actualizarTx() {
  uint32_t mv = analogReadMilliVolts(PIN_VBAT);
  long dv = lroundf(mv * FACTOR_VBAT / 100.0f);       // decimas de voltio
  uint8_t vbat = (uint8_t)constrain(dv, 0L, 255L);
  uint8_t banderas = (velAlta ? 0x80 : 0) | (modo == MODO_MANUAL ? 0x40 : 0) |
                     (bifurcacion ? 0x20 : 0) | (patron & 0x1F);
  uint8_t t[8] = { estado, error, conteo[0], conteo[1], conteo[2], vbat, banderas, 0 };
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

  for (int i = 0; i < 5; i++) pinMode(PIN_S[i], INPUT_PULLUP);
  pinMode(CLP, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(CLP), isrChoque, FALLING);

  lineaCal = cargarCalLinea();
  cargarAjustes();
  if (lineaCal) {
    Serial.print("Calibracion de linea cargada. Orden izq->der: ");
    for (int k = 0; k < 5; k++) Serial.printf("S%d ", orden[k] + 1);
    Serial.println();
  } else {
    Serial.println("AVISO: no hay calibracion de linea. Use prueba_sensores.ino (comando l).");
  }
  Serial.printf("Ajustes: Kp=%d  Kd=%d  velBaja=%d%%\n", kp, kd, velBaja);

  actualizarTx();
  Wire1.onReceive(onReceive);
  Wire1.onRequest(onRequest);
  Wire1.begin(I2C_DIR, I2C_SDA, I2C_SCL, 100000);   // esclavo I2C

  // Timer de hardware: 1 MHz, alarma cada 10 ms con autorecarga (API core 3.x)
  timerControl = timerBegin(1000000);
  timerAttachInterrupt(timerControl, &isrTimer);
  timerAlarm(timerControl, PERIODO_CONTROL_US, true, 0);

  ultimoContactoPi = millis();
  Serial.println("ESP32 listo: esclavo I2C en 0x42 (SDA 17, SCL 18)");
}

void loop() {
  if (rxNuevo) procesarComando();

  if (choque) {                               // marcado por la interrupcion del CLP
    choque = false;
    if (estado != DETENIDO) {
      detener(ERR_CHOQUE);
      Serial.println("CHOQUE: carro detenido");
    }
  }

  unsigned long uc = ultimoContactoPi;        // leer ANTES de millis() evita un falso timeout
  if (estado != DETENIDO && millis() - uc > TIMEOUT_PI_MS) {
    detener(ERR_SIN_PI);
    Serial.println("Sin comunicacion con la Pi: carro detenido");
  }

  if (tickControl) {                          // marcado por el timer cada 10 ms
    tickControl = false;
    pasoControl();
  }

  static unsigned long tTx = 0;
  if (millis() - tTx >= 50) {
    tTx = millis();
    actualizarTx();
  }
}
