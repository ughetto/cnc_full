#include <SPI.h>

// =========================
// LS7366R - Chip Select assi
// =========================
#define CS_X 10
#define CS_Y 9
#define CS_Z 8

// =========================
// Driver stepper - pin asse X
// =========================
#define X_STEP_PIN 2
#define X_DIR_PIN  3
#define X_EN_PIN   4

// =========================
// Driver stepper - pin asse Y
// =========================
#define Y_STEP_PIN 5
#define Y_DIR_PIN  6
#define Y_EN_PIN   7

// =========================
// Driver stepper - pin asse Z
// =========================
#define Z_STEP_PIN 14
#define Z_DIR_PIN  15
#define Z_EN_PIN   16

// =========================
// Commutatori semiautomatici ON-OFF-ON
// Comune commutatori a GND, ingressi con INPUT_PULLUP.
// ATTENZIONE: non usare 2-7 per questi ingressi, sono gia STEP/DIR/EN.
// =========================
#define SEMI_X_PLUS_PIN  24
#define SEMI_X_MINUS_PIN 25
#define SEMI_Y_PLUS_PIN  26
#define SEMI_Y_MINUS_PIN 27

// Se il driver si abilita con livello LOW lascia true.
// Se invece si abilita con HIGH metti false.
#define ENABLE_ACTIVE_LOW true

// Velocità massima manuale richiesta
const float MAX_SPEED_XY_SPS = 1600.0f;
const float MAX_SPEED_Z_SPS  = 6400.0f;

// Durata impulso STEP in microsecondi
const uint32_t STEP_PULSE_US = 10;

// =========================
// Comandi LS7366R
// =========================
#define CLR   0x00
#define RD    0x40
#define WR    0x80

// =========================
// Registri LS7366R
// =========================
#define REG_MDR0 0x08
#define REG_MDR1 0x10
#define REG_CNTR 0x20
#define REG_STR  0x30

// Lettura LS7366 conservativa: SPI volutamente più lento per ridurre letture spurie.
SPISettings ls7366_spi(250000, MSBFIRST, SPI_MODE0);

struct AxisMotor {
  uint8_t stepPin;
  uint8_t dirPin;
  uint8_t enPin;

  volatile float speedSps;       // valore attuale step/s
  bool stepState;
  uint32_t lastToggleUs;
  uint32_t nextIntervalUs;
};

AxisMotor motorX = {X_STEP_PIN, X_DIR_PIN, X_EN_PIN, 0.0f, false, 0, 0};
AxisMotor motorY = {Y_STEP_PIN, Y_DIR_PIN, Y_EN_PIN, 0.0f, false, 0, 0};
AxisMotor motorZ = {Z_STEP_PIN, Z_DIR_PIN, Z_EN_PIN, 0.0f, false, 0, 0};

String uartLine;

float jogX_sps = 0.0f;
float jogY_sps = 0.0f;
float jogZ_sps = 0.0f;
float semiX_sps = 800.0f;
float semiY_sps = 800.0f;
float semiZ_sps  = 1600.0f;

int32_t lastGoodX = 0;
int32_t lastGoodY = 0;
int32_t lastGoodZ = 0;
bool haveGoodCounters = false;
const int32_t MAX_REASONABLE_JUMP_COUNTS = 20000;

// =========================
// Utility CS
// =========================
void csLow(int cs) {
  digitalWrite(cs, LOW);
}

void csHigh(int cs) {
  digitalWrite(cs, HIGH);
}

// =========================
// SPI base
// =========================
void write8(int cs, uint8_t reg, uint8_t value) {
  SPI.beginTransaction(ls7366_spi);
  csLow(cs);
  SPI.transfer(WR | reg);
  SPI.transfer(value);
  csHigh(cs);
  SPI.endTransaction();
}

uint8_t read8(int cs, uint8_t reg) {
  SPI.beginTransaction(ls7366_spi);
  csLow(cs);
  SPI.transfer(RD | reg);
  uint8_t v = SPI.transfer(0x00);
  csHigh(cs);
  SPI.endTransaction();
  return v;
}

void clearReg(int cs, uint8_t reg) {
  SPI.beginTransaction(ls7366_spi);
  csLow(cs);
  SPI.transfer(CLR | reg);
  csHigh(cs);
  SPI.endTransaction();
}

int32_t readCNTR32(int cs) {
  SPI.beginTransaction(ls7366_spi);
  csLow(cs);
  SPI.transfer(RD | REG_CNTR);

  uint32_t v = 0;
  v |= ((uint32_t)SPI.transfer(0x00) << 24);
  v |= ((uint32_t)SPI.transfer(0x00) << 16);
  v |= ((uint32_t)SPI.transfer(0x00) << 8);
  v |= ((uint32_t)SPI.transfer(0x00));

  csHigh(cs);
  SPI.endTransaction();

  return (int32_t)v;
}

