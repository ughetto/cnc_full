#include <SPI.h>
#include <IntervalTimer.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

// =========================
// LS7366R - Chip Select assi
// =========================
#define CS_X 10
#define CS_Y 9
#define CS_Z 8

// =========================
// Selezione sorgenti breakout - uscite push-pull 3,3 V
// =========================
constexpr uint8_t ENABLE_X_SELECT_PIN = 32;
constexpr uint8_t ENABLE_Y_SELECT_PIN = 33;
constexpr uint8_t CONTROL_Z_SELECT_PIN = 36;
constexpr uint8_t MACH3_SELECT_PIN = 37;

// NC a GND: LOW libero, HIGH premuto/cavo interrotto. Solo 3,3 V.
// Il finecorsa software NON sostituisce un E-stop cablato.
constexpr uint8_t LIMIT_X_MINUS_PIN = 40;
constexpr uint8_t LIMIT_X_PLUS_PIN  = 41;
constexpr uint8_t LIMIT_Y_MINUS_PIN = 22;
constexpr uint8_t LIMIT_Y_PLUS_PIN  = 23;
constexpr uint8_t LIMIT_Z_TOP_PIN   = 20;
constexpr uint8_t ALARM_OUTPUT_PIN = 21;
const uint8_t limitPins[5] = {LIMIT_X_MINUS_PIN, LIMIT_X_PLUS_PIN,
                             LIMIT_Y_MINUS_PIN, LIMIT_Y_PLUS_PIN, LIMIT_Z_TOP_PIN};
// Bit 0..4, stesso ordine per STATUS corrente e latched.
const char *const limitNames[5] = {"X_MINUS", "X_PLUS", "Y_MINUS", "Y_PLUS", "Z_TOP"};
const char *const limitErrors[5] = {"LIMIT_X_MINUS", "LIMIT_X_PLUS",
                                   "LIMIT_Y_MINUS", "LIMIT_Y_PLUS", "LIMIT_Z_TOP"};
uint8_t limitCurrentMask = 0;
uint8_t limitLatchedMask = 0;
uint8_t limitAutoResetMask = 0;  // Un trip AUTO richiede RESET anche dopo STOP.
volatile uint8_t limitPendingMask = 0;
uint32_t limitReleaseSinceMs[5] = {};
const uint32_t LIMIT_RELEASE_DEBOUNCE_MS = 30;

// =========================
// Driver stepper - pin asse X
// =========================
#define X_STEP_PIN 2
#define X_DIR_PIN  3

// =========================
// Driver stepper - pin asse Y
// =========================
#define Y_STEP_PIN 5
#define Y_DIR_PIN  6

// =========================
// Driver stepper - pin asse Z
// =========================
#define Z_STEP_PIN 14
#define Z_DIR_PIN  15

// =========================
// Commutatori semiautomatici ON-OFF-ON
// Comune commutatori a GND, ingressi con INPUT_PULLUP.
// ATTENZIONE: non usare 2, 3, 5 e 6: sono gia STEP/DIR degli assi X/Y.
// =========================
#define SEMI_X_PLUS_PIN  24
#define SEMI_X_MINUS_PIN 25
#define SEMI_Y_PLUS_PIN  26
#define SEMI_Y_MINUS_PIN 27

// Velocità massima manuale richiesta
const float MAX_SPEED_XY_SPS = 1600.0f;
const float MAX_SPEED_Z_SPS  = 6400.0f;

// Durata impulso STEP in microsecondi
const uint32_t STEP_PULSE_US = 10;
const uint32_t AUTO_DIR_SETUP_US = 20;
const uint32_t AUTO_MIN_STEP_HIGH_US = 10;
const uint32_t AUTO_MAX_TIMER_INTERVAL_US = 10000000;

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

  volatile float speedSps;       // valore attuale step/s
  bool stepState;
  uint32_t lastToggleUs;
  uint32_t nextIntervalUs;
};

AxisMotor motorX = {X_STEP_PIN, X_DIR_PIN, 0.0f, false, 0, 0};
AxisMotor motorY = {Y_STEP_PIN, Y_DIR_PIN, 0.0f, false, 0, 0};
AxisMotor motorZ = {Z_STEP_PIN, Z_DIR_PIN, 0.0f, false, 0, 0};

const size_t UART_LINE_CAPACITY = 192;
char uartLineBuffer[UART_LINE_CAPACITY];
size_t uartLineLength = 0;
bool uartLineOverflow = false;
String legacyUartLine;  // Conservato esclusivamente per JOG/SEMI esistenti.

// =========================
// Protocollo AUTO - emissione STEP sperimentale per collaudo elettrico
// =========================
const uint8_t AUTO_FIFO_CAPACITY = 32;
const uint8_t AUTO_FIFO_LOW_WATERMARK = 8;
const size_t AUTO_JOB_ID_CAPACITY = 17;

enum AutoState {
  AUTO_IDLE,
  AUTO_BUFFERING,
  AUTO_RUNNING,
  AUTO_COMPLETED,
  AUTO_STOPPED,
  AUTO_ERROR
};

struct MotionSegment {
  uint32_t id;
  int32_t targetX;
  int32_t targetY;
  int32_t targetZ;
  float feedMmS;
  uint32_t durationUs;
  bool endProgram;
};

struct AutoInterpolationPlan {
  int32_t targetX;
  int32_t targetY;
  int32_t targetZ;
  int64_t deltaX;
  int64_t deltaY;
  int64_t deltaZ;
  uint32_t absDeltaX;
  uint32_t absDeltaY;
  uint32_t absDeltaZ;
  uint32_t dominantSteps;
  uint32_t ddaAccumulatorX;
  uint32_t ddaAccumulatorY;
  uint32_t ddaAccumulatorZ;
  uint32_t dominantStepIndex;
  float feedMmS;
  float pathLengthMm;
  float dominantStepIntervalUs;
  uint32_t calculatedDurationUs;
  char dominantAxis;
  bool valid;
};

struct AutoParsedFields {
  char job[AUTO_JOB_ID_CAPACITY];
  char reason[25];
  uint32_t totalSegments;
  uint32_t id;
  int32_t x;
  int32_t y;
  int32_t z;
  int32_t currentX;
  int32_t currentY;
  int32_t currentZ;
  float feed;
  float pulsesPerMmX;
  float pulsesPerMmY;
  float pulsesPerMmZ;
  uint32_t durationUs;
  bool endProgram;
  bool hasJob;
  bool hasReason;
  bool hasTotalSegments;
  bool hasId;
  bool hasX;
  bool hasY;
  bool hasZ;
  bool hasCurrentX;
  bool hasCurrentY;
  bool hasCurrentZ;
  bool hasFeed;
  bool hasPulsesPerMmX;
  bool hasPulsesPerMmY;
  bool hasPulsesPerMmZ;
  bool hasDuration;
  bool hasEnd;
};

MotionSegment autoFifo[AUTO_FIFO_CAPACITY];
uint8_t autoFifoHead = 0;
uint8_t autoFifoTail = 0;
uint8_t autoFifoCount = 0;
AutoState autoState = AUTO_IDLE;
char autoJobId[AUTO_JOB_ID_CAPACITY] = "";
uint32_t autoExpectedSegmentId = 1;
uint32_t autoLastAcceptedSegmentId = 0;
uint32_t autoTotalSegments = 0;
bool autoBufferLowReported = false;
MotionSegment autoActiveSegment;
bool autoSegmentActive = false;
uint32_t autoLastCompletedSegmentId = 0;
bool autoCompletedReported = false;
AutoInterpolationPlan autoInterpolationPlan;
int32_t autoCommandedX = 0;
int32_t autoCommandedY = 0;
int32_t autoCommandedZ = 0;
int32_t autoJobStartX = 0;
int32_t autoJobStartY = 0;
int32_t autoJobStartZ = 0;
float autoPulsesPerMmX = 0.0f;
float autoPulsesPerMmY = 0.0f;
float autoPulsesPerMmZ = 0.0f;

