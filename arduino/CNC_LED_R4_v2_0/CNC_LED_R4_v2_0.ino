#include <Adafruit_NeoPixel.h>

// CNC LED status display - Arduino UNO R4 Minima
// Version 2.0: three independent WS2812B strips, 10 LEDs each.
// ALARM and axis inputs are NPN-inverted: Teensy HIGH -> Arduino LOW.
// MACH3 is direct, non-inverted: Arduino PIN_CAM HIGH = MACH3 active.
constexpr uint8_t PIN_ALARM    = 9;
constexpr uint8_t PIN_ENABLE_Z = 10;
constexpr uint8_t PIN_ENABLE_Y = 11;
constexpr uint8_t PIN_CAM      = 12;
constexpr uint8_t PIN_ENABLE_X = 8;

constexpr uint8_t PIN_LED_X = 6;
constexpr uint8_t PIN_LED_Y = 7;
constexpr uint8_t PIN_LED_Z = 5;
constexpr uint16_t LEDS_PER_STRIP = 10;
constexpr uint8_t BRIGHTNESS = 60;
constexpr unsigned long BLINK_MS = 350;

Adafruit_NeoPixel ledX(LEDS_PER_STRIP, PIN_LED_X, NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel ledY(LEDS_PER_STRIP, PIN_LED_Y, NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel ledZ(LEDS_PER_STRIP, PIN_LED_Z, NEO_GRB + NEO_KHZ800);

// True when original Teensy output is HIGH (NPN stage inverts it).
bool teensyHigh(uint8_t pin) {
  return digitalRead(pin) == LOW;
}

void fillStrip(Adafruit_NeoPixel &strip, uint32_t color) {
  for (uint16_t i = 0; i < strip.numPixels(); ++i) {
    strip.setPixelColor(i, color);
  }
}

void setup() {
  pinMode(PIN_ALARM, INPUT_PULLUP);
  pinMode(PIN_ENABLE_Z, INPUT_PULLUP);
  pinMode(PIN_ENABLE_Y, INPUT_PULLUP);
  pinMode(PIN_CAM, INPUT_PULLUP);
  pinMode(PIN_ENABLE_X, INPUT_PULLUP);

  ledX.begin();
  ledY.begin();
  ledZ.begin();
  ledX.setBrightness(BRIGHTNESS);
  ledY.setBrightness(BRIGHTNESS);
  ledZ.setBrightness(BRIGHTNESS);
  ledX.clear();
  ledY.clear();
  ledZ.clear();
  ledX.show();
  ledY.show();
  ledZ.show();
}

void loop() {
  const bool alarm = !teensyHigh(PIN_ALARM);  // Arduino HIGH = alarm/disconnected
  const bool padZ = teensyHigh(PIN_ENABLE_Z);
  const bool teensyY = teensyHigh(PIN_ENABLE_Y);
  const bool mach3Mode = digitalRead(PIN_CAM) == HIGH;
  const bool teensyX = teensyHigh(PIN_ENABLE_X);

  const uint32_t red = ledX.Color(255, 0, 0);
  const uint32_t green = ledX.Color(0, 255, 0);
  const uint32_t blue = ledX.Color(0, 0, 255);

  if (mach3Mode) {
    // Direct MACH3 input: blue has absolute priority over ALARM and axis inputs.
    fillStrip(ledX, blue);
    fillStrip(ledY, blue);
    fillStrip(ledZ, blue);
  } else if (alarm) {
    // Outside MACH3, all three strips flash red on alarm.
    const bool on = ((millis() / BLINK_MS) % 2UL) == 0;
    const uint32_t color = on ? red : 0;
    fillStrip(ledX, color);
    fillStrip(ledY, color);
    fillStrip(ledZ, color);
  } else {
    // Same X/Y inverted colors and Z logic as tested v1.5.
    fillStrip(ledX, teensyX ? green : red);
    fillStrip(ledY, teensyY ? green : red);
    fillStrip(ledZ, padZ ? green : red);
  }

  ledX.show();
  ledY.show();
  ledZ.show();
  delay(10);
}