int32_t acceptCounterRead(int32_t raw, int32_t &lastGood) {
  if (!haveGoodCounters) {
    lastGood = raw;
    return raw;
  }

  if (labs(raw - lastGood) > MAX_REASONABLE_JUMP_COUNTS) {
    return lastGood;
  }

  lastGood = raw;
  return raw;
}

// =========================
// Init asse encoder
// =========================
void initAxisCounter(const char* name, int cs) {
  clearReg(cs, REG_MDR0);
  clearReg(cs, REG_MDR1);
  clearReg(cs, REG_CNTR);
  clearReg(cs, REG_STR);

  delayMicroseconds(20);

  // Quadrature x4
  write8(cs, REG_MDR0, 0x03);

  // Counter 4 byte
  write8(cs, REG_MDR1, 0x00);

  delayMicroseconds(20);

  Serial.print("ASSE ");
  Serial.print(name);
  Serial.print("  MDR0=0x");
  Serial.print(read8(cs, REG_MDR0), HEX);
  Serial.print("  MDR1=0x");
  Serial.print(read8(cs, REG_MDR1), HEX);
  Serial.print("  STR=0x");
  Serial.println(read8(cs, REG_STR), HEX);
}

// =========================
// Utility motori - open collector simulato
// =========================
void ocLow(uint8_t pin) {
  digitalWrite(pin, LOW);
  pinMode(pin, OUTPUT);
}

void ocRelease(uint8_t pin) {
  digitalWrite(pin, LOW);
  pinMode(pin, INPUT);
}

void ocWrite(uint8_t pin, uint8_t value) {
  if (value == LOW) ocLow(pin);
  else ocRelease(pin);
}

void enableMotor(AxisMotor &m, bool enable) {
  if (ENABLE_ACTIVE_LOW) {
    ocWrite(m.enPin, enable ? LOW : HIGH);
  } else {
    ocWrite(m.enPin, enable ? HIGH : LOW);
  }
}

void initMotor(AxisMotor &m) {
  ocRelease(m.stepPin);
  ocRelease(m.dirPin);
  ocRelease(m.enPin);
  enableMotor(m, false);

  m.speedSps = 0.0f;
  m.stepState = false;
  m.lastToggleUs = micros();
  m.nextIntervalUs = 0;
}

void setMotorSpeed(AxisMotor &m, float sps, float maxSpeedSps) {
  if (sps > maxSpeedSps) sps = maxSpeedSps;
  if (sps < -maxSpeedSps) sps = -maxSpeedSps;

  m.speedSps = sps;

  if (sps == 0.0f) {
    ocWrite(m.stepPin, HIGH);
    m.stepState = false;
    m.nextIntervalUs = 0;
    enableMotor(m, false);
    return;
  }

  ocWrite(m.dirPin, (sps >= 0.0f) ? HIGH : LOW);
  enableMotor(m, true);

  float absSps = fabsf(sps);
  m.nextIntervalUs = (uint32_t)(500000.0f / absSps);
  if (m.nextIntervalUs < STEP_PULSE_US) {
    m.nextIntervalUs = STEP_PULSE_US;
  }
}

void updateMotor(AxisMotor &m) {
  if (m.speedSps == 0.0f || m.nextIntervalUs == 0) return;

  uint32_t now = micros();
  if ((uint32_t)(now - m.lastToggleUs) >= m.nextIntervalUs) {
    m.lastToggleUs = now;
    m.stepState = !m.stepState;
    ocWrite(m.stepPin, m.stepState ? LOW : HIGH);
  }
}

// =========================
// Parsing comando UART
// atteso: JOG,X:120.0,Y:-50.0,Z:0.0
// =========================
float extractAxisValue(const String &line, const char axisName) {
  String key = String(axisName) + ":";
  int start = line.indexOf(key);
  if (start < 0) return 0.0f;

  start += key.length();
  int end = line.indexOf(',', start);
  String token = (end < 0) ? line.substring(start) : line.substring(start, end);
  token.trim();
  return token.toFloat();
}