IntervalTimer autoStepTimer;
IntervalTimer autoStepReleaseTimer;
volatile bool autoStepTimerRunning = false;
volatile bool autoStepSegmentComplete = false;
volatile bool autoStepReleasePending = false;
volatile bool autoStepReleaseFault = false;
volatile bool autoStepMainTimerNeedsPeriodUpdate = false;
volatile uint8_t autoStepPulseMask = 0;
volatile uint32_t autoIsrAbsDeltaX = 0;
volatile uint32_t autoIsrAbsDeltaY = 0;
volatile uint32_t autoIsrAbsDeltaZ = 0;
volatile uint32_t autoIsrDominantSteps = 0;
volatile uint32_t autoIsrAccumulatorX = 0;
volatile uint32_t autoIsrAccumulatorY = 0;
volatile uint32_t autoIsrAccumulatorZ = 0;
volatile uint32_t autoIsrDominantStepIndex = 0;
volatile uint32_t autoIsrEmittedX = 0;
volatile uint32_t autoIsrEmittedY = 0;
volatile uint32_t autoIsrEmittedZ = 0;
volatile uint32_t autoIsrStepIntervalUs = 0;
volatile uint32_t autoIsrEarliestStepUs = 0;
volatile uint32_t autoIsrLastStepStartUs = 0;

float jogX_sps = 0.0f;
float jogY_sps = 0.0f;
float jogZ_sps = 0.0f;
float semiX_sps = 800.0f;
float semiY_sps = 800.0f;
float semiZ_sps  = 1600.0f;
float selectedManualX_sps = 0.0f;
float selectedManualY_sps = 0.0f;
float selectedManualZ_sps = 0.0f;
bool mach3ControlRequested = false;

void updateControlSourceSelection();
void processMach3Command(const char *line);
void updateAlarmOutput();
void updateLimits();

int32_t lastGoodX = 0;
int32_t lastGoodY = 0;
int32_t lastGoodZ = 0;
bool haveGoodCounters = false;
const int32_t MAX_REASONABLE_JUMP_COUNTS = 20000;

// ISR GPIO: solo flag, nessuna seriale, timer o gestione stato.
void limitXMinusIsr() { limitPendingMask |= 0x01; }
void limitXPlusIsr()  { limitPendingMask |= 0x02; }
void limitYMinusIsr() { limitPendingMask |= 0x04; }
void limitYPlusIsr()  { limitPendingMask |= 0x08; }
void limitZTopIsr()   { limitPendingMask |= 0x10; }

uint8_t readLimitMask() {
  uint8_t mask = 0;
  for (uint8_t i = 0; i < 5; i++) {
    if (digitalRead(limitPins[i]) == HIGH) mask |= (1u << i);
  }
  return mask;
}

const char *firstLimitError(uint8_t mask) {
  for (uint8_t i = 0; i < 5; i++) {
    if (mask & (1u << i)) return limitErrors[i];
  }
  return "LIMIT_UNKNOWN";
}

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
// Utility motori - STEP/DIR push-pull 3,3 V verso le basi NPN
// =========================
// Solo STEP/DIR: polarita GPIO invertita rispetto al precedente open collector.
// HIGH accende l'NPN (driver a GND); LOW lo spegne (driver rilasciato).
void driverSignalAssert(uint8_t pin) {
  digitalWrite(pin, HIGH);
}

void driverSignalRelease(uint8_t pin) {
  digitalWrite(pin, LOW);
}

void initMotor(AxisMotor &m) {
  // Precarica entrambi i latch a LOW prima di abilitare le uscite: NPN spenti.
  driverSignalRelease(m.stepPin);
  driverSignalRelease(m.dirPin);
  pinMode(m.stepPin, OUTPUT);
  pinMode(m.dirPin, OUTPUT);

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
    driverSignalRelease(m.stepPin);
    m.stepState = false;
    m.nextIntervalUs = 0;
    return;
  }

  if (sps >= 0.0f) driverSignalRelease(m.dirPin);
  else driverSignalAssert(m.dirPin);

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
    if (m.stepState) driverSignalAssert(m.stepPin);
    else driverSignalRelease(m.stepPin);
  }
}

// =========================
// Protocollo AUTO: CRC, risposte e FIFO
// =========================
uint16_t autoCrc16Ccitt(const char *text) {
  uint16_t crc = 0xFFFF;
  while (*text != '\0') {
    crc ^= ((uint16_t)(uint8_t)*text++) << 8;
    for (uint8_t bit = 0; bit < 8; bit++) {
      crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
    }
  }
  return crc;
}

bool parseHex16Strict(const char *text, uint16_t &value) {
  if (text == nullptr || strlen(text) != 4) return false;
  uint16_t parsed = 0;
  for (uint8_t i = 0; i < 4; i++) {
    char c = text[i];
    uint8_t nibble;
    if (c >= '0' && c <= '9') nibble = (uint8_t)(c - '0');
    else if (c >= 'A' && c <= 'F') nibble = (uint8_t)(c - 'A' + 10);
    else if (c >= 'a' && c <= 'f') nibble = (uint8_t)(c - 'a' + 10);
    else return false;
    parsed = (uint16_t)((parsed << 4) | nibble);
  }
  value = parsed;
  return true;
}

void sendAutoPayload(const char *payload) {
  uint16_t crc = autoCrc16Ccitt(payload);
  Serial1.print(payload);
  Serial1.print('*');
  if (crc < 0x1000) Serial1.print('0');
  if (crc < 0x0100) Serial1.print('0');
  if (crc < 0x0010) Serial1.print('0');
  Serial1.println(crc, HEX);
}

const char *safeAutoJob(const char *job) {
  return (job != nullptr && job[0] != '\0') ? job : "NONE";
}

const char *autoStateName() {
  switch (autoState) {
    case AUTO_IDLE: return "IDLE";
    case AUTO_BUFFERING: return "BUFFERING";
    case AUTO_RUNNING: return "RUNNING";
    case AUTO_COMPLETED: return "COMPLETED";
    case AUTO_STOPPED: return "STOPPED";
    case AUTO_ERROR: return "ERROR";
  }
  return "ERROR";
}

uint8_t autoFifoFree() {
  return (uint8_t)(AUTO_FIFO_CAPACITY - autoFifoCount);
}

void releaseAutoStepMask(uint8_t mask) {
  if (mask & 0x01) driverSignalRelease(motorX.stepPin);
  if (mask & 0x02) driverSignalRelease(motorY.stepPin);
  if (mask & 0x04) driverSignalRelease(motorZ.stepPin);
}

void stopAutoStepGeneration() {
  noInterrupts();
  autoStepTimerRunning = false;
  autoStepTimer.end();
  autoStepReleaseTimer.end();
  uint8_t pulseMask = autoStepPulseMask;
  autoStepPulseMask = 0;
  autoStepReleasePending = false;
  autoStepSegmentComplete = false;
  autoStepReleaseFault = false;
  autoStepMainTimerNeedsPeriodUpdate = false;
  autoIsrEarliestStepUs = 0;
  autoIsrLastStepStartUs = 0;
  interrupts();
  releaseAutoStepMask(pulseMask);
}

void captureAutoPartialCommandedPosition() {
  if (!autoSegmentActive || !autoInterpolationPlan.valid) return;
  noInterrupts();
  uint32_t emittedX = autoIsrEmittedX;
  uint32_t emittedY = autoIsrEmittedY;
  uint32_t emittedZ = autoIsrEmittedZ;
  interrupts();

  int64_t partialX = autoInterpolationPlan.deltaX < 0 ? -(int64_t)emittedX : emittedX;
  int64_t partialY = autoInterpolationPlan.deltaY < 0 ? -(int64_t)emittedY : emittedY;
  int64_t partialZ = autoInterpolationPlan.deltaZ < 0 ? -(int64_t)emittedZ : emittedZ;
  autoCommandedX = (int32_t)((int64_t)autoCommandedX + partialX);
  autoCommandedY = (int32_t)((int64_t)autoCommandedY + partialY);
  autoCommandedZ = (int32_t)((int64_t)autoCommandedZ + partialZ);
}

