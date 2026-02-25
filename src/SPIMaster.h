/*
 * SPIMaster.h
 * RED808 SPI Master — Controls STM32 Audio Slave
 * Replaces AudioEngine for all audio DSP operations
 */

#ifndef SPI_MASTER_H
#define SPI_MASTER_H

#include <Arduino.h>
#include <SPI.h>
#include "protocol.h"
#include <freertos/semphr.h>

// Audio constants (shared with STM32)
#define SAMPLE_RATE 44100
#define MAX_VOICES 10

// ═══════════════════════════════════════════════════════
// SPI HARDWARE PINS (ESP32-S3 HSPI)
// ═══════════════════════════════════════════════════════
// Modo mínimo: solo 4 líneas SPI + GND
// Descomenta USE_SPI_SYNC_IRQ cuando añadas SYNC/IRQ
//#define USE_SPI_SYNC_IRQ  // Habilitar SYNC (GPIO9) e IRQ (GPIO14)

#define STM32_SPI_CS     10   // HSPI CS
#define STM32_SPI_MOSI   11   // HSPI MOSI
#define STM32_SPI_SCK    12   // HSPI SCK
#define STM32_SPI_MISO   13   // HSPI MISO

#ifdef USE_SPI_SYNC_IRQ
#define STM32_SPI_SYNC    9   // ESP32 → STM32: command ready pulse
#define STM32_SPI_IRQ    14   // STM32 → ESP32: data ready / request
#endif

// SPI Speed: 20 MHz — Daisy Seed (STM32H750 @ 480MHz) lo maneja sin problemas
#define STM32_SPI_CLOCK  20000000

// Audio constants (mirrored from old AudioEngine for compatibility)
static constexpr int MAX_AUDIO_TRACKS = 16;
static constexpr int MAX_PADS = 24;

// Filter types (keep compatible with old AudioEngine enums for WebInterface)
enum FilterType {
    FILTER_NONE = 0,
    FILTER_LOWPASS = 1,
    FILTER_HIGHPASS = 2,
    FILTER_BANDPASS = 3,
    FILTER_NOTCH = 4,
    FILTER_ALLPASS = 5,
    FILTER_PEAKING = 6,
    FILTER_LOWSHELF = 7,
    FILTER_HIGHSHELF = 8,
    FILTER_RESONANT = 9,
    FILTER_SCRATCH = 10,
    FILTER_TURNTABLISM = 11,
    FILTER_REVERSE = 12,
    FILTER_HALFSPEED = 13,
    FILTER_STUTTER = 14
};

// Distortion modes
enum DistortionMode {
    DIST_SOFT = 0,
    DIST_HARD = 1,
    DIST_TUBE = 2,
    DIST_FUZZ = 3
};

// Filter preset structure (for UI compatibility)
struct FilterPreset {
    FilterType type;
    float cutoff;
    float resonance;
    float gain;
    const char* name;
};

class SPIMaster {
public:
    SPIMaster();
    ~SPIMaster();
    
    // Initialization
    bool begin();
    
    // ══════════════════════════════════════════════════
    // TRIGGERS (replaces AudioEngine trigger methods)
    // ══════════════════════════════════════════════════
    void triggerSampleSequencer(int padIndex, uint8_t velocity, uint8_t trackVolume = 100, uint32_t maxSamples = 0);
    void triggerSampleLive(int padIndex, uint8_t velocity);
    void triggerSample(int padIndex, uint8_t velocity);  // Alias for triggerSampleLive
    void stopSample(int padIndex);
    void stopAll();
    void triggerSidechain(int sourceTrack);              // Manually fire sidechain ducking
    bool triggerBulk(const TriggerSeqPayload* triggers, uint8_t count); // Up to 16 simultaneous
    
    // ══════════════════════════════════════════════════
    // VOLUME CONTROL
    // ══════════════════════════════════════════════════
    void setMasterVolume(uint8_t volume);
    uint8_t getMasterVolume();
    void setSequencerVolume(uint8_t volume);
    uint8_t getSequencerVolume();
    void setLiveVolume(uint8_t volume);
    uint8_t getLiveVolume();
    void setLivePitchShift(float pitch);
    float getLivePitchShift();
    
    // ══════════════════════════════════════════════════
    // GLOBAL FILTER (legacy FX chain)
    // ══════════════════════════════════════════════════
    void setFilterType(FilterType type);
    void setFilterCutoff(float cutoff);
    void setFilterResonance(float resonance);
    void setBitDepth(uint8_t bits);
    void setDistortion(float amount);
    void setDistortionMode(DistortionMode mode);
    void setSampleRateReduction(uint32_t rate);
    
