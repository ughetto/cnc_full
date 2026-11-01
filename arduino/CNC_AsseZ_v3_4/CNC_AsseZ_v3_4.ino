/* CNC_AsseZ_v3_4 - PAD asse Z
   Parametri pulsanti ripristinati da v3_2:
   SLOW: 250 passi/s; FAST: 10000 passi/s; accelerazione: 10000 passi/s^2.
   Impulso STEP LOW 100 us; a riposo HIGH; Z_TOP HIGH blocca la salita.
   NOTA: a 100 us di LOW, 10000 passi/s e' un limite teorico, non
   raggiungibile esattamente con digitalWrite e gestione software.
*/
#include <Arduino.h>
#include <math.h>

constexpr uint8_t PIN_DIR = 4, PIN_STEP = 5, PIN_Z_TOP = 7;
constexpr uint8_t PIN_ENC_A = 2, PIN_ENC_B = 3;
constexpr uint8_t PIN_SLOW_DOWN = 10, PIN_FAST_DOWN = 11;
constexpr uint8_t PIN_SLOW_UP = 12, PIN_FAST_UP = 13;
// Polarita' collaudate nella versione 3_3.
constexpr uint8_t DIR_UP = LOW, DIR_DOWN = HIGH;
constexpr uint32_t STEP_LOW_US = 100, DIR_SETUP_US = 15;
constexpr float SLOW_MAX_SPEED = 250.0f;
constexpr float FAST_MAX_SPEED = 10000.0f;
constexpr float BUTTON_ACCEL = 10000.0f;
constexpr uint32_t ENC_PERIOD_US = 600;
constexpr int16_t MAX_ENCODER_QUEUE = 80;

volatile int16_t encoderQueue = 0; // positivo discesa, negativo salita
void encoderInterrupt() {
  if (digitalRead(PIN_ENC_B) == HIGH) {
    if (encoderQueue < MAX_ENCODER_QUEUE) ++encoderQueue;
  } else {
    if (encoderQueue > -MAX_ENCODER_QUEUE) --encoderQueue;
  }
}
void clearEncoderQueue() {
  noInterrupts(); encoderQueue = 0; interrupts();
}
void clearQueuedUp() {
  noInterrupts();
  if (encoderQueue < 0) encoderQueue = 0;
  interrupts();
}

enum PulseState : uint8_t { IDLE, DIR_SETTLING, STEP_LOW };
PulseState pulseState = IDLE;
uint32_t stateSince = 0, lastStepAt = 0, lastRampAt = 0;
bool haveLastStep = false, plannedEncoder = false;
uint8_t plannedDir = DIR_DOWN;
float signedSpeed = 0.0f; // positivo discesa, negativo salita

void setup() {
  digitalWrite(PIN_STEP, HIGH);
  pinMode(PIN_STEP, OUTPUT);
  digitalWrite(PIN_DIR, DIR_DOWN);
  pinMode(PIN_DIR, OUTPUT);
  pinMode(PIN_ENC_A, INPUT);
  pinMode(PIN_ENC_B, INPUT);
  pinMode(PIN_SLOW_DOWN, INPUT_PULLUP);
  pinMode(PIN_FAST_DOWN, INPUT_PULLUP);
  pinMode(PIN_SLOW_UP, INPUT_PULLUP);
  pinMode(PIN_FAST_UP, INPUT_PULLUP);
  pinMode(PIN_Z_TOP, INPUT);
  lastRampAt = micros();
  attachInterrupt(digitalPinToInterrupt(PIN_ENC_A), encoderInterrupt, RISING);
}