void autoStepReleaseTimerIsr() {
  autoStepReleaseTimer.end();
  uint8_t pulseMask = autoStepPulseMask;
  autoStepPulseMask = 0;
  autoStepReleasePending = false;
  releaseAutoStepMask(pulseMask);

  if (autoIsrDominantStepIndex >= autoIsrDominantSteps) {
    autoStepSegmentComplete = true;
  }
}

void autoStepTimerIsr() {
  // Guardia breve prima di ogni impulso: il loop gestisce arresto e diagnostica.
  // La lettura diretta copre anche un IRQ GPIO ancora in attesa di servizio.
  uint8_t limits = readLimitMask();
  if (limits || limitPendingMask) {
    limitPendingMask |= limits;
    return;
  }

  if (!autoStepTimerRunning) {
    autoStepTimer.end();
    return;
  }

  if (autoStepMainTimerNeedsPeriodUpdate) {
    autoStepTimer.update(autoIsrStepIntervalUs);
    autoStepMainTimerNeedsPeriodUpdate = false;
  }

  if (autoStepReleasePending) {
    autoStepReleaseFault = true;
    autoStepTimerRunning = false;
    autoStepTimer.end();
    return;
  }

  uint8_t pulseMask = 0;
  if (autoIsrAccumulatorX >= autoIsrDominantSteps - autoIsrAbsDeltaX) {
    autoIsrAccumulatorX -= autoIsrDominantSteps - autoIsrAbsDeltaX;
    pulseMask |= 0x01;
    autoIsrEmittedX++;
  } else {
    autoIsrAccumulatorX += autoIsrAbsDeltaX;
  }
  if (autoIsrAccumulatorY >= autoIsrDominantSteps - autoIsrAbsDeltaY) {
    autoIsrAccumulatorY -= autoIsrDominantSteps - autoIsrAbsDeltaY;
    pulseMask |= 0x02;
    autoIsrEmittedY++;
  } else {
    autoIsrAccumulatorY += autoIsrAbsDeltaY;
  }
  if (autoIsrAccumulatorZ >= autoIsrDominantSteps - autoIsrAbsDeltaZ) {
    autoIsrAccumulatorZ -= autoIsrDominantSteps - autoIsrAbsDeltaZ;
    pulseMask |= 0x04;
    autoIsrEmittedZ++;
  } else {
    autoIsrAccumulatorZ += autoIsrAbsDeltaZ;
  }

  autoStepPulseMask = pulseMask;
  autoIsrLastStepStartUs = micros();
  if (pulseMask & 0x01) driverSignalAssert(motorX.stepPin);
  if (pulseMask & 0x02) driverSignalAssert(motorY.stepPin);
  if (pulseMask & 0x04) driverSignalAssert(motorZ.stepPin);
  autoIsrDominantStepIndex++;
  autoStepReleasePending = true;

  if (!autoStepReleaseTimer.begin(autoStepReleaseTimerIsr, STEP_PULSE_US)) {
    autoStepReleasePending = false;
    autoStepReleaseFault = true;
    autoStepPulseMask = 0;
    releaseAutoStepMask(pulseMask);
    autoStepTimerRunning = false;
    autoStepTimer.end();
    return;
  }

  if (autoIsrDominantStepIndex >= autoIsrDominantSteps) {
    autoStepTimerRunning = false;
    autoStepTimer.end();
  }
}

void clearAutoFifo() {
  stopAutoStepGeneration();
  autoFifoHead = 0;
  autoFifoTail = 0;
  autoFifoCount = 0;
  autoBufferLowReported = false;
  autoSegmentActive = false;
  memset(&autoActiveSegment, 0, sizeof(autoActiveSegment));
  memset(&autoInterpolationPlan, 0, sizeof(autoInterpolationPlan));
}

bool enqueueAutoSegment(const MotionSegment &segment) {
  if (autoFifoCount >= AUTO_FIFO_CAPACITY) return false;
  autoFifo[autoFifoTail] = segment;
  autoFifoTail = (uint8_t)((autoFifoTail + 1) % AUTO_FIFO_CAPACITY);
  autoFifoCount++;
  if (autoFifoCount > AUTO_FIFO_LOW_WATERMARK) autoBufferLowReported = false;
  return true;
}

bool dequeueAutoSegment(MotionSegment &segment) {
  if (autoFifoCount == 0) return false;
  segment = autoFifo[autoFifoHead];
  autoFifoHead = (uint8_t)((autoFifoHead + 1) % AUTO_FIFO_CAPACITY);
  autoFifoCount--;
  return true;
}

bool autoModeActive() {
  return autoState == AUTO_BUFFERING || autoState == AUTO_RUNNING;
}

void clearManualMotionRequests() {
  jogX_sps = 0.0f;
  jogY_sps = 0.0f;
  jogZ_sps = 0.0f;
  // Le velocità SEMI sono configurazioni: conservarle rende subito riutilizzabili
  // i selettori fisici quando AUTO raggiunge uno stato terminale.
  selectedManualX_sps = 0.0f;
  selectedManualY_sps = 0.0f;
  selectedManualZ_sps = 0.0f;
}

void stopLegacyMotorOutputsForAuto() {
  setMotorSpeed(motorX, 0.0f, MAX_SPEED_XY_SPS);
  setMotorSpeed(motorY, 0.0f, MAX_SPEED_XY_SPS);
  setMotorSpeed(motorZ, 0.0f, MAX_SPEED_Z_SPS);
}

void sendAutoError(const char *job, const char *code, uint32_t segmentId = 0) {
  char payload[150];
  if (segmentId > 0) {
    snprintf(payload, sizeof(payload), "AUTO,ERROR,JOB:%s,CODE:%s,ID:%lu",
             safeAutoJob(job), code, (unsigned long)segmentId);
  } else {
    snprintf(payload, sizeof(payload), "AUTO,ERROR,JOB:%s,CODE:%s",
             safeAutoJob(job), code);
  }
  sendAutoPayload(payload);
}

void enterAutoError(const char *code, uint32_t segmentId = 0) {
  stopAutoStepGeneration();
  captureAutoPartialCommandedPosition();
  clearAutoFifo();
  clearManualMotionRequests();
  stopLegacyMotorOutputsForAuto();
  autoState = AUTO_ERROR;
  updateAlarmOutput();
  // Un errore arresta il moto e restituisce subito la macchina alle sorgenti manuali.
  updateControlSourceSelection();
  sendAutoError(autoJobId, code, segmentId);
}

void updateAlarmOutput() {
  // Push-pull 3,3 V; Arduino: ingresso compatibile/buffer/opto unidirezionale.
  // Non applicare mai 5 V ai GPIO Teensy. Z_TOP monitora soltanto il PAD Z.
  digitalWrite(ALARM_OUTPUT_PIN,
               (limitCurrentMask || limitLatchedMask || autoState == AUTO_ERROR) ? HIGH : LOW);
}

void updateLimits() {
  noInterrupts();
  uint8_t pending = limitPendingMask;
  limitPendingMask = 0;
  uint8_t raw = readLimitMask();
  // Mantiene la guardia STEP fino all'arresto completo fuori ISR.
  if (autoModeActive()) limitPendingMask = pending | raw;
  interrupts();
  uint8_t active = raw | pending;
  uint8_t previous = limitCurrentMask;
  uint32_t now = millis();
  for (uint8_t i = 0; i < 5; i++) {
    uint8_t bit = (1u << i);
    if (active & bit) {
      limitCurrentMask |= bit;
      limitReleaseSinceMs[i] = now;
    } else if ((uint32_t)(now - limitReleaseSinceMs[i]) >= LIMIT_RELEASE_DEBOUNCE_MS) {
      limitCurrentMask &= ~bit;
    }
  }
  limitLatchedMask |= limitCurrentMask;
  if (active && autoModeActive()) {
    limitAutoResetMask |= active;
    enterAutoError(firstLimitError(active), autoSegmentActive ? autoActiveSegment.id : 0);
  }
  updateAlarmOutput();
  uint8_t changed = previous ^ limitCurrentMask;
  for (uint8_t i = 0; i < 5; i++) {
    if (!(changed & (1u << i))) continue;
    Serial1.print("LIMIT,STATE:");
    Serial1.print(limitNames[i]);
    Serial1.print(",ACTIVE:");
    Serial1.println((limitCurrentMask & (1u << i)) ? 1 : 0);
  }
}

