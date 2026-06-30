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
  uint8_t enPin;

  volatile float speedSps;       // valore attuale step/s
  bool stepState;
  uint32_t lastToggleUs;
  uint32_t nextIntervalUs;
};

AxisMotor motorX = {X_STEP_PIN, X_DIR_PIN, X_EN_PIN, 0.0f, false, 0, 0};
AxisMotor motorY = {Y_STEP_PIN, Y_DIR_PIN, Y_EN_PIN, 0.0f, false, 0, 0};
AxisMotor motorZ = {Z_STEP_PIN, Z_DIR_PIN, Z_EN_PIN, 0.0f, false, 0, 0};

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
volatile bool autoStepTimerRunning = false;
volatile bool autoStepPulsePhase = false;
volatile bool autoStepSegmentComplete = false;
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
  if (mask & 0x01) ocWrite(motorX.stepPin, HIGH);
  if (mask & 0x02) ocWrite(motorY.stepPin, HIGH);
  if (mask & 0x04) ocWrite(motorZ.stepPin, HIGH);
}

void stopAutoStepGeneration() {
  noInterrupts();
  autoStepTimerRunning = false;
  autoStepTimer.end();
  uint8_t pulseMask = autoStepPulseMask;
  autoStepPulseMask = 0;
  autoStepPulsePhase = false;
  autoStepSegmentComplete = false;
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

void autoStepTimerIsr() {
  if (!autoStepTimerRunning) {
    autoStepTimer.end();
    return;
  }

  if (!autoStepPulsePhase && autoIsrEarliestStepUs != 0) {
    int32_t remainingUs = (int32_t)(autoIsrEarliestStepUs - micros());
    if (remainingUs > 0) {
      autoStepTimer.update((uint32_t)remainingUs);
      return;
    }
    autoIsrEarliestStepUs = 0;
  }

  if (autoStepPulsePhase) {
    bool finalPulse = autoIsrDominantStepIndex >= autoIsrDominantSteps;
    if (finalPulse) autoStepTimer.end();
    uint8_t pulseMask = autoStepPulseMask;
    autoStepPulseMask = 0;
    releaseAutoStepMask(pulseMask);

    if (finalPulse) {
      autoStepPulsePhase = false;
      autoStepTimerRunning = false;
      autoStepSegmentComplete = true;
      return;
    }

    autoStepPulsePhase = false;
    autoStepTimer.update(autoIsrStepIntervalUs - STEP_PULSE_US);
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
  if (pulseMask & 0x01) ocWrite(motorX.stepPin, LOW);
  if (pulseMask & 0x02) ocWrite(motorY.stepPin, LOW);
  if (pulseMask & 0x04) ocWrite(motorZ.stepPin, LOW);
  autoIsrDominantStepIndex++;
  autoStepPulsePhase = true;
  autoStepTimer.update(STEP_PULSE_US);
}

void clearAutoFifo() {
  stopAutoStepGeneration();
  autoFifoHead = 0;
  autoFifoTail = 0;
  autoFifoCount = 0;
  autoBufferLowReported = false;
  autoSegmentActive = false;
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
  // STOPPED ed ERROR mantengono il blocco manuale fino a RESET esplicito.
  return autoState != AUTO_IDLE;
}

void clearManualMotionRequests() {
  jogX_sps = 0.0f;
  jogY_sps = 0.0f;
  jogZ_sps = 0.0f;
  semiX_sps = 0.0f;
  semiY_sps = 0.0f;
  semiZ_sps = 0.0f;
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
  sendAutoError(autoJobId, code, segmentId);
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
  bool timerRunning = autoStepTimerRunning;
  interrupts();

  char payload[420];
  snprintf(payload, sizeof(payload),
           "AUTO,STATUS,JOB:%s,STATE:%s,Q:%u,FREE:%u,LAST:%lu,EXEC:%u,"
           "ACTIVE:%lu,LAST_DONE:%lu,LOGICAL_RUN:0,STEP_RUN:%u,"
           "AUTO_ACTIVE:%u,MANUAL_BLOCKED:%u,"
           "CMDX:%ld,CMDY:%ld,CMDZ:%ld,PLAN:%u,DOM:%c,DOM_STEPS:%lu,"
           "STEP_US:%.3f,CALC_US:%lu,TEST_T_US:%lu,ISR_STEP:%lu,"
           "EMITX:%lu,EMITY:%lu,EMITZ:%lu",
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
           (unsigned long)emittedY, (unsigned long)emittedZ);
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
    ocWrite(motorX.dirPin, autoInterpolationPlan.deltaX > 0 ? HIGH : LOW);
  }
  if (autoInterpolationPlan.deltaY != 0) {
    ocWrite(motorY.dirPin, autoInterpolationPlan.deltaY > 0 ? HIGH : LOW);
  }
  if (autoInterpolationPlan.deltaZ != 0) {
    ocWrite(motorZ.dirPin, autoInterpolationPlan.deltaZ > 0 ? HIGH : LOW);
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
  autoStepPulsePhase = false;
  autoStepSegmentComplete = autoInterpolationPlan.dominantSteps == 0;
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
  if (autoState != AUTO_RUNNING) return;
  if (!autoSegmentActive) {
    startNextAutoStepSegment();
    return;
  }

  noInterrupts();
  bool segmentComplete = autoStepSegmentComplete;
  uint32_t emittedX = autoIsrEmittedX;
  uint32_t emittedY = autoIsrEmittedY;
  uint32_t emittedZ = autoIsrEmittedZ;
  uint32_t dominantStepIndex = autoIsrDominantStepIndex;
  if (segmentComplete) autoStepSegmentComplete = false;
  interrupts();
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
    autoState = AUTO_IDLE;
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
  if (autoModeActive() && (line.startsWith("JOG") || line.startsWith("SEMI"))) {
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
        if (strncmp(uartLineBuffer, "AUTO,", 5) == 0) {
          processAutoLine(uartLineBuffer);
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

void applyRequestedSpeeds() {
  if (autoModeActive()) {
    // Il timer AUTO possiede STEP/DIR; il polling manuale non deve toccare le uscite.
    return;
  }

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
  updateAutoStepExecution();
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