    // ══════════════════════════════════════════════════
    // MASTER EFFECTS
    // ══════════════════════════════════════════════════
    // Delay/Echo
    void setDelayActive(bool active);
    void setDelayTime(float ms);
    void setDelayFeedback(float feedback);
    void setDelayMix(float mix);
    
    // Phaser
    void setPhaserActive(bool active);
    void setPhaserRate(float hz);
    void setPhaserDepth(float depth);
    void setPhaserFeedback(float feedback);
    
    // Flanger
    void setFlangerActive(bool active);
    void setFlangerRate(float hz);
    void setFlangerDepth(float depth);
    void setFlangerFeedback(float feedback);
    void setFlangerMix(float mix);
    
    // Compressor/Limiter
    void setCompressorActive(bool active);
    void setCompressorThreshold(float threshold);
    void setCompressorRatio(float ratio);
    void setCompressorAttack(float ms);
    void setCompressorRelease(float ms);
    void setCompressorMakeupGain(float db);
    
    // ══════════════════════════════════════════════════
    // PER-TRACK FILTER
    // ══════════════════════════════════════════════════
    bool setTrackFilter(int track, FilterType type, float cutoff = 1000.0f, float resonance = 1.0f, float gain = 0.0f);
    void clearTrackFilter(int track);
    FilterType getTrackFilter(int track);
    int getActiveTrackFiltersCount();
    
    // ══════════════════════════════════════════════════
    // PER-PAD FILTER
    // ══════════════════════════════════════════════════
    bool setPadFilter(int pad, FilterType type, float cutoff = 1000.0f, float resonance = 1.0f, float gain = 0.0f);
    void clearPadFilter(int pad);
    FilterType getPadFilter(int pad);
    int getActivePadFiltersCount();
    
    // ══════════════════════════════════════════════════
    // PER-PAD / PER-TRACK FX
    // ══════════════════════════════════════════════════
    void setPadDistortion(int pad, float amount, DistortionMode mode = DIST_SOFT);
    void setPadBitCrush(int pad, uint8_t bits);
    void clearPadFX(int pad);
    void setTrackDistortion(int track, float amount, DistortionMode mode = DIST_SOFT);
    void setTrackBitCrush(int track, uint8_t bits);
    void clearTrackFX(int track);
    
    // Per-track live FX (echo, flanger, compressor)
    void setTrackEcho(int track, bool active, float time = 100.0f, float feedback = 40.0f, float mix = 50.0f);
    void setTrackFlanger(int track, bool active, float rate = 50.0f, float depth = 50.0f, float feedback = 30.0f);
    void setTrackCompressor(int track, bool active, float threshold = -20.0f, float ratio = 4.0f);
    void clearTrackLiveFX(int track);
    bool getTrackEchoActive(int track) const;
    bool getTrackFlangerActive(int track) const;
    bool getTrackCompressorActive(int track) const;

    // Per-track FX send levels (to master bus FX)
    void setTrackReverbSend(int track, uint8_t level);   // 0-100
    void setTrackDelaySend(int track, uint8_t level);    // 0-100
    void setTrackChorusSend(int track, uint8_t level);   // 0-100

    // Per-track mixer controls
    void setTrackPan(int track, int8_t pan);             // -100..+100
    void setTrackMute(int track, bool mute);
    void setTrackSolo(int track, bool solo);

    // Per-track extended FX
    void setTrackPhaser(int track, bool active, float rate = 1.0f, float depth = 50.0f, float feedback = 50.0f);
    void setTrackTremolo(int track, bool active, float rate = 4.0f, float depth = 50.0f);
    void setTrackPitch(int track, int16_t cents);        // -1200..+1200
    void setTrackGate(int track, bool active, float thresholdDb = -40.0f, float attackMs = 1.0f, float releaseMs = 50.0f);
    void setTrackEqLow(int track, int8_t gainDb);        // -12..+12
    void setTrackEqMid(int track, int8_t gainDb);        // -12..+12
    void setTrackEqHigh(int track, int8_t gainDb);       // -12..+12
    void setTrackEq(int track, int8_t low, int8_t mid, int8_t high); // all-in-one
    