void sendAutoAck(const char *command, uint32_t segmentId = 0) {
  char payload[150];
  if (segmentId > 0) {
    snprintf(payload, sizeof(payload), "AUTO,ACK,JOB:%s,CMD:%s,ID:%lu,FREE:%u",
             safeAutoJob(autoJobId), command, (unsigned long)segmentId, autoFifoFree());
  } else {
    snprintf(payload, sizeof(payload), "AUTO,ACK,JOB:%s,CMD:%s,FREE:%u",
             safeAutoJob(autoJobId), command, autoFifoFree());
  }
  sendAutoPayload(payload);
}

void sendAutoStatus() {
  noInterrupts();
  uint32_t emittedX = autoIsrEmittedX;
  uint32_t emittedY = autoIsrEmittedY;
  uint32_t emittedZ = autoIsrEmittedZ;
  uint32_t isrStepIndex = autoIsrDominantStepIndex;
  bool timerRunning = autoStepTimerRunning || autoStepReleasePending;
  interrupts();

  char payload[640];
  snprintf(payload, sizeof(payload),
           "AUTO,STATUS,JOB:%s,STATE:%s,Q:%u,FREE:%u,LAST:%lu,EXEC:%u,"
           "ACTIVE:%lu,LAST_DONE:%lu,LOGICAL_RUN:0,STEP_RUN:%u,"
           "AUTO_ACTIVE:%u,MANUAL_BLOCKED:%u,"
           "CMDX:%ld,CMDY:%ld,CMDZ:%ld,PLAN:%u,DOM:%c,DOM_STEPS:%lu,"
           "STEP_US:%.3f,CALC_US:%lu,TEST_T_US:%lu,ISR_STEP:%lu,"
           "EMITX:%lu,EMITY:%lu,EMITZ:%lu,LIMIT_CURRENT:%u,LIMIT_LATCHED:%u",
           safeAutoJob(autoJobId), autoStateName(), autoFifoCount, autoFifoFree(),
           (unsigned long)autoLastAcceptedSegmentId,
           autoSegmentActive ? 1 : 0,
           (unsigned long)(autoSegmentActive ? autoActiveSegment.id : 0),
           (unsigned long)autoLastCompletedSegmentId,
           timerRunning ? 1 : 0,
           autoModeActive() ? 1 : 0, autoModeActive() ? 1 : 0,
           (long)autoCommandedX, (long)autoCommandedY, (long)autoCommandedZ,
           autoInterpolationPlan.valid ? 1 : 0,
           autoInterpolationPlan.valid ? autoInterpolationPlan.dominantAxis : '-',
           (unsigned long)autoInterpolationPlan.dominantSteps,
           autoInterpolationPlan.dominantStepIntervalUs,
           (unsigned long)autoInterpolationPlan.calculatedDurationUs,
           (unsigned long)(autoSegmentActive ? autoActiveSegment.durationUs : 0),
           (unsigned long)isrStepIndex, (unsigned long)emittedX,
           (unsigned long)emittedY, (unsigned long)emittedZ,
           (unsigned int)limitCurrentMask, (unsigned int)limitLatchedMask);
  sendAutoPayload(payload);
}

void sendAutoBufferLowIfNeeded() {
  if (autoState != AUTO_RUNNING || autoFifoCount > AUTO_FIFO_LOW_WATERMARK ||
      autoBufferLowReported) return;

  char payload[150];
  snprintf(payload, sizeof(payload),
           "AUTO,BUFFER_LOW,JOB:%s,Q:%u,FREE:%u,LAST:%lu",
           safeAutoJob(autoJobId), autoFifoCount, autoFifoFree(),
           (unsigned long)autoLastAcceptedSegmentId);
  sendAutoPayload(payload);
  autoBufferLowReported = true;
}

void sendAutoCompleted() {
  if (autoCompletedReported) return;
  char payload[120];
  snprintf(payload, sizeof(payload), "AUTO,COMPLETED,JOB:%s,LAST:%lu",
           safeAutoJob(autoJobId), (unsigned long)autoLastCompletedSegmentId);
  sendAutoPayload(payload);
  autoCompletedReported = true;
}

uint32_t autoDeltaMagnitude(int64_t delta) {
  return (uint32_t)(delta < 0 ? -delta : delta);
}

bool prepareAutoInterpolation(const MotionSegment &segment) {
  memset(&autoInterpolationPlan, 0, sizeof(autoInterpolationPlan));
  autoInterpolationPlan.targetX = segment.targetX;
  autoInterpolationPlan.targetY = segment.targetY;
  autoInterpolationPlan.targetZ = segment.targetZ;
  autoInterpolationPlan.deltaX = (int64_t)segment.targetX - autoCommandedX;
  autoInterpolationPlan.deltaY = (int64_t)segment.targetY - autoCommandedY;
  autoInterpolationPlan.deltaZ = (int64_t)segment.targetZ - autoCommandedZ;
  autoInterpolationPlan.absDeltaX = autoDeltaMagnitude(autoInterpolationPlan.deltaX);
  autoInterpolationPlan.absDeltaY = autoDeltaMagnitude(autoInterpolationPlan.deltaY);
  autoInterpolationPlan.absDeltaZ = autoDeltaMagnitude(autoInterpolationPlan.deltaZ);
  autoInterpolationPlan.feedMmS = segment.feedMmS;

  autoInterpolationPlan.dominantSteps = autoInterpolationPlan.absDeltaX;
  autoInterpolationPlan.dominantAxis = 'X';
  if (autoInterpolationPlan.absDeltaY > autoInterpolationPlan.dominantSteps) {
    autoInterpolationPlan.dominantSteps = autoInterpolationPlan.absDeltaY;
    autoInterpolationPlan.dominantAxis = 'Y';
  }
  if (autoInterpolationPlan.absDeltaZ > autoInterpolationPlan.dominantSteps) {
    autoInterpolationPlan.dominantSteps = autoInterpolationPlan.absDeltaZ;
    autoInterpolationPlan.dominantAxis = 'Z';
  }

  if (autoInterpolationPlan.deltaX != 0) {
    if (autoInterpolationPlan.deltaX > 0) driverSignalRelease(motorX.dirPin);
    else driverSignalAssert(motorX.dirPin);
  }
  if (autoInterpolationPlan.deltaY != 0) {
    if (autoInterpolationPlan.deltaY > 0) driverSignalRelease(motorY.dirPin);
    else driverSignalAssert(motorY.dirPin);
  }
  if (autoInterpolationPlan.deltaZ != 0) {
    if (autoInterpolationPlan.deltaZ > 0) driverSignalRelease(motorZ.dirPin);
    else driverSignalAssert(motorZ.dirPin);
  }

  if (autoInterpolationPlan.dominantSteps == 0) {
    autoInterpolationPlan.dominantAxis = '-';
    autoInterpolationPlan.valid = true;
    return true;
  }
  if (autoPulsesPerMmX <= 0.0f || autoPulsesPerMmY <= 0.0f ||
      autoPulsesPerMmZ <= 0.0f || segment.feedMmS <= 0.0f) {
    return false;
  }

  double dxMm = (double)autoInterpolationPlan.absDeltaX / autoPulsesPerMmX;
  double dyMm = (double)autoInterpolationPlan.absDeltaY / autoPulsesPerMmY;
  double dzMm = (double)autoInterpolationPlan.absDeltaZ / autoPulsesPerMmZ;
  double pathLengthMm = sqrt(dxMm * dxMm + dyMm * dyMm + dzMm * dzMm);
  double calculatedDurationUs = (pathLengthMm / segment.feedMmS) * 1000000.0;
  double dominantStepIntervalUs = calculatedDurationUs / autoInterpolationPlan.dominantSteps;
  double durationSeconds = calculatedDurationUs / 1000000.0;
  if (!isfinite(pathLengthMm) || pathLengthMm <= 0.0 ||
      !isfinite(calculatedDurationUs) || calculatedDurationUs < 1.0 ||
      calculatedDurationUs > UINT32_MAX || !isfinite(dominantStepIntervalUs) ||
      dominantStepIntervalUs <= 0.0 ||
      ((double)autoInterpolationPlan.absDeltaX / durationSeconds) > MAX_SPEED_XY_SPS ||
      ((double)autoInterpolationPlan.absDeltaY / durationSeconds) > MAX_SPEED_XY_SPS ||
      ((double)autoInterpolationPlan.absDeltaZ / durationSeconds) > MAX_SPEED_Z_SPS) {
    return false;
  }

  uint32_t roundedStepIntervalUs = (uint32_t)(dominantStepIntervalUs + 0.5);
  if (roundedStepIntervalUs < STEP_PULSE_US + AUTO_MIN_STEP_HIGH_US ||
      roundedStepIntervalUs > AUTO_MAX_TIMER_INTERVAL_US) return false;

  autoInterpolationPlan.pathLengthMm = (float)pathLengthMm;
  autoInterpolationPlan.calculatedDurationUs = (uint32_t)(calculatedDurationUs + 0.5);
  autoInterpolationPlan.dominantStepIntervalUs = (float)dominantStepIntervalUs;
  autoInterpolationPlan.valid = true;
  return true;
}

