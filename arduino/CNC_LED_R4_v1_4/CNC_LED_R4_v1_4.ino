#include <Adafruit_NeoPixel.h>

// CNC LED status display - Arduino UNO R4 Minima
// Five NPN-inverted inputs: Teensy HIGH -> Arduino LOW.
constexpr uint8_t PIN_ALARM = 9;
constexpr uint8_t PIN_ENABLE_Z = 10;
constexpr uint8_t PIN_ENABLE_Y = 11;
constexpr uint8_t PIN_CAM = 12;
constexpr uint8_t PIN_ENABLE_X = 8;
constexpr uint8_t PIN_STRIP = 6;

constexpr uint16_t LED_COUNT = 60;
constexpr uint8_t BRIGHTNESS = 60;
constexpr uint8_t GROUP_SIZE = 8;
constexpr uint8_t Z_START = 36;
constexpr uint8_t Y_START = 44;
constexpr uint8_t X_START = 52;
constexpr unsigned long BLINK_MS = 350;

Adafruit_NeoPixel strip(LED_COUNT, PIN_STRIP, NEO_GRB + NEO_KHZ800);

// True when the original Teensy output is HIGH.
bool teensyHigh(uint8_t pin) {
  return digitalRead(pin) == LOW;
}

void fillGroup(uint8_t first, uint32_t color) {
  for (uint8_t i = 0; i < GROUP_SIZE; ++i) {
    strip.setPixelColor(first + i, color);
  }
}

void setup() {
  pinMode(PIN_ALARM, INPUT_PULLUP);
  pinMode(PIN_ENABLE_Z, INPUT_PULLUP);
  pinMode(PIN_ENABLE_Y, INPUT_PULLUP);
  pinMode(PIN_CAM, INPUT_PULLUP);
  pinMode(PIN_ENABLE_X, INPUT_PULLUP);
  strip.begin();
  strip.setBrightness(BRIGHTNESS);
  strip.clear();
  strip.show();
}

void loop() {
  const bool alarm = !teensyHigh(PIN_ALARM);  // HIGH Arduino = alarm or disconnected wire
  const bool padZ = teensyHigh(PIN_ENABLE_Z);
  const bool teensyY = teensyHigh(PIN_ENABLE_Y);
  const bool teensyMode = teensyHigh(PIN_CAM);
  const bool teensyX = teensyHigh(PIN_ENABLE_X);

  const uint32_t red = strip.Color(255, 0, 0);
  const uint32_t green = strip.Color(0, 255, 0);
  const uint32_t blue = strip.Color(0, 0, 255);
  strip.clear();  // LEDs 0..35 always off.

  if (alarm) {
    // Alarm has highest priority; flash all 24 active LEDs red.
    const bool on = ((millis() / BLINK_MS) % 2UL) == 0;
    const uint32_t color = on ? red : 0;
    fillGroup(Z_START, color);
    fillGroup(Y_START, color);
    fillGroup(X_START, color);
  } else if (teensyMode) {
    // CAM/Mach3: original Teensy CAM signal HIGH (Arduino input LOW).
    fillGroup(Z_START, blue);
    fillGroup(Y_START, blue);
    fillGroup(X_START, blue);
  } else {
    fillGroup(Z_START, padZ ? green : red);
    fillGroup(Y_START, teensyY ? green : red);
    fillGroup(X_START, teensyX ? green : red);
  }
  strip.show();
  delay(10);  // Short refresh interval; alarm blinking uses millis().
}