void processCommand(const String &line) {
  if (line.startsWith("JOG")) {
    jogX_sps = extractAxisValue(line, 'X');
    jogY_sps = extractAxisValue(line, 'Y');
    jogZ_sps = extractAxisValue(line, 'Z');

    // Limiti di sicurezza.
    if (jogX_sps > MAX_SPEED_XY_SPS) jogX_sps = MAX_SPEED_XY_SPS;
    if (jogX_sps < -MAX_SPEED_XY_SPS) jogX_sps = -MAX_SPEED_XY_SPS;
    if (jogY_sps > MAX_SPEED_XY_SPS) jogY_sps = MAX_SPEED_XY_SPS;
    if (jogY_sps < -MAX_SPEED_XY_SPS) jogY_sps = -MAX_SPEED_XY_SPS;
    if (jogZ_sps > MAX_SPEED_Z_SPS) jogZ_sps = MAX_SPEED_Z_SPS;
    if (jogZ_sps < -MAX_SPEED_Z_SPS) jogZ_sps = -MAX_SPEED_Z_SPS;
    return;
  }

  if (line.startsWith("SEMI")) {
    // Nuovo formato: SEMI,X:<sps>,Y:<sps>
    float sx = extractAxisValue(line, 'X');
    float sy = extractAxisValue(line, 'Y');

    // Compatibilita con vecchio formato: SEMI,XY:<sps>,Z:<sps>
    int xyStart = line.indexOf("XY:");
    if (xyStart >= 0) {
      xyStart += 3;
      int end = line.indexOf(',', xyStart);
      String token = (end < 0) ? line.substring(xyStart) : line.substring(xyStart, end);
      token.trim();
      sx = token.toFloat();
      sy = sx;
    }

    semiX_sps = sx;
    semiY_sps = sy;

    if (semiX_sps < 0.0f) semiX_sps = -semiX_sps;
    if (semiY_sps < 0.0f) semiY_sps = -semiY_sps;
    if (semiX_sps > MAX_SPEED_XY_SPS) semiX_sps = MAX_SPEED_XY_SPS;
    if (semiY_sps > MAX_SPEED_XY_SPS) semiY_sps = MAX_SPEED_XY_SPS;
    return;
  }
}

void readUartCommands() {
  while (Serial1.available()) {
    char c = (char)Serial1.read();

    if (c == '\r') continue;

    if (c == '\n') {
      if (uartLine.length() > 0) {
        processCommand(uartLine);
        uartLine = "";
      }
    } else {
      uartLine += c;
      if (uartLine.length() > 120) {
        uartLine = "";
      }
    }
  }
}



bool semiActive(uint8_t pin) {
  return digitalRead(pin) == LOW;
}

float chooseSemiAxisSpeed(float jogSpeed, uint8_t plusPin, uint8_t minusPin, float semiSpeed) {
  bool plusActive = semiActive(plusPin);
  bool minusActive = semiActive(minusPin);

  // Se per errore sono attivi entrambi, fermo l'asse.
  if (plusActive && minusActive) return 0.0f;
  if (plusActive) return semiSpeed;
  if (minusActive) return -semiSpeed;
  return jogSpeed;
}

void applyRequestedSpeeds() {
  float sx = chooseSemiAxisSpeed(jogX_sps, SEMI_X_PLUS_PIN, SEMI_X_MINUS_PIN, semiX_sps);
  float sy = chooseSemiAxisSpeed(jogY_sps, SEMI_Y_PLUS_PIN, SEMI_Y_MINUS_PIN, semiY_sps);
  float sz = jogZ_sps;

  setMotorSpeed(motorX, sx, MAX_SPEED_XY_SPS);
  setMotorSpeed(motorY, sy, MAX_SPEED_XY_SPS);
  setMotorSpeed(motorZ, sz, MAX_SPEED_Z_SPS);
}

void setup() {
  Serial.begin(115200);   // USB debug verso PC
  Serial1.begin(115200);  // UART verso Raspberry
  delay(1200);

  pinMode(CS_X, OUTPUT);
  pinMode(CS_Y, OUTPUT);
  pinMode(CS_Z, OUTPUT);

  csHigh(CS_X);
  csHigh(CS_Y);
  csHigh(CS_Z);

  SPI.begin();

  Serial.println("=== INIT 3 ASSI LS7366R + STEPPER v15 ===");

  initAxisCounter("X", CS_X);
  initAxisCounter("Y", CS_Y);
  initAxisCounter("Z", CS_Z);

  initMotor(motorX);
  initMotor(motorY);
  initMotor(motorZ);

  pinMode(SEMI_X_PLUS_PIN, INPUT_PULLUP);
  pinMode(SEMI_X_MINUS_PIN, INPUT_PULLUP);
  pinMode(SEMI_Y_PLUS_PIN, INPUT_PULLUP);
  pinMode(SEMI_Y_MINUS_PIN, INPUT_PULLUP);

  Serial.println("=== PRONTO ===");
}

void loop() {
  static uint32_t lastMs = 0;

  readUartCommands();
  applyRequestedSpeeds();

  updateMotor(motorX);
  updateMotor(motorY);
  updateMotor(motorZ);

  if (millis() - lastMs >= 100) {
    lastMs = millis();

    int32_t rawX = readCNTR32(CS_X);
    int32_t rawY = readCNTR32(CS_Y);
    int32_t rawZ = readCNTR32(CS_Z);

    int32_t x = acceptCounterRead(rawX, lastGoodX);
    int32_t y = acceptCounterRead(rawY, lastGoodY);
    int32_t z = acceptCounterRead(rawZ, lastGoodZ);
    haveGoodCounters = true;

    // debug USB
    Serial.print("X:");
    Serial.print(x);
    Serial.print(" Y:");
    Serial.print(y);
    Serial.print(" Z:");
    Serial.println(z);

    // UART verso Raspberry
    Serial1.print("X:");
    Serial1.print(x);
    Serial1.print(",Y:");
    Serial1.print(y);
    Serial1.print(",Z:");
    Serial1.println(z);
  }
}
