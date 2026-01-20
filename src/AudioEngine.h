/*
 * AudioEngine.h
 * Motor d'àudio per ESP32-S3 Drum Machine
 * Gestiona I2S, samples i mixing de múltiples veus
 */

#ifndef AUDIOENGINE_H
#define AUDIOENGINE_H

#include <Arduino.h>
#include <driver/i2s.h>
#include <cmath>

#define MAX_VOICES 16
#define SAMPLE_RATE 44100
#define DMA_BUF_COUNT 4
#define DMA_BUF_LEN 128

// Filter types
enum FilterType {
  FILTER_NONE = 0,
  FILTER_LOWPASS = 1,
  FILTER_HIGHPASS = 2,
  FILTER_BANDPASS = 3,
  FILTER_NOTCH = 4
};

// Biquad filter coefficients
struct BiquadCoeffs {
  float b0, b1, b2;  // Numerator coefficients
  float a1, a2;      // Denominator coefficients (a0 normalized to 1)
};

// Filter state (for stereo)
struct FilterState {
  float x1, x2;  // Input history
  float y1, y2;  // Output history
};

// FX parameters
struct FXParams {
  FilterType filterType;
  float cutoff;          // Hz
  float resonance;       // Q factor
  uint8_t bitDepth;      // 4-16 bits
  float distortion;      // 0-100
  uint32_t sampleRate;   // Hz (for decimation)
  
  BiquadCoeffs coeffs;
  FilterState state;
  
  // Sample rate reducer state
  int32_t srHold;
  uint32_t srCounter;
};

// Voice structure
struct Voice {
  int16_t* buffer;        // Pointer to sample data in PSRAM
  uint32_t position;      // Current playback position
  uint32_t length;        // Sample length in samples
  bool active;            // Is voice playing?
  uint8_t velocity;       // MIDI velocity (0-127)
  float pitchShift;       // Pitch shift multiplier
  bool loop;              // Loop sample?
  uint32_t loopStart;     // Loop start point
  uint32_t loopEnd;       // Loop end point
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
  void stopSample(int padIndex);
  void stopAll();
  
  // Voice parameters
  void setPitch(int voiceIndex, float pitch);
  void setLoop(int voiceIndex, bool loop, uint32_t start = 0, uint32_t end = 0);
  
  // FX Control
  void setFilterType(FilterType type);
  void setFilterCutoff(float cutoff);
  void setFilterResonance(float resonance);
  void setBitDepth(uint8_t bits);
  void setDistortion(float amount);
  void setSampleRateReduction(uint32_t rate);
  
  // Volume Control
  void setMasterVolume(uint8_t volume); // 0-100
  uint8_t getMasterVolume();
  
  // Processing
  void process();
  
  // Statistics
  int getActiveVoices();
  float getCpuLoad();
  
  // Audio data capture for visualization
  void captureAudioData(uint8_t* spectrum, uint8_t* waveform);
  
private:
  Voice voices[MAX_VOICES];
  int16_t* sampleBuffers[16];  // Pointers to PSRAM sample data
  uint32_t sampleLengths[16];
  
  i2s_port_t i2sPort;
  int16_t mixBuffer[DMA_BUF_LEN * 2]; // Stereo buffer
  
  uint32_t processCount;
  uint32_t lastCpuCheck;
  float cpuLoad;
  
  FXParams fx;
  uint8_t masterVolume; // 0-100
  
  // Visualization buffers
  int16_t captureBuffer[256];
  uint8_t captureIndex;
  
  void fillBuffer(int16_t* buffer, size_t samples);
  int findFreeVoice();
  void resetVoice(int voiceIndex);
  
  // FX processing functions (optimized)
  void calculateBiquadCoeffs();
  inline int16_t applyFilter(int16_t input);
  inline int16_t applyBitCrush(int16_t input);
  inline int16_t applyDistortion(int16_t input);
  inline int16_t processFX(int16_t input);
};

#endif // AUDIOENGINE_H