    // ══════════════════════════════════════════════════
    // MASTER FX — REVERB (DaisySP ReverbSc)
    // ══════════════════════════════════════════════════
    void setReverbActive(bool active);
    void setReverbFeedback(float feedback);   // 0.0-1.0 (room size / decay)
    void setReverbLpFreq(float hz);           // 200-12000 Hz (damp / color)
    void setReverbMix(float mix);             // 0.0-1.0 (dry/wet)
    void setReverb(bool active, float feedback = 0.85f, float lpFreq = 8000.0f, float mix = 0.3f); // all-in-one
    bool isReverbActive() const { return cachedReverbActive; }

    // ══════════════════════════════════════════════════
    // MASTER FX — CHORUS (DaisySP Chorus)
    // ══════════════════════════════════════════════════
    void setChorusActive(bool active);
    void setChorusRate(float hz);             // 0.1-10.0 Hz
    void setChorusDepth(float depth);         // 0.0-1.0
    void setChorusMix(float mix);             // 0.0-1.0
    void setChorus(bool active, float rate = 0.5f, float depth = 0.5f, float mix = 0.4f); // all-in-one
    bool isChorusActive() const { return cachedChorusActive; }

    // ══════════════════════════════════════════════════
    // MASTER FX — TREMOLO (DaisySP Tremolo)
    // ══════════════════════════════════════════════════
    void setTremoloActive(bool active);
    void setTremoloRate(float hz);            // 0.1-20.0 Hz
    void setTremoloDepth(float depth);        // 0.0-1.0
    void setTremolo(bool active, float rate = 4.0f, float depth = 0.7f); // all-in-one
    bool isTremoloActive() const { return cachedTremoloActive; }

    // ══════════════════════════════════════════════════
    // MASTER FX — WAVEFOLDER + LIMITER
    // ══════════════════════════════════════════════════
    void setWaveFolderGain(float gain);       // 1.0-10.0 (1.0=off, >1=fold)
    void setLimiterActive(bool active);       // Brick-wall 0dBFS limiter
    bool isLimiterActive() const { return cachedLimiterActive; }

    // ══════════════════════════════════════════════════
    // SIDECHAIN
    // ══════════════════════════════════════════════════
    void setSidechain(bool active, int sourceTrack, uint16_t destinationMask,
                      float amount, float attackMs, float releaseMs, float knee);
    void clearSidechain();
    
    // ══════════════════════════════════════════════════
    // PAD CONTROL (loop, reverse, pitch, stutter, scratch)
    // ══════════════════════════════════════════════════
    void setPadLoop(int padIndex, bool enabled);
    bool isPadLooping(int padIndex);
    void setReverseSample(int padIndex, bool reverse);
    void setTrackPitchShift(int padIndex, float pitch);
    void setStutter(int padIndex, bool active, int intervalMs = 100);
    void setScratchParams(int padIndex, bool active, float rate = 5.0f, float depth = 0.85f, 
                          float filterCutoff = 4000.0f, float crackle = 0.25f);
    void setTurntablismParams(int padIndex, bool active, bool autoMode = true, int mode = -1, 
                              int brakeMs = 350, int backspinMs = 450, float transformRate = 11.0f, 
                              float vinylNoise = 0.35f);
    
    // ══════════════════════════════════════════════════
    // SAMPLE TRANSFER (ESP32 PSRAM → STM32)
    // ══════════════════════════════════════════════════
    bool setSampleBuffer(int padIndex, int16_t* buffer, uint32_t length);
    bool transferSample(int padIndex, int16_t* buffer, uint32_t numSamples);
    void unloadSample(int padIndex);
    void unloadAllSamples();

    // ══════════════════════════════════════════════════
    // DAISY SD CARD FILE SYSTEM
    //   Read kit folders/files from Daisy's SD card.
    //   Daisy loads WAVs directly from SD → SDRAM.
    // ══════════════════════════════════════════════════
    bool sdListFolders(SdFolderListResponse& out);        // Get kit folder names
    bool sdListFiles(const char* folder, SdFileListResponse& out); // Get WAV files in folder
    bool sdGetFileInfo(const char* folder, const char* file, SdFileInfoResponse& out);
    bool sdLoadSample(int padIndex, const char* folder, const char* file); // Load WAV → pad slot
    bool sdLoadKit(const char* kitName, uint8_t startPad = 0, uint8_t maxPads = 16); // Load all WAVs in kit
    bool sdGetKitList(SdKitListResponse& out);            // Get available kit names
    bool sdGetStatus(SdStatusResponse& out);              // SD card health + loaded info
    void sdUnloadKit();                                    // Free all SDRAM pad slots
    bool sdGetLoadedKit(SdStatusResponse& out);           // What's currently loaded?
    void sdAbortLoad();                                    // Cancel ongoing load
    
