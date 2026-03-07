/*
 * PhysControlButtons.cpp  v2.0
 * ============================
 */
#include "PhysControlButtons.h"

// ── Defaults por botón ────────────────────────────────────────────────────
static const uint8_t  _defaultPins[4] = { CTRL_BTN_0, CTRL_BTN_1, CTRL_BTN_2, CTRL_BTN_3 };
static const uint8_t  _defaultFuncs[4] = {
    BTN_FUNC_PLAY_PAUSE,      // 0 → verde = tocando, rojo = parado
    BTN_FUNC_PREV_PAT_PLAY,   // 1 → patrón anterior y sigue tocando
    BTN_FUNC_NEXT_PAT_PLAY,   // 2 → patrón siguiente y sigue tocando
    BTN_FUNC_STOP             // 3 → parada limpia
};
static const uint32_t _defaultColorsOn[4] = {
    CTRL_CLR_GREEN,   // PLAY activo
    CTRL_CLR_ORANGE,  // PREV PAT
    CTRL_CLR_ORANGE,  // NEXT PAT
    CTRL_CLR_RED      // STOP
};
static const char* _defaultLabels[4] = {
    "PLAY/PAUSE", "PREV+PLAY", "NEXT+PLAY", "STOP"
};

// ── Constructor ───────────────────────────────────────────────────────────
PhysControlButtons::PhysControlButtons()
    : _leds(CTRL_LED_COUNT, CTRL_LED_PIN, NEO_GRB + NEO_KHZ800)
{
    for (int i = 0; i < 4; i++) {
        // lastRead=LOW, stable=false(LOW reposo), pressed unused, debounceEnd=0
        _btn[i] = { _defaultPins[i], false, false, false, 0 };
        // Config por defecto
        _cfg[i].funcId   = _defaultFuncs[i];
        _cfg[i].colorOff = CTRL_CLR_RED;
        _cfg[i].colorOn  = _defaultColorsOn[i];
        strncpy(_cfg[i].label, _defaultLabels[i], 19);
        _cfg[i].label[19] = '\0';
        // LED por defecto = rojo (apagado)
        _baseColor[i]   = CTRL_CLR_RED;
        _flashActive[i] = false;
        _flashEnd[i]    = 0;
    }
}

// ── begin() ───────────────────────────────────────────────────────────────
void PhysControlButtons::begin() {
    for (int i = 0; i < 4; i++) {
        // HTTM activo-ALTO: pull-down interno para mantener LOW en reposo
        // sin pull-down el pin flota y el debouncer se corrompe
        pinMode(_btn[i].pin, INPUT_PULLDOWN);
    }
    _leds.begin();
    _leds.setBrightness(CTRL_BRIGHTNESS);
    for (int i = 0; i < CTRL_LED_COUNT; i++) {
        _leds.setPixelColor(i, _baseColor[i]);
    }
    _leds.show();
}

// ── setCfg() ──────────────────────────────────────────────────────────────
void PhysControlButtons::setCfg(int idx, const BtnCfg& cfg) {
    if (idx < 0 || idx >= 4) return;
    _cfg[idx] = cfg;
    // Actualizar color base al colorOff
    _baseColor[idx] = cfg.colorOff;
    if (!_flashActive[idx]) {
        _leds.setPixelColor(idx, _baseColor[idx]);
        _leds.show();
    }
}

// ── setLedState() ─────────────────────────────────────────────────────────
void PhysControlButtons::setLedState(int idx, bool active) {
    if (idx < 0 || idx >= CTRL_LED_COUNT) return;
    _baseColor[idx] = active ? _cfg[idx].colorOn : _cfg[idx].colorOff;
    if (!_flashActive[idx]) {
        _leds.setPixelColor(idx, _baseColor[idx]);
        _leds.show();
    }
}

// ── flashLed() ────────────────────────────────────────────────────────────
void PhysControlButtons::flashLed(int idx, uint32_t color) {
    if (idx < 0 || idx >= CTRL_LED_COUNT) return;
    _flashActive[idx] = true;
    _flashEnd[idx]    = millis() + CTRL_FLASH_MS;
    _leds.setPixelColor(idx, color);
    _leds.show();
}

// ── refreshLeds() ─────────────────────────────────────────────────────────
void PhysControlButtons::refreshLeds() {
    for (int i = 0; i < CTRL_LED_COUNT; i++) {
        if (!_flashActive[i]) {
            _leds.setPixelColor(i, _baseColor[i]);
        }
    }
    _leds.show();
}

// ── update() ──────────────────────────────────────────────────────────────
void PhysControlButtons::update() {
    for (int i = 0; i < 4; i++) {
        _scanButton(i);
    }
    _processFlashes();
}

// ── _scanButton() ─────────────────────────────────────────────────────────
// HTTM capacitivo latch: toque 1 → OUT=HIGH, toque 2 → OUT=LOW, etc.
// Disparamos en CADA cambio de estado estable (subida y bajada) para que
// cada toque físico genere exactamente una acción, sin importar el ciclo.
void PhysControlButtons::_scanButton(int idx) {
    bool raw = (bool)digitalRead(_btn[idx].pin);

    // Reiniciar temporizador si la línea cambia
    if (raw != _btn[idx].lastRead) {
        _btn[idx].debounceEnd = millis() + CTRL_DEBOUNCE_MS;
        _btn[idx].lastRead    = raw;
    }
    if (millis() < _btn[idx].debounceEnd) return;

    // Actuar solo cuando el estado estable cambia
    if (raw == _btn[idx].stable) return;
    _btn[idx].stable = raw;   // guardar nuevo estado estable

    // Disparar en CUALQUIER flanco: cada cambio = un toque físico del HTTM
    if (onAction) {
        onAction(idx, _cfg[idx].funcId);
    }
}

// ── _processFlashes() ─────────────────────────────────────────────────────
void PhysControlButtons::_processFlashes() {
    bool changed = false;
    uint32_t now = millis();
    for (int i = 0; i < CTRL_LED_COUNT; i++) {
        if (_flashActive[i] && now >= _flashEnd[i]) {
            _flashActive[i] = false;
            _leds.setPixelColor(i, _baseColor[i]);
            changed = true;
        }
    }
    if (changed) _leds.show();
}
