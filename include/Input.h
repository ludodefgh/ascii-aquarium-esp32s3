// EC11 rotary encoder (A/B/PUSH) + K0 button input driver.
// Replaces the XPT2046 touch layer from the original CYD sketch.
#pragma once

#include <Arduino.h>

// Wiring (see platformio.ini header comment):
//   EC11_A = GPIO4, EC11_B = GPIO5, EC11_PUSH = GPIO6, K0 = GPIO15
#define PIN_ENC_A     4
#define PIN_ENC_B     5
#define PIN_ENC_PUSH  6
#define PIN_K0        15

void inputInit();

// Returns accumulated encoder detents since the last call (+1 per clockwise
// step, -1 per counter-clockwise step) and resets the internal counter.
int inputReadEncoderDelta();

// Edge-triggered (true exactly once per physical press), debounced.
bool inputEncoderPressed();
bool inputK0Pressed();

// Level, debounced - useful for press-and-hold gestures.
bool inputEncoderHeld();
bool inputK0Held();
