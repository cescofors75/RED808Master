/*
 * AudioEngine.h
 * Motor d'àudio per ESP32-S3 Drum Machine
 * Gestiona I2S, samples i mixing de múltiples veus
 * Master effects inspired by torvalds/AudioNoise (GPL-2.0)
 */

#ifndef AUDIOENGINE_H
#define AUDIOENGINE_H

#include <Arduino.h>
#include <driver/i2s.h>
#include <cmath>

#define MAX_VOICES 10
#define SAMPLE_RATE 44100
#define DMA_BUF_COUNT 6
#define DMA_BUF_LEN 128

// Constants for filter management
static constexpr int MAX_AUDIO_TRACKS = 16;
static constexpr int MAX_PADS = 24;  // 16 sequencer + 8 XTRA pads

// ============= NEW: Master Effects Constants =============
#define DELAY_BUFFER_SIZE 32768    // ~0.74s at 44100Hz (in PSRAM)
#define FLANGER_BUFFER_SIZE 512    // ~11.6ms at 44100Hz (in SRAM)
#define LFO_TABLE_SIZE 256         // Quarter-sine lookup table
#define PHASER_STAGES 4            // Cascaded allpass stages

// Per-track live FX (SLAVE controller)
#define TRACK_ECHO_SIZE 8820       // 200ms per-track echo at 44100Hz
#define TRACK_FLANGER_BUF 256      // ~5.8ms per-track flanger

// Filter types (10 classic types)
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

// Distortion modes (inspired by torvalds/AudioNoise distortion.h)
enum DistortionMode {
  DIST_SOFT = 0,    // x / (1 + |x|) - Torvalds limit_value, smooth analog
  DIST_HARD = 1,    // Hard clip at threshold
  DIST_TUBE = 2,    // Asymmetric exponential saturation (tube.h inspired)
  DIST_FUZZ = 3     // Extreme squared soft clip
};

// LFO waveform types (inspired by torvalds/AudioNoise lfo.h)
enum LFOWaveform {
  LFO_SINE = 0,
  LFO_TRIANGLE = 1,
  LFO_SAWTOOTH = 2
};

// Filter preset structure
struct FilterPreset {
  FilterType type;
  float cutoff;
  float resonance;
  float gain;
  const char* name;
};

// Biquad filter coefficients
struct BiquadCoeffs {
  float b0, b1, b2;
  float a1, a2;
};

// Filter state
struct FilterState {
  float x1, x2;
  float y1, y2;
};

// FX parameters (existing global filter/lofi chain)
struct FXParams {
  FilterType filterType;
  float cutoff;
  float resonance;
  float gain;
  uint8_t bitDepth;
  float distortion;
  uint32_t sampleRate;
  
  BiquadCoeffs coeffs;
  FilterState state;
  
  int32_t srHold;
  uint32_t srCounter;
};

// ============= NEW: LFO State (torvalds/AudioNoise lfo.h pattern) =============
struct LFOState {
  uint32_t phase;       // 32-bit phase accumulator
  uint32_t phaseInc;    // Phase increment per sample
  float depth;          // 0.0 - 1.0
  LFOWaveform waveform;
};

// ============= NEW: Delay/Echo (torvalds/AudioNoise echo.h) =============
struct DelayParams {
  bool active;
  float time;             // ms (10-750)
  float feedback;         // 0.0 - 0.95
  float mix;              // Dry/wet 0.0 - 1.0
  uint32_t delaySamples;  // Calculated from time
  uint32_t writePos;      // Circular buffer position
};

// ============= NEW: Phaser (torvalds/AudioNoise phaser.h) =============
struct PhaserParams {
  bool active;
  float rate;             // LFO rate Hz (0.1-5.0)
  float depth;            // 0.0 - 1.0
  float feedback;         // -0.9 to 0.9
  float lastOutput;       // Feedback state
  FilterState stages[PHASER_STAGES];
  LFOState lfo;
};