    // ══════════════════════════════════════════════════
    // STATUS / METERING
    // ══════════════════════════════════════════════════
    float getTrackPeak(int track);
    float getMasterPeak();
    void getTrackPeaks(float* outPeaks, int count);
    bool requestPeaks();               // Request peaks from slave (updates cache)
    bool requestActiveVoices();        // Request active voice count from slave
    bool requestCpuLoad();             // Request CPU % from slave
    bool requestStatus();              // Request full StatusResponse from slave
    int getActiveVoices();             // Returns cached value
    float getCpuLoad();                // Returns cached value
    bool getStatusSnapshot(StatusResponse& out); // Fills struct with latest cached status
    bool ping(uint32_t& roundtripUs);
    void resetDSP();
    
    // ══════════════════════════════════════════════════
    // FILTER PRESETS (static, for UI)
    // ══════════════════════════════════════════════════
    static const FilterPreset* getFilterPreset(FilterType type);
    static const char* getFilterName(FilterType type);
    
    // Connection status
    bool isConnected() const { return stm32Connected; }
    uint32_t getSPIErrors() const { return spiErrorCount; }
    
    // Process (called from task loop — handles IRQ, peak polling)
    void process();
    
private:
    SPIClass* spi;
    uint16_t seqNumber;
    uint32_t spiErrorCount;
    bool stm32Connected;
    
    // TX/RX buffers
    uint8_t txBuffer[SPI_MAX_PAYLOAD];
    uint8_t rxBuffer[SPI_MAX_PAYLOAD];
    
    // Cached state (so getters don't need SPI round-trip)
    uint8_t cachedMasterVolume;
    uint8_t cachedSeqVolume;
    uint8_t cachedLiveVolume;
    float cachedLivePitch;

    // New FX cached state
    bool  cachedReverbActive;
    float cachedReverbFeedback;
    float cachedReverbLpFreq;
    float cachedReverbMix;
    bool  cachedChorusActive;
    float cachedChorusRate;
    float cachedChorusDepth;
    float cachedChorusMix;
    bool  cachedTremoloActive;
    float cachedTremoloRate;
    float cachedTremoloDepth;
    float cachedWaveFolderGain;
    bool  cachedLimiterActive;

    // Status cache
    StatusResponse cachedStatus;
    
    // Per-track/pad cached filter state
    FilterType cachedTrackFilter[MAX_AUDIO_TRACKS];
    bool trackFilterActive[MAX_AUDIO_TRACKS];
    FilterType cachedPadFilter[MAX_PADS];
    bool padFilterActive[MAX_PADS];
    
    // Per-track live FX cached state
    bool cachedTrackEchoActive[MAX_AUDIO_TRACKS];
    bool cachedTrackFlangerActive[MAX_AUDIO_TRACKS];
    bool cachedTrackCompActive[MAX_AUDIO_TRACKS];

    // Per-track mixer cached state
    uint8_t cachedTrackReverbSend[MAX_AUDIO_TRACKS];
    uint8_t cachedTrackDelaySend[MAX_AUDIO_TRACKS];
    uint8_t cachedTrackChorusSend[MAX_AUDIO_TRACKS];
    int8_t  cachedTrackPan[MAX_AUDIO_TRACKS];
    bool    cachedTrackMute[MAX_AUDIO_TRACKS];
    bool    cachedTrackSolo[MAX_AUDIO_TRACKS];
    
    // Pad loop cached state
    bool cachedPadLoop[MAX_PADS];
    
    // Peak levels (updated by requestPeaks)
    float cachedTrackPeaks[MAX_AUDIO_TRACKS];
    float cachedMasterPeak;
    uint32_t lastPeakRequest;
    
    // SPI mutex for thread safety (Core0 triggers vs Core1 process)
    SemaphoreHandle_t spiMutex;
    
    // SPI low-level
    bool sendCommand(uint8_t cmd, const void* payload, uint16_t payloadLen);
    bool sendAndReceive(uint8_t cmd, const void* payload, uint16_t payloadLen,
                        void* response, uint16_t responseLen);
    uint16_t crc16(const uint8_t* data, uint16_t len);
    void csLow();
    void csHigh();
    void syncPulse();
};

#endif // SPI_MASTER_H