bool startNextAutoStepSegment() {
  updateLimits();
  if (autoState != AUTO_RUNNING || autoSegmentActive) return false;
  if (!dequeueAutoSegment(autoActiveSegment)) {
    sendAutoBufferLowIfNeeded();
    return false;
  }
  if (!prepareAutoInterpolation(autoActiveSegment)) {
    enterAutoError("BAD_PLAN", autoActiveSegment.id);
    return false;
  }
  autoSegmentActive = true;
  sendAutoBufferLowIfNeeded();

  noInterrupts();
  autoIsrAbsDeltaX = autoInterpolationPlan.absDeltaX;
  autoIsrAbsDeltaY = autoInterpolationPlan.absDeltaY;
  autoIsrAbsDeltaZ = autoInterpolationPlan.absDeltaZ;
  autoIsrDominantSteps = autoInterpolationPlan.dominantSteps;
  autoIsrAccumulatorX = 0;
  autoIsrAccumulatorY = 0;
  autoIsrAccumulatorZ = 0;
  autoIsrDominantStepIndex = 0;
  autoIsrEmittedX = 0;
  autoIsrEmittedY = 0;
  autoIsrEmittedZ = 0;
  autoStepPulseMask = 0;
  autoStepSegmentComplete = autoInterpolationPlan.dominantSteps == 0;
  autoStepReleasePending = false;
  autoStepReleaseFault = false;
  autoStepMainTimerNeedsPeriodUpdate = false;
  autoIsrStepIntervalUs =
      (uint32_t)(autoInterpolationPlan.dominantStepIntervalUs + 0.5f);
  interrupts();

  if (autoInterpolationPlan.dominantSteps == 0) return true;

  uint32_t nowUs = micros();
  uint32_t firstEventDelayUs = autoIsrStepIntervalUs;
  noInterrupts();
  uint32_t previousStepStartUs = autoIsrLastStepStartUs;
  interrupts();
  if (previousStepStartUs != 0) {
    uint32_t nextStepDueUs = previousStepStartUs + autoIsrStepIntervalUs;
    int32_t remainingUs = (int32_t)(nextStepDueUs - nowUs);
    firstEventDelayUs = remainingUs > 0 ? (uint32_t)remainingUs : 1;
  }
  // prepareAutoInterpolation() può avere appena aggiornato DIR: la continuità
  // non deve mai ridurre il tempo minimo di setup della direzione.
  if (firstEventDelayUs < AUTO_DIR_SETUP_US) firstEventDelayUs = AUTO_DIR_SETUP_US;
  noInterrupts();
  autoIsrEarliestStepUs = nowUs + firstEventDelayUs;
  autoStepTimerRunning = true;
  autoStepMainTimerNeedsPeriodUpdate = firstEventDelayUs != autoIsrStepIntervalUs;
  interrupts();
  if (!autoStepTimer.begin(autoStepTimerIsr, firstEventDelayUs)) {
    autoStepTimerRunning = false;
    autoIsrEarliestStepUs = 0;
    enterAutoError("TIMER_START", autoActiveSegment.id);
    return false;
  }
  autoStepTimer.priority(64);
  return true;
}

void updateAutoStepExecution() {
  updateLimits();
  if (autoState != AUTO_RUNNING) return;
  if (!autoSegmentActive) {
    startNextAutoStepSegment();
    return;
  }

  noInterrupts();
  bool releaseFault = autoStepReleaseFault;
  bool segmentComplete = autoStepSegmentComplete;
  uint32_t emittedX = autoIsrEmittedX;
  uint32_t emittedY = autoIsrEmittedY;
  uint32_t emittedZ = autoIsrEmittedZ;
  uint32_t dominantStepIndex = autoIsrDominantStepIndex;
  if (releaseFault) autoStepReleaseFault = false;
  if (segmentComplete) autoStepSegmentComplete = false;
  interrupts();
  if (releaseFault) {
    enterAutoError("PULSE_RELEASE", autoActiveSegment.id);
    return;
  }
  if (!segmentComplete) return;

  if (emittedX != autoInterpolationPlan.absDeltaX ||
      emittedY != autoInterpolationPlan.absDeltaY ||
      emittedZ != autoInterpolationPlan.absDeltaZ ||
      dominantStepIndex != autoInterpolationPlan.dominantSteps) {
    enterAutoError("STEP_COUNT", autoActiveSegment.id);
    return;
  }

  autoInterpolationPlan.dominantStepIndex = dominantStepIndex;
  autoLastCompletedSegmentId = autoActiveSegment.id;
  autoCommandedX = autoActiveSegment.targetX;
  autoCommandedY = autoActiveSegment.targetY;
  autoCommandedZ = autoActiveSegment.targetZ;
  bool endProgram = autoActiveSegment.endProgram;
  autoSegmentActive = false;

  if (endProgram) {
    autoState = AUTO_COMPLETED;
    updateAlarmOutput();
    // Il controllo AUTO viene rilasciato soltanto dopo l'ultimo STEP effettivo.
    updateControlSourceSelection();
    sendAutoCompleted();
    return;
  }

  startNextAutoStepSegment();
}

bool parseUint32Strict(const char *text, uint32_t &value) {
  if (text == nullptr || text[0] == '\0' || text[0] == '-') return false;
  errno = 0;
  char *end = nullptr;
  unsigned long parsed = strtoul(text, &end, 10);
  if (errno != 0 || end == text || *end != '\0') return false;
  value = (uint32_t)parsed;
  return true;
}

bool parseInt32Strict(const char *text, int32_t &value) {
  if (text == nullptr || text[0] == '\0') return false;
  errno = 0;
  char *end = nullptr;
  long parsed = strtol(text, &end, 10);
  if (errno != 0 || end == text || *end != '\0' || parsed < INT32_MIN || parsed > INT32_MAX) {
    return false;
  }
  value = (int32_t)parsed;
  return true;
}

bool parsePositiveFloatStrict(const char *text, float &value) {
  if (text == nullptr || text[0] == '\0') return false;
  errno = 0;
  char *end = nullptr;
  float parsed = strtof(text, &end);
  if (errno != 0 || end == text || *end != '\0' || !isfinite(parsed) || parsed <= 0.0f) {
    return false;
  }
  value = parsed;
  return true;
}

bool copyAutoToken(char *destination, size_t capacity, const char *source) {
  size_t length = strlen(source);
  if (length == 0 || length >= capacity) return false;
  for (size_t i = 0; i < length; i++) {
    char c = source[i];
    if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
          (c >= '0' && c <= '9') || c == '_' || c == '-')) return false;
  }
  memcpy(destination, source, length + 1);
  return true;
}

void initAutoParsedFields(AutoParsedFields &fields) {
  memset(&fields, 0, sizeof(fields));
}

