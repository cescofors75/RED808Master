/*
 * LFOEngine.h
 * Per-pad LFO system — "Organic Drum Machine"
 *
 * Each of the 24 pads has its own LFO that can modulate:
 *   pitch, decay, filter cutoff, pan, volume
 *
 * LFOs sync to the sequencer BPM (1/4, 1/8, 1/16, 1/32)
 * or run free at a user-set Hz rate.
 *
 * The engine lives 100% on the ESP32. It periodically sends
 * the modulated parameter value via SPI to the Daisy slave
 * using existing commands (CMD_PAD_PITCH, CMD_TRACK_PAN, etc.).
 *
 * Web UI draws mini oscilloscope per pad via WebSocket.
 */

#ifndef LFO_ENGINE_H
#define LFO_ENGINE_H

#include <Arduino.h>
#include "protocol.h"

// Forward declarations (avoid circular includes)
class SPIMaster;

#define LFO_MAX_PADS   24

// ═══════════════════════════════════════════════════════
// LFO Waveform types
// ═══════════════════════════════════════════════════════
enum LfoWaveform : uint8_t {
    LFO_WAVE_SINE     = 0,
    LFO_WAVE_TRIANGLE = 1,
    LFO_WAVE_SQUARE   = 2,
    LFO_WAVE_SAW      = 3,
    LFO_WAVE_SH       = 4   // Sample & Hold (random)
};

// ═══════════════════════════════════════════════════════
// LFO Rate division (sync to BPM)
// ═══════════════════════════════════════════════════════
enum LfoDivision : uint8_t {
    LFO_RATE_1_4   = 0,    // Quarter note
    LFO_RATE_1_8   = 1,    // Eighth note
    LFO_RATE_1_16  = 2,    // Sixteenth note
    LFO_RATE_1_32  = 3,    // Thirty-second note
    LFO_RATE_FREE  = 4     // Free-running Hz
};

// ═══════════════════════════════════════════════════════
// LFO Modulation target
// ═══════════════════════════════════════════════════════
enum LfoTarget : uint8_t {
    LFO_TGT_PITCH   = 0,   // ±1200 cents
    LFO_TGT_DECAY   = 1,   // maxSamples multiplier
    LFO_TGT_FILTER  = 2,   // Filter cutoff Hz
    LFO_TGT_PAN     = 3,   // -100..+100
    LFO_TGT_VOLUME  = 4    // 0-150 track volume
};

// ═══════════════════════════════════════════════════════
// Per-pad LFO state
// ═══════════════════════════════════════════════════════
struct PadLFO {
    bool        active;
    LfoWaveform waveform;
    LfoDivision division;
    LfoTarget   target;
    uint8_t     depth;       // 0-100 (percentage of max range)
    float       freeHz;      // Hz when division == FREE (0.1-20.0)
    float       phase;       // 0.0 - 1.0 (wraps)
    float       value;       // Current output: -1.0 .. +1.0
    float       shValue;     // Latched S&H random value
    uint8_t     startPhase;  // Phase offset 0-255 (for per-pad offset)
    bool        retrigger;   // Reset phase on note trigger
};

// ═══════════════════════════════════════════════════════
// LFO scope data (sent to web UI for visualization)
// ═══════════════════════════════════════════════════════
#define LFO_SCOPE_POINTS  32  // Points per mini scope

struct LfoScopeData {
    float    values[LFO_MAX_PADS];     // Current value per pad (-1..+1)
    uint8_t  activeMask[3];            // Bitmask: [0]=pads 0-7, [1]=8-15, [2]=16-23
};

// ═══════════════════════════════════════════════════════
// LFOEngine class
// ═══════════════════════════════════════════════════════
class LFOEngine {
public:
    LFOEngine();

    // ── Configuration per pad ────────────────────────
    void setActive(uint8_t pad, bool on);
    bool isActive(uint8_t pad) const;

    void setWaveform(uint8_t pad, LfoWaveform wf);
    LfoWaveform getWaveform(uint8_t pad) const;

    void setDivision(uint8_t pad, LfoDivision div);
    LfoDivision getDivision(uint8_t pad) const;

    void setTarget(uint8_t pad, LfoTarget tgt);
    LfoTarget getTarget(uint8_t pad) const;

    void setDepth(uint8_t pad, uint8_t depth);  // 0-100
    uint8_t getDepth(uint8_t pad) const;

    void setFreeHz(uint8_t pad, float hz);       // 0.1-20.0
    float getFreeHz(uint8_t pad) const;

    void setPhaseOffset(uint8_t pad, uint8_t phase255); // 0-255
    void setRetrigger(uint8_t pad, bool on);

    // ── Runtime ──────────────────────────────────────
    // Call from the audio task (Core 1) at ~1kHz
    void update(float bpm, SPIMaster& spi);

    // Call when a pad is triggered (for retrigger feature)
    void onPadTrigger(uint8_t pad);

    // ── Scope data for web UI ────────────────────────
    // Returns current LFO values for all pads (thread-safe snapshot)
    void getScopeData(LfoScopeData& out) const;

    // Get full state of one pad LFO (for state sync to web)
    const PadLFO& getPadLFO(uint8_t pad) const;

    // ── Global ───────────────────────────────────────
    void resetAll();

private:
    PadLFO lfos[LFO_MAX_PADS];
    uint32_t lastUpdateUs;
    uint32_t lastSpiSendMs;     // Rate-limit SPI sends

    // Waveform generators (input: phase 0..1, output: -1..+1)
    static float waveSine(float phase);
    static float waveTriangle(float phase);
    static float waveSquare(float phase);
    static float waveSaw(float phase);

    // Convert LFO value to target parameter and send via SPI
    void applyModulation(uint8_t pad, SPIMaster& spi);

    // Calculate frequency from BPM and division
    static float divisionToHz(float bpm, LfoDivision div);
};

#endif // LFO_ENGINE_H
