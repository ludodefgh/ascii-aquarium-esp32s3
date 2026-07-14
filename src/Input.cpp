#include "Input.h"

// Full-step quadrature state table (Buxton/Dannegger), robust to contact
// bounce without needing a separate debounce timer on A/B.
// State 0 is the stable "resting" position between detents.
static const uint8_t kDirNone = 0x0;
static const uint8_t kDirCW   = 0x10;
static const uint8_t kDirCCW  = 0x20;

static const uint8_t kTransitionTable[7][4] = {
    {0x0, 0x2, 0x4, 0x0},
    {0x3, 0x0, 0x1, 0x10},
    {0x3, 0x2, 0x0, 0x0},
    {0x3, 0x2, 0x1, 0x0},
    {0x6, 0x0, 0x4, 0x0},
    {0x6, 0x5, 0x0, 0x20},
    {0x6, 0x5, 0x4, 0x0},
};

static volatile uint8_t s_encState = 0;
static volatile int32_t s_encAccum = 0;

static void IRAM_ATTR encoderISR() {
    uint8_t pins = (digitalRead(PIN_ENC_A) << 1) | digitalRead(PIN_ENC_B);
    s_encState = kTransitionTable[s_encState & 0x0F][pins];
    uint8_t dir = s_encState & 0x30;
    if (dir == kDirCW) {
        s_encAccum++;
    } else if (dir == kDirCCW) {
        s_encAccum--;
    }
}

struct DebouncedButton {
    uint8_t pin;
    bool stableState = false;   // debounced level, true = pressed
    bool rawState = false;
    bool edgeFired = false;
    unsigned long lastChangeMs = 0;
};

static DebouncedButton s_encBtn{PIN_ENC_PUSH};
static DebouncedButton s_k0Btn{PIN_K0};

static const unsigned long kDebounceMs = 25;

static void updateButton(DebouncedButton &btn) {
    bool level = (digitalRead(btn.pin) == LOW);  // active-low
    unsigned long now = millis();

    if (level != btn.rawState) {
        btn.rawState = level;
        btn.lastChangeMs = now;
    }

    if ((now - btn.lastChangeMs) >= kDebounceMs && btn.stableState != btn.rawState) {
        btn.stableState = btn.rawState;
        if (btn.stableState) {
            btn.edgeFired = true;  // press edge, consumed by *Pressed()
        }
    }
}

void inputInit() {
    pinMode(PIN_ENC_A, INPUT_PULLUP);
    pinMode(PIN_ENC_B, INPUT_PULLUP);
    pinMode(PIN_ENC_PUSH, INPUT_PULLUP);
    pinMode(PIN_K0, INPUT_PULLUP);

    attachInterrupt(digitalPinToInterrupt(PIN_ENC_A), encoderISR, CHANGE);
    attachInterrupt(digitalPinToInterrupt(PIN_ENC_B), encoderISR, CHANGE);
}

int inputReadEncoderDelta() {
    noInterrupts();
    int32_t accum = s_encAccum;
    s_encAccum = 0;
    interrupts();
    return (int)accum;
}

bool inputEncoderPressed() {
    updateButton(s_encBtn);
    if (s_encBtn.edgeFired) {
        s_encBtn.edgeFired = false;
        return true;
    }
    return false;
}

bool inputK0Pressed() {
    updateButton(s_k0Btn);
    if (s_k0Btn.edgeFired) {
        s_k0Btn.edgeFired = false;
        return true;
    }
    return false;
}

bool inputEncoderHeld() {
    updateButton(s_encBtn);
    return s_encBtn.stableState;
}

bool inputK0Held() {
    updateButton(s_k0Btn);
    return s_k0Btn.stableState;
}