bool parseAutoField(char *token, AutoParsedFields &fields) {
  char *separator = strchr(token, ':');
  if (separator == nullptr || separator == token || separator[1] == '\0') return false;
  *separator = '\0';
  const char *key = token;
  const char *value = separator + 1;

  if (strcmp(key, "JOB") == 0) {
    if (fields.hasJob || !copyAutoToken(fields.job, sizeof(fields.job), value)) return false;
    fields.hasJob = true;
    return true;
  }
  if (strcmp(key, "REASON") == 0) {
    if (fields.hasReason || !copyAutoToken(fields.reason, sizeof(fields.reason), value)) return false;
    fields.hasReason = true;
    return true;
  }
  if (strcmp(key, "N") == 0) {
    if (fields.hasTotalSegments || !parseUint32Strict(value, fields.totalSegments)) return false;
    fields.hasTotalSegments = true;
    return true;
  }
  if (strcmp(key, "ID") == 0) {
    if (fields.hasId || !parseUint32Strict(value, fields.id)) return false;
    fields.hasId = true;
    return true;
  }
  if (strcmp(key, "X") == 0) {
    if (fields.hasX || !parseInt32Strict(value, fields.x)) return false;
    fields.hasX = true;
    return true;
  }
  if (strcmp(key, "Y") == 0) {
    if (fields.hasY || !parseInt32Strict(value, fields.y)) return false;
    fields.hasY = true;
    return true;
  }
  if (strcmp(key, "Z") == 0) {
    if (fields.hasZ || !parseInt32Strict(value, fields.z)) return false;
    fields.hasZ = true;
    return true;
  }
  if (strcmp(key, "CX") == 0) {
    if (fields.hasCurrentX || !parseInt32Strict(value, fields.currentX)) return false;
    fields.hasCurrentX = true;
    return true;
  }
  if (strcmp(key, "CY") == 0) {
    if (fields.hasCurrentY || !parseInt32Strict(value, fields.currentY)) return false;
    fields.hasCurrentY = true;
    return true;
  }
  if (strcmp(key, "CZ") == 0) {
    if (fields.hasCurrentZ || !parseInt32Strict(value, fields.currentZ)) return false;
    fields.hasCurrentZ = true;
    return true;
  }
  if (strcmp(key, "F") == 0) {
    if (fields.hasFeed || !parsePositiveFloatStrict(value, fields.feed)) return false;
    fields.hasFeed = true;
    return true;
  }
  if (strcmp(key, "PX") == 0) {
    if (fields.hasPulsesPerMmX || !parsePositiveFloatStrict(value, fields.pulsesPerMmX)) {
      return false;
    }
    fields.hasPulsesPerMmX = true;
    return true;
  }
  if (strcmp(key, "PY") == 0) {
    if (fields.hasPulsesPerMmY || !parsePositiveFloatStrict(value, fields.pulsesPerMmY)) {
      return false;
    }
    fields.hasPulsesPerMmY = true;
    return true;
  }
  if (strcmp(key, "PZ") == 0) {
    if (fields.hasPulsesPerMmZ || !parsePositiveFloatStrict(value, fields.pulsesPerMmZ)) {
      return false;
    }
    fields.hasPulsesPerMmZ = true;
    return true;
  }
  if (strcmp(key, "T") == 0) {
    if (fields.hasDuration || !parseUint32Strict(value, fields.durationUs) ||
        fields.durationUs == 0) return false;
    fields.hasDuration = true;
    return true;
  }
  if (strcmp(key, "END") == 0) {
    if (fields.hasEnd || (strcmp(value, "0") != 0 && strcmp(value, "1") != 0)) return false;
    fields.endProgram = strcmp(value, "1") == 0;
    fields.hasEnd = true;
    return true;
  }
  return false;
}

bool autoJobMatches(const char *job) {
  return job != nullptr && autoJobId[0] != '\0' && strcmp(job, autoJobId) == 0;
}

void processAutoCommand(const char *command, const AutoParsedFields &fields) {
  updateLimits();
  if (mach3ControlRequested &&
      (strcmp(command, "BEGIN") == 0 || strcmp(command, "RUN") == 0)) {
    sendAutoError(fields.hasJob ? fields.job : "NONE", "BUSY");
    return;
  }
  if (strcmp(command, "BEGIN") == 0 || strcmp(command, "RUN") == 0) {
    uint8_t blocked = limitCurrentMask | limitAutoResetMask;
    if (blocked) {
      sendAutoError(fields.hasJob ? fields.job : "NONE", firstLimitError(blocked));
      return;
    }
  }
  if (strcmp(command, "BEGIN") == 0) {
    if (!fields.hasJob || !fields.hasTotalSegments || fields.totalSegments == 0 ||
        !fields.hasCurrentX || !fields.hasCurrentY || !fields.hasCurrentZ ||
        !fields.hasPulsesPerMmX || !fields.hasPulsesPerMmY || !fields.hasPulsesPerMmZ) {
      sendAutoError(fields.hasJob ? fields.job : "NONE", "BAD_BEGIN");
      return;
    }
    if ((autoState == AUTO_BUFFERING || autoState == AUTO_RUNNING) &&
        !autoJobMatches(fields.job)) {
      sendAutoError(fields.job, "BUSY");
      return;
    }
    if (autoJobMatches(fields.job) &&
        (autoState == AUTO_BUFFERING || autoState == AUTO_RUNNING)) {
      if (fields.totalSegments != autoTotalSegments ||
          fields.currentX != autoJobStartX || fields.currentY != autoJobStartY ||
          fields.currentZ != autoJobStartZ ||
          fields.pulsesPerMmX != autoPulsesPerMmX ||
          fields.pulsesPerMmY != autoPulsesPerMmY ||
          fields.pulsesPerMmZ != autoPulsesPerMmZ) {
        sendAutoError(fields.job, "BAD_BEGIN");
        return;
      }
      clearManualMotionRequests();
      sendAutoAck("BEGIN");
      return;
    }

    clearAutoFifo();
    clearManualMotionRequests();
    stopLegacyMotorOutputsForAuto();
    strncpy(autoJobId, fields.job, sizeof(autoJobId) - 1);
    autoJobId[sizeof(autoJobId) - 1] = '\0';
    autoTotalSegments = fields.totalSegments;
    autoExpectedSegmentId = 1;
    autoLastAcceptedSegmentId = 0;
    autoLastCompletedSegmentId = 0;
    autoCompletedReported = false;
    autoCommandedX = fields.currentX;
    autoCommandedY = fields.currentY;
    autoCommandedZ = fields.currentZ;
    autoJobStartX = fields.currentX;
    autoJobStartY = fields.currentY;
    autoJobStartZ = fields.currentZ;
    autoPulsesPerMmX = fields.pulsesPerMmX;
    autoPulsesPerMmY = fields.pulsesPerMmY;
    autoPulsesPerMmZ = fields.pulsesPerMmZ;
    autoState = AUTO_BUFFERING;
    updateAlarmOutput();
    // Da BEGIN AUTO possiede tutte le sorgenti di movimento.
    updateControlSourceSelection();
    sendAutoAck("BEGIN");
    return;
  }

  if (strcmp(command, "RESET") == 0) {
    if (!fields.hasJob) {
      sendAutoError("NONE", "BAD_RESET");
      return;
    }
    if (autoJobId[0] != '\0' && !autoJobMatches(fields.job)) {
      sendAutoError(fields.job, "JOB_MISMATCH");
      return;
    }
    if (autoState != AUTO_COMPLETED && autoState != AUTO_STOPPED &&
        autoState != AUTO_ERROR && autoState != AUTO_IDLE) {
      sendAutoError(safeAutoJob(autoJobId), "BAD_STATE");
      return;
    }
    if (autoJobId[0] == '\0') {
      strncpy(autoJobId, fields.job, sizeof(autoJobId) - 1);
      autoJobId[sizeof(autoJobId) - 1] = '\0';
    }
    clearAutoFifo();
    clearManualMotionRequests();
    stopLegacyMotorOutputsForAuto();
    // RESET cancella la memoria; un contatto ancora aperto resta in allarme.
    limitLatchedMask = limitCurrentMask;
    limitAutoResetMask = 0;
    autoState = AUTO_IDLE;
    updateAlarmOutput();
    updateControlSourceSelection();
    autoExpectedSegmentId = 1;
    autoLastAcceptedSegmentId = 0;
    autoLastCompletedSegmentId = 0;
    autoTotalSegments = 0;
    autoCompletedReported = false;
    autoCommandedX = 0;
    autoCommandedY = 0;
    autoCommandedZ = 0;
    autoJobStartX = 0;
    autoJobStartY = 0;
    autoJobStartZ = 0;
    autoPulsesPerMmX = 0.0f;
    autoPulsesPerMmY = 0.0f;
    autoPulsesPerMmZ = 0.0f;
    sendAutoAck("RESET");
    autoJobId[0] = '\0';
    return;
  }

  if (strcmp(command, "STATUS") == 0) {
    if (autoJobId[0] != '\0' && (!fields.hasJob || !autoJobMatches(fields.job))) {
      sendAutoError(fields.hasJob ? fields.job : "NONE", "JOB_MISMATCH");
      return;
    }
    sendAutoStatus();
    return;
  }

  if (!fields.hasJob || !autoJobMatches(fields.job)) {
    sendAutoError(fields.hasJob ? fields.job : "NONE", "JOB_MISMATCH");
    return;
  }

  if (strcmp(command, "MOVE") == 0) {
    if (autoState != AUTO_BUFFERING && autoState != AUTO_RUNNING) {
      sendAutoError(autoJobId, "BAD_STATE", fields.hasId ? fields.id : 0);
      return;
    }
    if (!(fields.hasId && fields.hasX && fields.hasY && fields.hasZ && fields.hasFeed &&
          fields.hasDuration && fields.hasEnd)) {
      enterAutoError("BAD_MOVE", fields.hasId ? fields.id : 0);
      return;
    }
    if (fields.id == 0) {
      enterAutoError("BAD_ID");
      return;
    }
    if (fields.id < autoExpectedSegmentId) {
      sendAutoAck("MOVE", fields.id);
      return;
    }
    if (fields.id != autoExpectedSegmentId || fields.id > autoTotalSegments) {
      enterAutoError("BAD_ID", fields.id);
      return;
    }
    bool shouldBeEnd = fields.id == autoTotalSegments;
    if (fields.endProgram != shouldBeEnd) {
      enterAutoError("BAD_END", fields.id);
      return;
    }

    MotionSegment segment = {
      fields.id, fields.x, fields.y, fields.z, fields.feed, fields.durationUs, fields.endProgram
    };
    if (!enqueueAutoSegment(segment)) {
      enterAutoError("FIFO_FULL", fields.id);
      return;
    }
    autoLastAcceptedSegmentId = fields.id;
    autoExpectedSegmentId = fields.id + 1;
    sendAutoAck("MOVE", fields.id);
    return;
  }

  if (strcmp(command, "RUN") == 0) {
    if (autoState == AUTO_RUNNING) {
      clearManualMotionRequests();
      sendAutoAck("RUN");
      return;
    }
    if (autoState != AUTO_BUFFERING || autoFifoCount == 0) {
      sendAutoError(autoJobId, "BAD_STATE");
      return;
    }
    clearManualMotionRequests();
    stopLegacyMotorOutputsForAuto();
    autoState = AUTO_RUNNING;
    updateAlarmOutput();
    updateControlSourceSelection();
    sendAutoAck("RUN");
    startNextAutoStepSegment();
    return;
  }

  if (strcmp(command, "STOP") == 0) {
    uint32_t lastCompleted = autoLastCompletedSegmentId;
    stopAutoStepGeneration();
    captureAutoPartialCommandedPosition();
    clearAutoFifo();
    clearManualMotionRequests();
    stopLegacyMotorOutputsForAuto();
    autoState = AUTO_STOPPED;
    updateAlarmOutput();
    // STOP rilascia la breakout solo dopo avere fermato e rilasciato gli STEP.
    updateControlSourceSelection();
    sendAutoAck("STOP");
    char payload[150];
    snprintf(payload, sizeof(payload), "AUTO,STOPPED,JOB:%s,LAST:%lu,REASON:%s",
             safeAutoJob(autoJobId), (unsigned long)lastCompleted,
             fields.hasReason ? fields.reason : "USER");
    sendAutoPayload(payload);
    return;
  }

  if (strcmp(command, "PING") == 0) {
    sendAutoAck("PING");
    return;
  }

  sendAutoError(autoJobId, "UNKNOWN_CMD");
}