// ============= NEW: Flanger (torvalds/AudioNoise flanger.h) =============
struct FlangerParams {
  bool active;
  float rate;             // LFO rate Hz (0.1-5.0)
  float depth;            // 0.0 - 1.0 (max ~4ms delay)
  float feedback;         // -0.9 to 0.9
  float mix;              // Dry/wet 0.0 - 1.0
  uint32_t writePos;      // Circular buffer position
  LFOState lfo;
};

// ============= NEW: Compressor/Limiter =============
struct CompressorParams {
  bool active;
  float threshold;        // 0.0 - 1.0 (normalized amplitude)
  float ratio;            // 1.0 - 20.0
  float attackCoeff;      // Pre-calculated envelope coefficient
  float releaseCoeff;     // Pre-calculated envelope coefficient
  float makeupGain;       // Linear gain multiplier
  float envelope;         // Current envelope level (state)
};

// Per-track Echo/Delay state (for SLAVE controller)
struct TrackEchoState {
    bool active;
    float time;           // ms (10-200)
    float feedback;       // 0.0-0.9
    float mix;            // 0.0-1.0
    uint32_t delaySamples;
    uint32_t writePos;
};

// Per-track Flanger state (for SLAVE controller)
struct TrackFlangerState {
    bool active;
    float rate;           // Hz (0.1-5.0)
    float depth;          // 0.0-1.0
    float feedback;       // -0.9 to 0.9
    uint32_t writePos;
    LFOState lfo;
};

// Per-track Compressor state (for SLAVE controller)
struct TrackCompressorState {
    bool active;
    float threshold;      // 0.0-1.0 normalized
    float ratio;          // 1.0-20.0
    float attackCoeff;
    float releaseCoeff;
    float envelope;
};

// Scratch effect state (per-pad, vinyl scratch simulation)
struct ScratchState {
  float lfoPhase;       // LFO phase accumulator
  float lfoRate;        // Scratch oscillation rate (Hz)
  float depth;          // Scratch depth (amplitude 0.5-1.0)
  float lpState1;       // One-pole LP filter state 1
  float lpState2;       // One-pole LP filter state 2
  uint32_t noiseState;  // LFSR for vinyl crackle
  float filterCutoff;   // Vinyl tone filter cutoff
  float crackleAmount;  // Crackle intensity 0.0-1.0
};

// Turntablism effect state (per-pad, DJ turntable simulation)
struct TurntablismState {
  int mode;             // 0=normal, 1=brake, 2=backspin, 3=stutter
  uint32_t modeTimer;   // Samples remaining in current mode
  float gatePhase;      // Stutter gate LFO phase
  float lpState1;       // One-pole LP state 1
  float lpState2;       // One-pole LP state 2
  uint32_t noiseState;  // LFSR for vinyl noise
  bool autoMode;        // true=auto cycle, false=manual
  uint32_t brakeLen;    // Brake duration in samples
  uint32_t backspinLen; // Backspin duration in samples
  float transformRate;  // Transform stutter rate in Hz
  float vinylNoise;     // Vinyl noise amount 0.0-1.0
};

// Voice structure
struct Voice {
  int16_t* buffer;
  uint32_t position;
  uint32_t length;
  uint32_t maxLength;   // 0 = play full sample; >0 = cut after this many samples (note length)
  bool active;
  uint8_t velocity;
  uint8_t volume;
  float pitchShift;
  bool loop;
  uint32_t loopStart;
  uint32_t loopEnd;
  int padIndex;
  bool isLivePad;
  uint32_t startAge;
  FilterState filterState;
  float scratchPos;     // Float position for scratch/turntablism effects
};

class AudioEngine {
public:
  AudioEngine();
  ~AudioEngine();
  
  // Initialization
  bool begin(int bckPin, int wsPin, int dataPin);
  
  // Sample management
  bool setSampleBuffer(int padIndex, int16_t* buffer, uint32_t length);
  
  // Playback control
  void triggerSample(int padIndex, uint8_t velocity);
  void triggerSampleSequencer(int padIndex, uint8_t velocity, uint8_t trackVolume = 100, uint32_t maxSamples = 0);
  void triggerSampleLive(int padIndex, uint8_t velocity);
  void stopSample(int padIndex);
  void stopAll();
  
