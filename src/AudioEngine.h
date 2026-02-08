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
static constexpr int MAX_PADS = 16;

// ============= NEW: Master Effects Constants =============
#define DELAY_BUFFER_SIZE 32768    // ~0.74s at 44100Hz (in PSRAM)
#define FLANGER_BUFFER_SIZE 512    // ~11.6ms at 44100Hz (in SRAM)
#define LFO_TABLE_SIZE 256         // Quarter-sine lookup table
#define PHASER_STAGES 4            // Cascaded allpass stages

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
  FILTER_RESONANT = 9
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

// Voice structure
struct Voice {
  int16_t* buffer;
  uint32_t position;
  uint32_t length;
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
  void triggerSampleSequencer(int padIndex, uint8_t velocity, uint8_t trackVolume = 100);
  void triggerSampleLive(int padIndex, uint8_t velocity);
  void stopSample(int padIndex);
  void stopAll();
  
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
  
  // Filter Presets
  static const FilterPreset* getFilterPreset(FilterType type);
  static const char* getFilterName(FilterType type);
  
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
  
private:
  Voice voices[MAX_VOICES];
  int16_t* sampleBuffers[16];
  uint32_t sampleLengths[16];
  
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
  
  // Per-track and per-pad filters
  FXParams trackFilters[MAX_AUDIO_TRACKS];
  bool trackFilterActive[MAX_AUDIO_TRACKS];
  FXParams padFilters[MAX_PADS];
  bool padFilterActive[MAX_PADS];
  
  // ============= NEW: Master Effects State =============
  float* delayBuffer;                           // PSRAM-allocated circular buffer
  float flangerBuffer[FLANGER_BUFFER_SIZE];     // SRAM small buffer
  
  DelayParams delayParams;
  PhaserParams phaserParams;
  FlangerParams flangerParams;
  CompressorParams compressorParams;
  
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
};

#endif // AUDIOENGINE_H