void processAutoLine(char *line) {
  char *checksumSeparator = strrchr(line, '*');
  if (checksumSeparator == nullptr || strchr(checksumSeparator + 1, '*') != nullptr) {
    enterAutoError("BAD_CRC");
    return;
  }

  uint16_t receivedCrc;
  if (!parseHex16Strict(checksumSeparator + 1, receivedCrc)) {
    enterAutoError("BAD_CRC");
    return;
  }
  *checksumSeparator = '\0';
  if (autoCrc16Ccitt(line) != receivedCrc) {
    enterAutoError("BAD_CRC");
    return;
  }

  char *savePointer = nullptr;
  char *prefix = strtok_r(line, ",", &savePointer);
  char *command = strtok_r(nullptr, ",", &savePointer);
  if (prefix == nullptr || command == nullptr || strcmp(prefix, "AUTO") != 0) {
    enterAutoError("BAD_FORMAT");
    return;
  }

  AutoParsedFields fields;
  initAutoParsedFields(fields);
  char *token;
  while ((token = strtok_r(nullptr, ",", &savePointer)) != nullptr) {
    if (!parseAutoField(token, fields)) {
      enterAutoError("BAD_FIELD");
      return;
    }
  }
  processAutoCommand(command, fields);
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
  if ((autoModeActive() || mach3ControlRequested) && (line.startsWith("JOG") || line.startsWith("SEMI"))) {
    // AUTO mantiene la proprietà esclusiva del movimento fino a RESET.
    return;
  }

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
    updateLimits();
    char c = (char)Serial1.read();

    if (c == '\r') continue;

    if (c == '\n') {
      if (uartLineOverflow) {
        uartLineBuffer[uartLineLength] = '\0';
        if (strncmp(uartLineBuffer, "AUTO,", 5) == 0) {
          enterAutoError("LINE_TOO_LONG");
        }
      } else if (uartLineLength > 0) {
        uartLineBuffer[uartLineLength] = '\0';
        // DEBUG TEMPORANEO: riga UART ricevuta, solo su USB.
        Serial.print("DEBUG UART RX: [");
        Serial.print(uartLineBuffer);
        Serial.println("]");
        if (strncmp(uartLineBuffer, "AUTO,", 5) == 0) {
          processAutoLine(uartLineBuffer);
        } else if (strncmp(uartLineBuffer, "MACH3,", 6) == 0) {
          processMach3Command(uartLineBuffer);
        } else {
          // Compatibilità: JOG e SEMI continuano a usare l'interprete esistente.
          legacyUartLine = uartLineBuffer;
          processCommand(legacyUartLine);
        }
      }
      uartLineLength = 0;
      uartLineOverflow = false;
    } else {
      if (!uartLineOverflow) {
        if (uartLineLength < UART_LINE_CAPACITY - 1) {
          uartLineBuffer[uartLineLength++] = c;
        } else {
          uartLineOverflow = true;
        }
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

void initializeControlSourceSelectionPins() {
  // Carica prima i latch con lo stato sicuro, poi abilita le uscite push-pull.
  digitalWrite(ENABLE_X_SELECT_PIN, HIGH);
  digitalWrite(ENABLE_Y_SELECT_PIN, HIGH);
  digitalWrite(CONTROL_Z_SELECT_PIN, HIGH);
  digitalWrite(MACH3_SELECT_PIN, LOW);
  pinMode(ENABLE_X_SELECT_PIN, OUTPUT);
  pinMode(ENABLE_Y_SELECT_PIN, OUTPUT);
  pinMode(CONTROL_Z_SELECT_PIN, OUTPUT);
  pinMode(MACH3_SELECT_PIN, OUTPUT);
}

void updateControlSourceSelection() {
  bool selectX = false;
  bool selectY = false;
  bool selectZ = false;
  bool selectMach3 = false;

  if (autoModeActive()) {
    // AUTO ha proprietà esclusiva di X, Y e Z da BEGIN fino allo stato terminale.
    selectX = true;
    selectY = true;
    selectZ = true;
  } else if (mach3ControlRequested) {
    selectX = true;
    selectY = true;
    selectZ = true;
    selectMach3 = true;
  } else {
    // Fuori da AUTO e MACH3, ogni asse segue indipendentemente la propria richiesta.
    selectX = selectedManualX_sps != 0.0f;
    selectY = selectedManualY_sps != 0.0f;
    selectZ = selectedManualZ_sps != 0.0f;
  }

  digitalWrite(ENABLE_X_SELECT_PIN, selectX ? LOW : HIGH);
  digitalWrite(ENABLE_Y_SELECT_PIN, selectY ? LOW : HIGH);
  digitalWrite(CONTROL_Z_SELECT_PIN, selectZ ? LOW : HIGH);
  digitalWrite(MACH3_SELECT_PIN, selectMach3 ? HIGH : LOW);
}

// Commutazione eseguita esclusivamente nel loop UART, mai in ISR.
void processMach3Command(const char *line) {
  // DEBUG TEMPORANEO: diagnostica RX sulla sola USB, fuori dalle ISR.
  Serial.print("DEBUG MACH3 RX: ");
  Serial.println(line);
  if (strcmp(line, "MACH3,STATUS") == 0) {
    // DEBUG TEMPORANEO: copia della risposta sulla sola USB.
    Serial.print("DEBUG MACH3 TX: ");
    Serial.println(mach3ControlRequested ? "MACH3,STATUS,ACTIVE:1" : "MACH3,STATUS,ACTIVE:0");
    Serial1.println(mach3ControlRequested ? "MACH3,STATUS,ACTIVE:1" : "MACH3,STATUS,ACTIVE:0");
    return;
  }
  if (strcmp(line, "MACH3,ON") == 0) {
    if (mach3ControlRequested) {
      // DEBUG TEMPORANEO: copia della risposta sulla sola USB.
      Serial.print("DEBUG MACH3 TX: ");
      Serial.println("MACH3,ACK,ACTIVE:1");
      Serial1.println("MACH3,ACK,ACTIVE:1");
      return;
    }
    updateLimits();
    if (autoModeActive() || autoState == AUTO_ERROR) {
      // DEBUG TEMPORANEO: copia della risposta sulla sola USB.
      Serial.print("DEBUG MACH3 TX: ");
      Serial.println("MACH3,ERROR,CODE:AUTO_ACTIVE");
      Serial1.println("MACH3,ERROR,CODE:AUTO_ACTIVE");
      return;
    }
    if (jogX_sps || jogY_sps || jogZ_sps ||
        selectedManualX_sps || selectedManualY_sps || selectedManualZ_sps ||
        motorX.speedSps || motorY.speedSps || motorZ.speedSps ||
        semiActive(SEMI_X_PLUS_PIN) || semiActive(SEMI_X_MINUS_PIN) ||
        semiActive(SEMI_Y_PLUS_PIN) || semiActive(SEMI_Y_MINUS_PIN)) {
      // DEBUG TEMPORANEO: copia della risposta sulla sola USB.
      Serial.print("DEBUG MACH3 TX: ");
      Serial.println("MACH3,ERROR,CODE:BUSY");
      Serial1.println("MACH3,ERROR,CODE:BUSY");
      return;
    }
    clearManualMotionRequests();
    semiX_sps = semiY_sps = 0.0f;
    stopAutoStepGeneration();
    stopLegacyMotorOutputsForAuto();
    digitalWrite(ENABLE_X_SELECT_PIN, LOW);
    digitalWrite(ENABLE_Y_SELECT_PIN, LOW);
    digitalWrite(CONTROL_Z_SELECT_PIN, LOW);
    delayMicroseconds(1000);
    mach3ControlRequested = true;
    digitalWrite(MACH3_SELECT_PIN, HIGH);
    // DEBUG TEMPORANEO: copia della risposta sulla sola USB.
    Serial.print("DEBUG MACH3 TX: ");
    Serial.println("MACH3,ACK,ACTIVE:1");
    Serial1.println("MACH3,ACK,ACTIVE:1");
  } else if (strcmp(line, "MACH3,OFF") == 0) {
    digitalWrite(MACH3_SELECT_PIN, LOW);
    delayMicroseconds(1000);
    clearManualMotionRequests();
    mach3ControlRequested = false;
    updateControlSourceSelection();
    // DEBUG TEMPORANEO: copia della risposta sulla sola USB.
    Serial.print("DEBUG MACH3 TX: ");
    Serial.println("MACH3,ACK,ACTIVE:0");
    Serial1.println("MACH3,ACK,ACTIVE:0");
  }
}

void applyRequestedSpeeds() {
  if (mach3ControlRequested) {
    updateControlSourceSelection();
    return;
  }
  if (autoModeActive()) {
    // Il timer AUTO possiede STEP/DIR; il polling manuale non deve toccare le uscite.
    return;
  }

  float sx = chooseSemiAxisSpeed(jogX_sps, SEMI_X_PLUS_PIN, SEMI_X_MINUS_PIN, semiX_sps);
  float sy = chooseSemiAxisSpeed(jogY_sps, SEMI_Y_PLUS_PIN, SEMI_Y_MINUS_PIN, semiY_sps);
  float sz = jogZ_sps;

  selectedManualX_sps = sx;
  selectedManualY_sps = sy;
  selectedManualZ_sps = sz;
  updateControlSourceSelection();

  setMotorSpeed(motorX, sx, MAX_SPEED_XY_SPS);
  setMotorSpeed(motorY, sy, MAX_SPEED_XY_SPS);
  setMotorSpeed(motorZ, sz, MAX_SPEED_Z_SPS);
}

void setup() {
  digitalWrite(ALARM_OUTPUT_PIN, LOW);
  pinMode(ALARM_OUTPUT_PIN, OUTPUT);
  for (uint8_t i = 0; i < 5; i++) pinMode(limitPins[i], INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(LIMIT_X_MINUS_PIN), limitXMinusIsr, RISING);
  attachInterrupt(digitalPinToInterrupt(LIMIT_X_PLUS_PIN), limitXPlusIsr, RISING);
  attachInterrupt(digitalPinToInterrupt(LIMIT_Y_MINUS_PIN), limitYMinusIsr, RISING);
  attachInterrupt(digitalPinToInterrupt(LIMIT_Y_PLUS_PIN), limitYPlusIsr, RISING);
  attachInterrupt(digitalPinToInterrupt(LIMIT_Z_TOP_PIN), limitZTopIsr, RISING);
  initializeControlSourceSelectionPins();
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

  updateLimits();
  Serial.println("=== PRONTO ===");
}

void loop() {
  static uint32_t lastMs = 0;

  updateLimits();
  readUartCommands();
  updateAutoStepExecution();
  applyRequestedSpeeds();

  if (!mach3ControlRequested) {
    updateMotor(motorX);
    updateMotor(motorY);
    updateMotor(motorZ);
  }

  if (millis() - lastMs >= 100) {
    lastMs = millis();

    int32_t rawX = readCNTR32(CS_X);
    int32_t rawY = readCNTR32(CS_Y);
    int32_t rawZ = readCNTR32(CS_Z);

    int32_t x = acceptCounterRead(rawX, lastGoodX);
    int32_t y = acceptCounterRead(rawY, lastGoodY);
    int32_t z = acceptCounterRead(rawZ, lastGoodZ);
    haveGoodCounters = true;

    // DEBUG TEMPORANEO: disabilita il DRO USB continuo per leggere i log UART.
    constexpr bool USB_DRO_DEBUG = false;
    if (USB_DRO_DEBUG) {
      Serial.print("X:");
      Serial.print(x);
      Serial.print(" Y:");
      Serial.print(y);
      Serial.print(" Z:");
      Serial.println(z);
    }

    // UART verso Raspberry
    Serial1.print("X:");
    Serial1.print(x);
    Serial1.print(",Y:");
    Serial1.print(y);
    Serial1.print(",Z:");
    Serial1.println(z);
  }
}