  // Live performance
  void setLivePitchShift(float pitch);
  float getLivePitchShift();
  
  // Voice parameters
  void setPitch(int voiceIndex, float pitch);
  void setLoop(int voiceIndex, bool loop, uint32_t start = 0, uint32_t end = 0);
  
  // FX Control (Global filter/lofi chain)
  void setFilterType(FilterType type);
  void setFilterCutoff(float cutoff);
  void setFilterResonance(float resonance);
  void setBitDepth(uint8_t bits);
  void setDistortion(float amount);
  void setDistortionMode(DistortionMode mode);
  void setSampleRateReduction(uint32_t rate);
  
  // ============= NEW: Master Effects Control =============
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
  
  // Per-Track Filter Management
  bool setTrackFilter(int track, FilterType type, float cutoff = 1000.0f, float resonance = 1.0f, float gain = 0.0f);
  void clearTrackFilter(int track);
  FilterType getTrackFilter(int track);
  int getActiveTrackFiltersCount();
  
  // Per-Pad (Live) Filter Management
  bool setPadFilter(int pad, FilterType type, float cutoff = 1000.0f, float resonance = 1.0f, float gain = 0.0f);
  void clearPadFilter(int pad);
  FilterType getPadFilter(int pad);
  int getActivePadFiltersCount();
  
  // Per-Pad/Track FX (Distortion + BitCrush per channel)
  void setPadDistortion(int pad, float amount, DistortionMode mode = DIST_SOFT);
  void setPadBitCrush(int pad, uint8_t bits);
  void clearPadFX(int pad);
  void setTrackDistortion(int track, float amount, DistortionMode mode = DIST_SOFT);
  void setTrackBitCrush(int track, uint8_t bits);
  void clearTrackFX(int track);
  
  // Per-track live FX (SLAVE controller - echo, flanger, compressor)
  void setTrackEcho(int track, bool active, float time = 100.0f, float feedback = 40.0f, float mix = 50.0f);
  void setTrackFlanger(int track, bool active, float rate = 50.0f, float depth = 50.0f, float feedback = 30.0f);
  void setTrackCompressor(int track, bool active, float threshold = -20.0f, float ratio = 4.0f);
  void clearTrackLiveFX(int track);
  bool getTrackEchoActive(int track) const;
  bool getTrackFlangerActive(int track) const;
  bool getTrackCompressorActive(int track) const;
  
  // Filter Presets
  static const FilterPreset* getFilterPreset(FilterType type);
  static const char* getFilterName(FilterType type);
  
  // Pad-level continuous loop (for XTRA pads)
  void setPadLoop(int padIndex, bool enabled);
  bool isPadLooping(int padIndex);
  
  // Reverse / Pitch Shift / Stutter
  void setReverseSample(int padIndex, bool reverse);
  void setTrackPitchShift(int padIndex, float pitch);
  void setStutter(int padIndex, bool active, int intervalMs = 100);
  
  // Scratch / Turntablism per-track control
  void setScratchParams(int padIndex, bool active, float rate = 5.0f, float depth = 0.85f, float filterCutoff = 4000.0f, float crackle = 0.25f);
  void setTurntablismParams(int padIndex, bool active, bool autoMode = true, int mode = -1, int brakeMs = 350, int backspinMs = 450, float transformRate = 11.0f, float vinylNoise = 0.35f);
  
  // Volume Control
  void setMasterVolume(uint8_t volume);
  uint8_t getMasterVolume();
  void setSequencerVolume(uint8_t volume);
  uint8_t getSequencerVolume();
  void setLiveVolume(uint8_t volume);
  uint8_t getLiveVolume();
  
  // Processing
  void process();
  
  // Statistics
  int getActiveVoices();
  float getCpuLoad();
  
  // Peak level tracking for VU meters (per-track + master)
  float getTrackPeak(int track);
  float getMasterPeak();
  void getTrackPeaks(float* outPeaks, int count);
  
private:
  Voice voices[MAX_VOICES];
  int16_t* sampleBuffers[MAX_PADS];
  uint32_t sampleLengths[MAX_PADS];
  