void loop() {
  const uint32_t now = micros();
  const bool atTop = digitalRead(PIN_Z_TOP) == HIGH;
  if (atTop) {
    clearQueuedUp();
    if (signedSpeed < 0) signedSpeed = 0; // arresto di sicurezza, senza rampa
  }

  // La durata minima LOW viene sempre rispettata prima di tornare HIGH.
  if (pulseState == STEP_LOW) {
    if ((uint32_t)(now - stateSince) >= STEP_LOW_US) {
      digitalWrite(PIN_STEP, HIGH);
      pulseState = IDLE;
    } else return;
  }

  const bool slowUp = digitalRead(PIN_SLOW_UP) == LOW;
  const bool fastUp = digitalRead(PIN_FAST_UP) == LOW;
  const bool slowDown = digitalRead(PIN_SLOW_DOWN) == LOW;
  const bool fastDown = digitalRead(PIN_FAST_DOWN) == LOW;
  const bool buttonsPressed = slowUp || fastUp || slowDown || fastDown;
  if (buttonsPressed) clearEncoderQueue();

  const bool requestUp = slowUp || fastUp;
  const bool requestDown = slowDown || fastDown;
  int8_t requestedSign = 0;
  float maxSpeed = SLOW_MAX_SPEED;
  if (!(requestUp && requestDown)) {
    if (requestUp && !atTop) requestedSign = -1;
    else if (requestDown) requestedSign = 1;
    if (fastUp || fastDown) maxSpeed = FAST_MAX_SPEED;
  }

  // Rampa dei pulsanti: accelerazione e velocita' della v3_2.
  // Al rilascio, decelera fino a zero; se cambia direzione frena prima.
  const uint32_t dtUs = (uint32_t)(now - lastRampAt);
  lastRampAt = now;
  const float dv = BUTTON_ACCEL * (min(dtUs, (uint32_t)20000) * 0.000001f);
  const float target = requestedSign * maxSpeed;
  if (signedSpeed < target) signedSpeed = min(target, signedSpeed + dv);
  else if (signedSpeed > target) signedSpeed = max(target, signedSpeed - dv);
  if (atTop && signedSpeed < 0) signedSpeed = 0;
  if (fabsf(signedSpeed) < 0.01f) signedSpeed = 0;

  bool requested = false, fromEncoder = false;
  uint8_t dir = DIR_DOWN;
  uint32_t period = 0;
  if (signedSpeed != 0) {
    requested = true;
    dir = signedSpeed < 0 ? DIR_UP : DIR_DOWN;
    period = (uint32_t)(1000000.0f / fabsf(signedSpeed));
    if (period < STEP_LOW_US + DIR_SETUP_US + 10)
      period = STEP_LOW_US + DIR_SETUP_US + 10;
  } else if (!buttonsPressed) {
    noInterrupts();
    const int16_t pending = encoderQueue;
    interrupts();
    if (pending > 0 || (pending < 0 && !atTop)) {
      requested = true;
      fromEncoder = true;
      dir = pending > 0 ? DIR_DOWN : DIR_UP;
      period = ENC_PERIOD_US;
    }
  }

  if (!requested) {
    pulseState = IDLE;
    haveLastStep = false;
    return;
  }

  if (pulseState == DIR_SETTLING) {
    if (dir != plannedDir || fromEncoder != plannedEncoder) {
      pulseState = IDLE;
    } else if ((uint32_t)(now - stateSince) >= DIR_SETUP_US) {
      if (dir == DIR_UP && digitalRead(PIN_Z_TOP) == HIGH) {
        clearQueuedUp();
        signedSpeed = 0;
        pulseState = IDLE;
        return;
      }
      digitalWrite(PIN_STEP, LOW);
      lastStepAt = micros();
      haveLastStep = true;
      stateSince = lastStepAt;
      pulseState = STEP_LOW;
      if (fromEncoder) {
        noInterrupts();
        if (dir == DIR_DOWN && encoderQueue > 0) --encoderQueue;
        if (dir == DIR_UP && encoderQueue < 0) ++encoderQueue;
        interrupts();
      }
      return;
    } else return;
  }

  if (haveLastStep && (uint32_t)(now - lastStepAt) < period) return;
  plannedDir = dir;
  plannedEncoder = fromEncoder;
  digitalWrite(PIN_DIR, dir);
  stateSince = micros();
  pulseState = DIR_SETTLING;
}