  // Peak level tracking
  volatile float trackPeaks[MAX_AUDIO_TRACKS];
  volatile float masterPeak;
  float trackPeakDecay[MAX_AUDIO_TRACKS];
  float masterPeakDecay;
  
  i2s_port_t i2sPort;
  int16_t mixBuffer[DMA_BUF_LEN * 2];
  int32_t mixAcc[DMA_BUF_LEN * 2];
  
  uint32_t processCount;
  uint32_t lastCpuCheck;
  float cpuLoad;
  uint32_t voiceAge;
  
  FXParams fx;
  DistortionMode distortionMode;
  uint8_t masterVolume;
  uint8_t sequencerVolume;
  uint8_t liveVolume;
  float livePitchShift;
  
  // Per-track and per-pad filters
  FXParams trackFilters[MAX_AUDIO_TRACKS];
  bool trackFilterActive[MAX_AUDIO_TRACKS];
  FXParams padFilters[MAX_PADS];
  bool padFilterActive[MAX_PADS];
  
  // Per-track and per-pad FX (distortion + bitcrush)
  DistortionMode trackDistortionMode[MAX_AUDIO_TRACKS];
  DistortionMode padDistortionMode[MAX_PADS];
  
  // Scratch / Turntablism per-pad state
  ScratchState scratchState[MAX_PADS];
  TurntablismState turntablismState[MAX_PADS];
  
  // Pad-level continuous loop (for XTRA pads)
  bool padLoopEnabled[MAX_PADS];
  
  // Reverse / Pitch / Stutter per-pad state
  bool sampleReversed[MAX_PADS];
  float trackPitchShift[MAX_PADS];
  bool stutterActive[MAX_PADS];
  int stutterInterval[MAX_PADS];  // ms
  
  // ============= NEW: Master Effects State =============
  float* delayBuffer;                           // PSRAM-allocated circular buffer
  float flangerBuffer[FLANGER_BUFFER_SIZE];     // SRAM small buffer
  
  DelayParams delayParams;
  PhaserParams phaserParams;
  FlangerParams flangerParams;
  CompressorParams compressorParams;
  
  // Per-track live FX state (SLAVE controller)
  TrackEchoState trackEcho[MAX_AUDIO_TRACKS];
  TrackFlangerState trackFlanger[MAX_AUDIO_TRACKS];
  TrackCompressorState trackComp[MAX_AUDIO_TRACKS];
  float* trackEchoBuffer[MAX_AUDIO_TRACKS];    // PSRAM per-track echo buffers (lazy alloc)
  float* trackFlangerBuffers;                   // PSRAM: 16 × TRACK_FLANGER_BUF
  float* trackFxInputBuf;                       // PSRAM: 16 × DMA_BUF_LEN
  
  static float lfoSineTable[LFO_TABLE_SIZE];    // Shared sine lookup
  static bool lfoTableInitialized;
  
  void fillBuffer(int16_t* buffer, size_t samples);
  int findFreeVoice();
  void resetVoice(int voiceIndex);
  
  // FX processing functions (existing)
  void calculateBiquadCoeffs();
  void calculateBiquadCoeffs(FXParams& fx);
  inline int16_t applyFilter(int16_t input);
  inline int16_t applyFilter(int16_t input, FXParams& fx);
  inline int16_t applyBitCrush(int16_t input);
  inline int16_t applyDistortion(int16_t input);
  inline int16_t processFX(int16_t input);
  
  // ============= NEW: Master Effects Processing =============
  void initLFOTable();
  void updateLFOPhaseInc(LFOState& lfo, float rateHz);
  float lfoTick(LFOState& lfo);
  inline float softClipKnee(float input);    // Smooth limiter with knee
  inline float processDelay(float input);     // Echo/delay
  inline float processPhaser(float input);    // 4-stage allpass phaser
  inline float processFlanger(float input);   // Modulated short delay
  inline float processCompressor(float input);// Dynamics compressor
  
  // Per-track live FX processing
  inline float processTrackEcho(int track, float input);
  inline float processTrackFlanger(int track, float input);
  inline float processTrackCompressor(int track, float input);
};

#endif // AUDIOENGINE_H
