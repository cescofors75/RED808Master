/*
 * AudioEngine.cpp
 * Implementació del motor d'àudio
 * Master effects inspired by torvalds/AudioNoise (GPL-2.0)
 * - Soft clipping: limit_value pattern from util.h
 * - Delay/Echo: circular buffer pattern from echo.h
 * - Phaser: cascaded allpass from phaser.h
 * - Flanger: modulated delay from flanger.h
 * - LFO: phase accumulator + sine LUT from lfo.h
 */

#include "AudioEngine.h"

// Static member initialization
float AudioEngine::lfoSineTable[LFO_TABLE_SIZE] = {0};
bool AudioEngine::lfoTableInitialized = false;

AudioEngine::AudioEngine() : i2sPort(I2S_NUM_0),
                             processCount(0), lastCpuCheck(0), cpuLoad(0.0f),
                             voiceAge(0), delayBuffer(nullptr) {
  // Initialize LFO sine lookup table (once, shared across instances)
  initLFOTable();
  
  // Initialize voices
  for (int i = 0; i < MAX_VOICES; i++) {
    resetVoice(i);
  }
  
  // Initialize sample buffers
  for (int i = 0; i < 16; i++) {
    sampleBuffers[i] = nullptr;
    sampleLengths[i] = 0;
  }
  
  // Initialize FX (existing global filter/lofi chain)
  fx.filterType = FILTER_NONE;
  fx.cutoff = 8000.0f;
  fx.resonance = 1.0f;
  fx.gain = 0.0f;
  fx.bitDepth = 16;
  fx.distortion = 0.0f;
  fx.sampleRate = SAMPLE_RATE;
  fx.state.x1 = fx.state.x2 = 0.0f;
  fx.state.y1 = fx.state.y2 = 0.0f;
  fx.srHold = 0;
  fx.srCounter = 0;
  distortionMode = DIST_SOFT;
  calculateBiquadCoeffs();
  
  // Initialize per-track and per-pad filters
  for (int i = 0; i < MAX_AUDIO_TRACKS; i++) {
    trackFilters[i].filterType = FILTER_NONE;
    trackFilters[i].cutoff = 1000.0f;
    trackFilters[i].resonance = 1.0f;
    trackFilters[i].gain = 0.0f;
    trackFilters[i].state.x1 = trackFilters[i].state.x2 = 0.0f;
    trackFilters[i].state.y1 = trackFilters[i].state.y2 = 0.0f;
    trackFilterActive[i] = false;
  }
  
  for (int i = 0; i < MAX_PADS; i++) {
    padFilters[i].filterType = FILTER_NONE;
    padFilters[i].cutoff = 1000.0f;
    padFilters[i].resonance = 1.0f;
    padFilters[i].gain = 0.0f;
    padFilters[i].state.x1 = padFilters[i].state.x2 = 0.0f;
    padFilters[i].state.y1 = padFilters[i].state.y2 = 0.0f;
    padFilterActive[i] = false;
  }
  
  // Initialize volume
  masterVolume = 100;
  sequencerVolume = 10;
  liveVolume = 80;
  
  // ============= Initialize NEW Master Effects =============
  
  // Delay/Echo - allocate buffer in PSRAM
  delayBuffer = (float*)ps_malloc(DELAY_BUFFER_SIZE * sizeof(float));
  if (delayBuffer) {
    memset(delayBuffer, 0, DELAY_BUFFER_SIZE * sizeof(float));
    Serial.printf("[AudioEngine] Delay buffer allocated: %d bytes in PSRAM\n", 
                  DELAY_BUFFER_SIZE * (int)sizeof(float));
  } else {
    Serial.println("[AudioEngine] WARNING: Failed to allocate delay buffer in PSRAM!");
  }
  delayParams.active = false;
  delayParams.time = 250.0f;
  delayParams.feedback = 0.3f;
  delayParams.mix = 0.3f;
  delayParams.delaySamples = (uint32_t)(250.0f * SAMPLE_RATE / 1000.0f);
  delayParams.writePos = 0;
  
  // Phaser
  phaserParams.active = false;
  phaserParams.rate = 0.5f;
  phaserParams.depth = 0.7f;
  phaserParams.feedback = 0.3f;
  phaserParams.lastOutput = 0.0f;
  for (int i = 0; i < PHASER_STAGES; i++) {
    phaserParams.stages[i] = {0, 0, 0, 0};
  }
  phaserParams.lfo.phase = 0;
  phaserParams.lfo.depth = 0.7f;
  phaserParams.lfo.waveform = LFO_SINE;
  updateLFOPhaseInc(phaserParams.lfo, 0.5f);
  
  // Flanger
  memset(flangerBuffer, 0, sizeof(flangerBuffer));
  flangerParams.active = false;
  flangerParams.rate = 0.3f;
  flangerParams.depth = 0.5f;
  flangerParams.feedback = 0.4f;
  flangerParams.mix = 0.5f;
  flangerParams.writePos = 0;
  flangerParams.lfo.phase = 0;
  flangerParams.lfo.depth = 0.5f;
  flangerParams.lfo.waveform = LFO_SINE;
  updateLFOPhaseInc(flangerParams.lfo, 0.3f);
  
  // Compressor
  compressorParams.active = false;
  compressorParams.threshold = 0.5f;
  compressorParams.ratio = 4.0f;
  compressorParams.attackCoeff = expf(-1.0f / (SAMPLE_RATE * 0.010f));   // 10ms
  compressorParams.releaseCoeff = expf(-1.0f / (SAMPLE_RATE * 0.100f));  // 100ms
  compressorParams.makeupGain = 1.0f;
  compressorParams.envelope = 0.0f;
  
  // Clear mix accumulator
  memset(mixAcc, 0, sizeof(mixAcc));
}

AudioEngine::~AudioEngine() {
  i2s_driver_uninstall(i2sPort);
  if (delayBuffer) {
    free(delayBuffer);
    delayBuffer = nullptr;
  }
}

// ============= LFO Initialization =============

void AudioEngine::initLFOTable() {
  if (lfoTableInitialized) return;
  // Full sine table (256 entries) - inspired by torvalds/AudioNoise lfo.h quarter-sine approach
  for (int i = 0; i < LFO_TABLE_SIZE; i++) {
    lfoSineTable[i] = sinf(2.0f * PI * (float)i / (float)LFO_TABLE_SIZE);
  }
  lfoTableInitialized = true;
  Serial.println("[AudioEngine] LFO sine table initialized");
}

void AudioEngine::updateLFOPhaseInc(LFOState& lfo, float rateHz) {
  // Phase increment per sample: rate * 2^32 / SAMPLE_RATE
  // Using 64-bit intermediate to avoid overflow
  lfo.phaseInc = (uint32_t)((double)rateHz * 4294967296.0 / (double)SAMPLE_RATE);
}

// LFO tick - returns value in range [-depth, +depth]
// Inspired by torvalds/AudioNoise lfo.h phase accumulator pattern
float AudioEngine::lfoTick(LFOState& lfo) {
  lfo.phase += lfo.phaseInc;
  
  // Extract table index from top 8 bits of 32-bit phase
  uint8_t idx = lfo.phase >> 24;
  
  switch (lfo.waveform) {
    case LFO_SINE:
      return lfoSineTable[idx] * lfo.depth;
      
    case LFO_TRIANGLE: {
      float t = (float)(lfo.phase >> 16) / 65536.0f;  // 0.0 - 1.0
      float tri = (t < 0.5f) ? (4.0f * t - 1.0f) : (3.0f - 4.0f * t);
      return tri * lfo.depth;
    }
    
    case LFO_SAWTOOTH: {
      float saw = 2.0f * (float)(lfo.phase >> 16) / 65536.0f - 1.0f;
      return saw * lfo.depth;
    }
    
    default:
      return 0.0f;
  }
}

bool AudioEngine::begin(int bckPin, int wsPin, int dataPin) {
  // I2S configuration para DAC externo
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = DMA_BUF_COUNT,
    .dma_buf_len = DMA_BUF_LEN,
    .use_apll = true,           // APLL = better audio clock accuracy
    .tx_desc_auto_clear = true,
    .fixed_mclk = 0
  };
  
  // I2S pin configuration
  i2s_pin_config_t pin_config = {
    .bck_io_num = bckPin,
    .ws_io_num = wsPin,
    .data_out_num = dataPin,
    .data_in_num = I2S_PIN_NO_CHANGE
  };
  
  // Install and start I2S driver
  esp_err_t err = i2s_driver_install(i2sPort, &i2s_config, 0, NULL);
  if (err != ESP_OK) {
    Serial.printf("I2S driver install failed: %d\n", err);
    return false;
  }
  
  err = i2s_set_pin(i2sPort, &pin_config);
  if (err != ESP_OK) {
    Serial.printf("I2S set pin failed: %d\n", err);
    return false;
  }
  
  // Set I2S clock
  i2s_set_clk(i2sPort, SAMPLE_RATE, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_STEREO);
  
  Serial.println("I2S External DAC initialized successfully");
  return true;
}

bool AudioEngine::setSampleBuffer(int padIndex, int16_t* buffer, uint32_t length) {
  if (padIndex < 0 || padIndex >= 16) return false;
  
  sampleBuffers[padIndex] = buffer;
  sampleLengths[padIndex] = length;
  
  Serial.printf("[AudioEngine] Sample buffer set: Pad %d, Buffer: %p, Length: %d samples\n", 
                padIndex, buffer, length);
  
  return true;
}

void AudioEngine::triggerSample(int padIndex, uint8_t velocity) {
  triggerSampleLive(padIndex, velocity);
}

void AudioEngine::triggerSampleSequencer(int padIndex, uint8_t velocity, uint8_t trackVolume) {
  if (padIndex < 0 || padIndex >= 16 || sampleBuffers[padIndex] == nullptr) return;
  
  int voiceIndex = findFreeVoice();
  if (voiceIndex < 0) return; // Voice stealing handled inside findFreeVoice
  
  voices[voiceIndex].buffer = sampleBuffers[padIndex];
  voices[voiceIndex].position = 0;
  voices[voiceIndex].length = sampleLengths[padIndex];
  voices[voiceIndex].active = true;
  voices[voiceIndex].velocity = velocity;
  voices[voiceIndex].volume = constrain((sequencerVolume * trackVolume) / 100, 0, 150);
  voices[voiceIndex].pitchShift = 1.0f;
  voices[voiceIndex].loop = false;
  voices[voiceIndex].padIndex = padIndex;
  voices[voiceIndex].isLivePad = false;
  voices[voiceIndex].startAge = ++voiceAge;
}

void AudioEngine::triggerSampleLive(int padIndex, uint8_t velocity) {
  if (padIndex < 0 || padIndex >= 16 || sampleBuffers[padIndex] == nullptr) return;
  
  int voiceIndex = findFreeVoice();
  if (voiceIndex < 0) return;
  
  voices[voiceIndex].buffer = sampleBuffers[padIndex];
  voices[voiceIndex].position = 0;
  voices[voiceIndex].length = sampleLengths[padIndex];
  voices[voiceIndex].active = true;
  voices[voiceIndex].velocity = velocity;
  voices[voiceIndex].volume = constrain((liveVolume * 120) / 100, 0, 180);
  voices[voiceIndex].pitchShift = 1.0f;
  voices[voiceIndex].loop = false;
  voices[voiceIndex].padIndex = padIndex;
  voices[voiceIndex].isLivePad = true;
  voices[voiceIndex].startAge = ++voiceAge;
}

void AudioEngine::stopSample(int padIndex) {
  // Stop all voices playing this sample
  for (int i = 0; i < MAX_VOICES; i++) {
    if (voices[i].active && voices[i].buffer == sampleBuffers[padIndex]) {
      voices[i].active = false;
    }
  }
}

void AudioEngine::stopAll() {
  for (int i = 0; i < MAX_VOICES; i++) {
    voices[i].active = false;
  }
}

void AudioEngine::setPitch(int voiceIndex, float pitch) {
  if (voiceIndex < 0 || voiceIndex >= MAX_VOICES) return;
  voices[voiceIndex].pitchShift = pitch;
}

void AudioEngine::setLoop(int voiceIndex, bool loop, uint32_t start, uint32_t end) {
  if (voiceIndex < 0 || voiceIndex >= MAX_VOICES) return;
  
  voices[voiceIndex].loop = loop;
  voices[voiceIndex].loopStart = start;
  voices[voiceIndex].loopEnd = end > 0 ? end : voices[voiceIndex].length;
}

void IRAM_ATTR AudioEngine::process() {
  // Fill mix buffer
  fillBuffer(mixBuffer, DMA_BUF_LEN);
  
  // Write to I2S External DAC
  size_t bytes_written;
  i2s_write(i2sPort, mixBuffer, DMA_BUF_LEN * 4, &bytes_written, portMAX_DELAY);
  
  // Update CPU load calculation (lightweight, no serial)
  processCount++;
  uint32_t now = millis();
  if (now - lastCpuCheck > 1000) {
    cpuLoad = (processCount * DMA_BUF_LEN * 1000.0f) / (SAMPLE_RATE * (now - lastCpuCheck));
    processCount = 0;
    lastCpuCheck = now;
  }
}

void IRAM_ATTR AudioEngine::fillBuffer(int16_t* buffer, size_t samples) {
  // Clear output buffer and accumulator
  memset(buffer, 0, samples * sizeof(int16_t) * 2);
  memset(mixAcc, 0, samples * sizeof(int32_t) * 2);
  
  // Mix all active voices
  for (int v = 0; v < MAX_VOICES; v++) {
    if (!voices[v].active) continue;
    
    Voice& voice = voices[v];
    
    for (size_t i = 0; i < samples; i++) {
      if (voice.position >= voice.length) {
        if (voice.loop && voice.loopEnd > voice.loopStart) {
          voice.position = voice.loopStart;
        } else {
          voice.active = false;
          break;
        }
      }
      
      // Get sample and apply velocity + volume in one step
      int32_t scaled = ((int32_t)voice.buffer[voice.position] * voice.velocity * voice.volume) / 12700;
      
      // Apply per-pad or per-track filter if active (using per-voice state)
      int16_t filtered = (int16_t)constrain(scaled, -32768, 32767);
      if (voice.padIndex >= 0 && voice.padIndex < MAX_PADS) {
        if (voice.isLivePad && padFilterActive[voice.padIndex]) {
          float x = (float)filtered;
          float y = padFilters[voice.padIndex].coeffs.b0 * x + voice.filterState.x1;
          voice.filterState.x1 = padFilters[voice.padIndex].coeffs.b1 * x - padFilters[voice.padIndex].coeffs.a1 * y + voice.filterState.x2;
          voice.filterState.x2 = padFilters[voice.padIndex].coeffs.b2 * x - padFilters[voice.padIndex].coeffs.a2 * y;
          if (y > 32767.0f) y = 32767.0f;
          else if (y < -32768.0f) y = -32768.0f;
          filtered = (int16_t)y;
        } else if (!voice.isLivePad && voice.padIndex < MAX_AUDIO_TRACKS && trackFilterActive[voice.padIndex]) {
          float x = (float)filtered;
          float y = trackFilters[voice.padIndex].coeffs.b0 * x + voice.filterState.x1;
          voice.filterState.x1 = trackFilters[voice.padIndex].coeffs.b1 * x - trackFilters[voice.padIndex].coeffs.a1 * y + voice.filterState.x2;
          voice.filterState.x2 = trackFilters[voice.padIndex].coeffs.b2 * x - trackFilters[voice.padIndex].coeffs.a2 * y;
          if (y > 32767.0f) y = 32767.0f;
          else if (y < -32768.0f) y = -32768.0f;
          filtered = (int16_t)y;
        }
      }
      
      // Mix to accumulator (stereo - mono source)
      mixAcc[i * 2] += filtered;
      mixAcc[i * 2 + 1] += filtered;
      
      voice.position++;
    }
  }
  
  // Check which FX chains are active
  const bool hasOldFX = (fx.distortion > 0.1f) || (fx.filterType != FILTER_NONE) || 
                        (fx.sampleRate < SAMPLE_RATE) || (fx.bitDepth < 16);
  const bool hasNewFX = delayParams.active || phaserParams.active || 
                        flangerParams.active || compressorParams.active;
  
  // Process mono signal with master volume, soft clipping, and FX chains
  for (size_t i = 0; i < samples; i++) {
    // Mono from accumulator (L=R for drum samples)
    int32_t val = (mixAcc[i * 2] * masterVolume) / 100;
    
    // Normalize to float [-1.0, 1.0] range for processing
    float fval = (float)val / 32768.0f;
    
    // Soft clipping with knee (replaces old hard clamp)
    // Inspired by torvalds/AudioNoise limit_value: x / (1 + |x|)
    fval = softClipKnee(fval);
    
    // Convert back to int16 for legacy FX chain
    int16_t sample = (int16_t)(fval * 32767.0f);
    
    // Apply legacy FX chain (distortion, filter, SR reduction, bitcrush)
    if (hasOldFX) {
      sample = processFX(sample);
    }
    
    // Apply NEW master effects chain (in float domain for precision)
    if (hasNewFX) {
      float fs = (float)sample / 32768.0f;
      
      // Phaser (4-stage cascaded allpass with LFO)
      if (phaserParams.active) fs = processPhaser(fs);
      
      // Flanger (short modulated delay)
      if (flangerParams.active) fs = processFlanger(fs);
      
      // Delay/Echo (longer delay with feedback)
      if (delayParams.active) fs = processDelay(fs);
      
      // Compressor/Limiter (dynamics control - last in chain)
      if (compressorParams.active) fs = processCompressor(fs);
      
      // Final safety limiter (Torvalds limit_value)
      fs = fs / (1.0f + fabsf(fs));
      fs *= 2.0f;  // Compensate limit_value gain loss
      
      sample = (int16_t)constrain((int32_t)(fs * 32767.0f), -32768, 32767);
    }
    
    // Write stereo output (mono source)
    buffer[i * 2] = sample;
    buffer[i * 2 + 1] = sample;
  }
}

int AudioEngine::findFreeVoice() {
  // 1. Look for inactive voice
  for (int i = 0; i < MAX_VOICES; i++) {
    if (!voices[i].active) return i;
  }
  
  // 2. Steal oldest voice (lowest startAge)
  int oldest = 0;
  uint32_t oldestAge = voices[0].startAge;
  for (int i = 1; i < MAX_VOICES; i++) {
    if (voices[i].startAge < oldestAge) {
      oldestAge = voices[i].startAge;
      oldest = i;
    }
  }
  return oldest;
}

void AudioEngine::resetVoice(int voiceIndex) {
  voices[voiceIndex].buffer = nullptr;
  voices[voiceIndex].position = 0;
  voices[voiceIndex].length = 0;
  voices[voiceIndex].active = false;
  voices[voiceIndex].velocity = 127;
  voices[voiceIndex].volume = 100;
  voices[voiceIndex].pitchShift = 1.0f;
  voices[voiceIndex].loop = false;
  voices[voiceIndex].loopStart = 0;
  voices[voiceIndex].loopEnd = 0;
  voices[voiceIndex].padIndex = -1;
  voices[voiceIndex].isLivePad = false;
  voices[voiceIndex].startAge = 0;
  voices[voiceIndex].filterState = {0.0f, 0.0f, 0.0f, 0.0f};
}

// ============= FX IMPLEMENTATION =============

void AudioEngine::setFilterType(FilterType type) {
  fx.filterType = type;
  calculateBiquadCoeffs();
}

void AudioEngine::setFilterCutoff(float cutoff) {
  fx.cutoff = constrain(cutoff, 100.0f, 16000.0f);
  calculateBiquadCoeffs();
}

void AudioEngine::setFilterResonance(float resonance) {
  fx.resonance = constrain(resonance, 0.5f, 20.0f);
  calculateBiquadCoeffs();
}

void AudioEngine::setBitDepth(uint8_t bits) {
  fx.bitDepth = constrain(bits, 4, 16);
}

void AudioEngine::setDistortion(float amount) {
  fx.distortion = constrain(amount, 0.0f, 100.0f);
}

void AudioEngine::setDistortionMode(DistortionMode mode) {
  distortionMode = mode;
  Serial.printf("[AudioEngine] Distortion mode: %d\n", mode);
}

void AudioEngine::setSampleRateReduction(uint32_t rate) {
  fx.sampleRate = constrain(rate, 8000, SAMPLE_RATE);
  fx.srCounter = 0;
}

// Volume Control
void AudioEngine::setMasterVolume(uint8_t volume) {
  masterVolume = constrain(volume, 0, 150);
}

uint8_t AudioEngine::getMasterVolume() {
  return masterVolume;
}

void AudioEngine::setSequencerVolume(uint8_t volume) {
  sequencerVolume = constrain(volume, 0, 150);
}

uint8_t AudioEngine::getSequencerVolume() {
  return sequencerVolume;
}

void AudioEngine::setLiveVolume(uint8_t volume) {
  liveVolume = constrain(volume, 0, 150);
}

uint8_t AudioEngine::getLiveVolume() {
  return liveVolume;
}

// Biquad filter coefficient calculation (optimized)
void AudioEngine::calculateBiquadCoeffs() {
  if (fx.filterType == FILTER_NONE) return;
  
  float omega = 2.0f * PI * fx.cutoff / SAMPLE_RATE;
  float sn = sinf(omega);
  float cs = cosf(omega);
  float alpha = sn / (2.0f * fx.resonance);
  
  switch (fx.filterType) {
    case FILTER_LOWPASS:
      fx.coeffs.b0 = (1.0f - cs) / 2.0f;
      fx.coeffs.b1 = 1.0f - cs;
      fx.coeffs.b2 = (1.0f - cs) / 2.0f;
      fx.coeffs.a1 = -2.0f * cs;
      fx.coeffs.a2 = 1.0f - alpha;
      break;
      
    case FILTER_HIGHPASS:
      fx.coeffs.b0 = (1.0f + cs) / 2.0f;
      fx.coeffs.b1 = -(1.0f + cs);
      fx.coeffs.b2 = (1.0f + cs) / 2.0f;
      fx.coeffs.a1 = -2.0f * cs;
      fx.coeffs.a2 = 1.0f - alpha;
      break;
      
    case FILTER_BANDPASS:
      fx.coeffs.b0 = alpha;
      fx.coeffs.b1 = 0.0f;
      fx.coeffs.b2 = -alpha;
      fx.coeffs.a1 = -2.0f * cs;
      fx.coeffs.a2 = 1.0f - alpha;
      break;
      
    case FILTER_NOTCH:
      fx.coeffs.b0 = 1.0f;
      fx.coeffs.b1 = -2.0f * cs;
      fx.coeffs.b2 = 1.0f;
      fx.coeffs.a1 = -2.0f * cs;
      fx.coeffs.a2 = 1.0f - alpha;
      break;
      
    default:
      break;
  }
  
  // Normalize by a0
  float a0 = 1.0f + alpha;
  fx.coeffs.b0 /= a0;
  fx.coeffs.b1 /= a0;
  fx.coeffs.b2 /= a0;
  fx.coeffs.a1 /= a0;
  fx.coeffs.a2 /= a0;
}

// Biquad filter processing (Direct Form II Transposed - optimized)
inline int16_t AudioEngine::applyFilter(int16_t input) {
  if (fx.filterType == FILTER_NONE) return input;
  
  float x = (float)input;
  float y = fx.coeffs.b0 * x + fx.state.x1;
  
  fx.state.x1 = fx.coeffs.b1 * x - fx.coeffs.a1 * y + fx.state.x2;
  fx.state.x2 = fx.coeffs.b2 * x - fx.coeffs.a2 * y;
  
  // Clamp to prevent overflow
  if (y > 32767.0f) y = 32767.0f;
  else if (y < -32768.0f) y = -32768.0f;
  
  return (int16_t)y;
}

// Bit crusher (super fast)
inline int16_t AudioEngine::applyBitCrush(int16_t input) {
  if (fx.bitDepth >= 16) return input;
  
  int shift = 16 - fx.bitDepth;
  return (input >> shift) << shift;
}

// Distortion with multiple modes (inspired by torvalds/AudioNoise distortion.h)
inline int16_t AudioEngine::applyDistortion(int16_t input) {
  if (fx.distortion < 0.1f) return input;
  
  float x = (float)input / 32768.0f;
  float amount = fx.distortion / 100.0f;
  
  // Drive boost
  x *= (1.0f + amount * 3.0f);
  
  switch (distortionMode) {
    case DIST_SOFT:
      // Torvalds limit_value: x / (1 + |x|) - smooth analog saturation
      x = x / (1.0f + fabsf(x));
      break;
      
    case DIST_HARD:
      // Hard clip at threshold
      if (x > 1.0f) x = 1.0f;
      else if (x < -1.0f) x = -1.0f;
      break;
      
    case DIST_TUBE: {
      // Asymmetric exponential saturation (torvalds/AudioNoise tube.h inspired)
      // Positive half: soft compression, negative half: harder clip
      if (x >= 0.0f) {
        x = 1.0f - expf(-x);           // Exponential saturation
      } else {
        x = -(1.0f - expf(x * 1.2f));  // Slightly harder on negative half
      }
      break;
    }
    
    case DIST_FUZZ:
      // Extreme: double soft clip for heavy saturation
      x = x / (1.0f + fabsf(x));
      x *= 2.0f;
      x = x / (1.0f + fabsf(x));
      break;
      
    default:
      x = x / (1.0f + fabsf(x));
      break;
  }
  
  return (int16_t)(x * 32768.0f);
}

// Complete FX chain (optimized order)
inline int16_t AudioEngine::processFX(int16_t input) {
  int16_t output = input;
  
  // 1. Distortion (before filtering for analog character)
  if (fx.distortion > 0.1f) {
    output = applyDistortion(output);
  }
  
  // 2. Filter
  if (fx.filterType != FILTER_NONE) {
    output = applyFilter(output);
  }
  
  // 3. Sample rate reduction (decimation)
  if (fx.sampleRate < SAMPLE_RATE) {
    uint32_t decimation = SAMPLE_RATE / fx.sampleRate;
    if (fx.srCounter++ >= decimation) {
      fx.srHold = output;
      fx.srCounter = 0;
    }
    output = fx.srHold;
  }
  
  // 4. Bit crush (last for lo-fi effect)
  if (fx.bitDepth < 16) {
    output = applyBitCrush(output);
  }
  
  return output;
}

int AudioEngine::getActiveVoices() {
  int count = 0;
  for (int i = 0; i < MAX_VOICES; i++) {
    if (voices[i].active) count++;
  }
  return count;
}

float AudioEngine::getCpuLoad() {
  return cpuLoad * 100.0f;
}

// ============= NEW: Soft Clip with Knee =============
// Linear below ±0.9, smooth Torvalds-style limiting above
// Preserves dynamics for normal signals, only clips peaks
inline float AudioEngine::softClipKnee(float x) {
  const float knee = 0.9f;
  if (x > knee) {
    float excess = x - knee;
    return knee + (1.0f - knee) * excess / (1.0f + excess * 10.0f);
  } else if (x < -knee) {
    float excess = -x - knee;
    return -(knee + (1.0f - knee) * excess / (1.0f + excess * 10.0f));
  }
  return x;
}

// ============= NEW: Delay/Echo Processing =============
// Inspired by torvalds/AudioNoise echo.h circular buffer pattern

inline float AudioEngine::processDelay(float input) {
  if (!delayBuffer) return input;
  
  // Read from delay buffer (circular)
  uint32_t readPos = (delayParams.writePos + DELAY_BUFFER_SIZE - delayParams.delaySamples) 
                     % DELAY_BUFFER_SIZE;
  float delayed = delayBuffer[readPos];
  
  // Write input + feedback to delay buffer
  float writeVal = input + delayed * delayParams.feedback;
  // Prevent feedback runaway with Torvalds limit_value
  writeVal = writeVal / (1.0f + fabsf(writeVal));
  delayBuffer[delayParams.writePos] = writeVal;
  delayParams.writePos = (delayParams.writePos + 1) % DELAY_BUFFER_SIZE;
  
  // Mix dry and wet
  return input * (1.0f - delayParams.mix) + delayed * delayParams.mix;
}

// ============= NEW: Phaser Processing =============
// 4-stage cascaded allpass, LFO-modulated
// Inspired by torvalds/AudioNoise phaser.h

inline float AudioEngine::processPhaser(float input) {
  // Get LFO value (0.0 to 1.0 range for frequency sweep)
  float lfoVal = (lfoTick(phaserParams.lfo) + 1.0f) * 0.5f;
  
  // Map LFO to allpass coefficient
  // Sweep center frequency from ~200Hz to ~4000Hz
  float minFreq = 200.0f;
  float maxFreq = 4000.0f;
  float freq = minFreq + (maxFreq - minFreq) * lfoVal * phaserParams.depth;
  
  // 1st-order allpass coefficient from frequency
  // coeff = (1 - tan(pi*f/sr)) / (1 + tan(pi*f/sr))
  float omega = PI * freq / SAMPLE_RATE;
  // Fast tan approximation for small angles: tan(x) ≈ x + x³/3
  float tn = omega + (omega * omega * omega) * 0.333333f;
  float coeff = (1.0f - tn) / (1.0f + tn);
  
  // Mix feedback into input
  float x = input + phaserParams.lastOutput * phaserParams.feedback;
  
  // Cascade through 4 allpass stages
  // Each stage: y = coeff * x + x1 - coeff * y1 (1st-order allpass)
  for (int s = 0; s < PHASER_STAGES; s++) {
    float y = coeff * x + phaserParams.stages[s].x1 - coeff * phaserParams.stages[s].y1;
    phaserParams.stages[s].x1 = x;
    phaserParams.stages[s].y1 = y;
    x = y;
  }
  
  phaserParams.lastOutput = x;
  
  // Mix original and phased signal (50/50 for classic phaser)
  return (input + x) * 0.5f;
}

// ============= NEW: Flanger Processing =============
// Short LFO-modulated delay with feedback
// Inspired by torvalds/AudioNoise flanger.h

inline float AudioEngine::processFlanger(float input) {
  // Write to flanger buffer
  flangerBuffer[flangerParams.writePos] = input;
  
  // Get LFO-modulated delay in samples (0 to ~4ms = 0 to ~176 samples)
  float lfoVal = (lfoTick(flangerParams.lfo) + 1.0f) * 0.5f;  // 0.0 - 1.0
  float delaySamplesF = lfoVal * flangerParams.depth * 176.0f + 1.0f;  // 1 to 177
  
  // Interpolated read from flanger buffer (linear interpolation)
  uint32_t delayInt = (uint32_t)delaySamplesF;
  float frac = delaySamplesF - (float)delayInt;
  
  uint32_t readPos1 = (flangerParams.writePos + FLANGER_BUFFER_SIZE - delayInt) 
                      % FLANGER_BUFFER_SIZE;
  uint32_t readPos2 = (readPos1 + FLANGER_BUFFER_SIZE - 1) % FLANGER_BUFFER_SIZE;
  
  float delayed = flangerBuffer[readPos1] * (1.0f - frac) + flangerBuffer[readPos2] * frac;
  
  // Add feedback to the written sample
  flangerBuffer[flangerParams.writePos] += delayed * flangerParams.feedback;
  
  // Advance write position
  flangerParams.writePos = (flangerParams.writePos + 1) % FLANGER_BUFFER_SIZE;
  
  // Mix dry and wet
  return input * (1.0f - flangerParams.mix) + delayed * flangerParams.mix;
}

// ============= NEW: Compressor/Limiter Processing =============

inline float AudioEngine::processCompressor(float input) {
  // Envelope follower (peak detection)
  float absInput = fabsf(input);
  if (absInput > compressorParams.envelope) {
    compressorParams.envelope = compressorParams.attackCoeff * compressorParams.envelope 
                               + (1.0f - compressorParams.attackCoeff) * absInput;
  } else {
    compressorParams.envelope = compressorParams.releaseCoeff * compressorParams.envelope 
                               + (1.0f - compressorParams.releaseCoeff) * absInput;
  }
  
  // Calculate gain reduction
  float gain = 1.0f;
  if (compressorParams.envelope > compressorParams.threshold) {
    // Ratio compression
    float excess = compressorParams.envelope / compressorParams.threshold;
    float targetGain = compressorParams.threshold * powf(excess, 1.0f / compressorParams.ratio - 1.0f);
    gain = targetGain;
  }
  
  return input * gain * compressorParams.makeupGain;
}

// ============= NEW: Master Effects Setters =============

// --- Delay/Echo ---
void AudioEngine::setDelayActive(bool active) {
  delayParams.active = active;
  if (active && delayBuffer) {
    memset(delayBuffer, 0, DELAY_BUFFER_SIZE * sizeof(float));
    delayParams.writePos = 0;
  }
  Serial.printf("[AudioEngine] Delay: %s\n", active ? "ON" : "OFF");
}

void AudioEngine::setDelayTime(float ms) {
  delayParams.time = constrain(ms, 10.0f, 750.0f);
  delayParams.delaySamples = (uint32_t)(delayParams.time * SAMPLE_RATE / 1000.0f);
  if (delayParams.delaySamples >= DELAY_BUFFER_SIZE) {
    delayParams.delaySamples = DELAY_BUFFER_SIZE - 1;
  }
  Serial.printf("[AudioEngine] Delay time: %.0f ms (%d samples)\n", delayParams.time, delayParams.delaySamples);
}

void AudioEngine::setDelayFeedback(float feedback) {
  delayParams.feedback = constrain(feedback, 0.0f, 0.95f);
}

void AudioEngine::setDelayMix(float mix) {
  delayParams.mix = constrain(mix, 0.0f, 1.0f);
}

// --- Phaser ---
void AudioEngine::setPhaserActive(bool active) {
  phaserParams.active = active;
  if (active) {
    phaserParams.lastOutput = 0.0f;
    for (int i = 0; i < PHASER_STAGES; i++) {
      phaserParams.stages[i] = {0, 0, 0, 0};
    }
  }
  Serial.printf("[AudioEngine] Phaser: %s\n", active ? "ON" : "OFF");
}

void AudioEngine::setPhaserRate(float hz) {
  phaserParams.rate = constrain(hz, 0.05f, 5.0f);
  phaserParams.lfo.depth = phaserParams.depth;
  updateLFOPhaseInc(phaserParams.lfo, phaserParams.rate);
}

void AudioEngine::setPhaserDepth(float depth) {
  phaserParams.depth = constrain(depth, 0.0f, 1.0f);
  phaserParams.lfo.depth = phaserParams.depth;
}

void AudioEngine::setPhaserFeedback(float feedback) {
  phaserParams.feedback = constrain(feedback, -0.9f, 0.9f);
}

// --- Flanger ---
void AudioEngine::setFlangerActive(bool active) {
  flangerParams.active = active;
  if (active) {
    memset(flangerBuffer, 0, sizeof(flangerBuffer));
    flangerParams.writePos = 0;
  }
  Serial.printf("[AudioEngine] Flanger: %s\n", active ? "ON" : "OFF");
}

void AudioEngine::setFlangerRate(float hz) {
  flangerParams.rate = constrain(hz, 0.05f, 5.0f);
  updateLFOPhaseInc(flangerParams.lfo, flangerParams.rate);
}

void AudioEngine::setFlangerDepth(float depth) {
  flangerParams.depth = constrain(depth, 0.0f, 1.0f);
  flangerParams.lfo.depth = flangerParams.depth;
}

void AudioEngine::setFlangerFeedback(float feedback) {
  flangerParams.feedback = constrain(feedback, -0.9f, 0.9f);
}

void AudioEngine::setFlangerMix(float mix) {
  flangerParams.mix = constrain(mix, 0.0f, 1.0f);
}

// --- Compressor ---
void AudioEngine::setCompressorActive(bool active) {
  compressorParams.active = active;
  if (active) {
    compressorParams.envelope = 0.0f;
  }
  Serial.printf("[AudioEngine] Compressor: %s\n", active ? "ON" : "OFF");
}

void AudioEngine::setCompressorThreshold(float threshold) {
  // Input in dB (-60 to 0), convert to linear
  float db = constrain(threshold, -60.0f, 0.0f);
  compressorParams.threshold = powf(10.0f, db / 20.0f);
}

void AudioEngine::setCompressorRatio(float ratio) {
  compressorParams.ratio = constrain(ratio, 1.0f, 20.0f);
}

void AudioEngine::setCompressorAttack(float ms) {
  float t = constrain(ms, 0.1f, 100.0f);
  compressorParams.attackCoeff = expf(-1.0f / (SAMPLE_RATE * t / 1000.0f));
}

void AudioEngine::setCompressorRelease(float ms) {
  float t = constrain(ms, 10.0f, 1000.0f);
  compressorParams.releaseCoeff = expf(-1.0f / (SAMPLE_RATE * t / 1000.0f));
}

void AudioEngine::setCompressorMakeupGain(float db) {
  float d = constrain(db, 0.0f, 24.0f);
  compressorParams.makeupGain = powf(10.0f, d / 20.0f);
}

// ============= PER-TRACK FILTER MANAGEMENT =============

bool AudioEngine::setTrackFilter(int track, FilterType type, float cutoff, float resonance, float gain) {
  if (track < 0 || track >= MAX_AUDIO_TRACKS) return false;
  
  // Check if enabling a new filter would exceed the limit of 8
  if (type != FILTER_NONE && !trackFilterActive[track]) {
    if (getActiveTrackFiltersCount() >= 8) {
      Serial.println("[AudioEngine] ERROR: Max 8 track filters active");
      return false;
    }
  }
  
  trackFilters[track].filterType = type;
  trackFilters[track].cutoff = constrain(cutoff, 100.0f, 16000.0f);
  trackFilters[track].resonance = constrain(resonance, 0.5f, 20.0f);
  trackFilters[track].gain = constrain(gain, -12.0f, 12.0f);
  trackFilterActive[track] = (type != FILTER_NONE);
  
  // Calculate coefficients for this filter
  if (type != FILTER_NONE) {
    calculateBiquadCoeffs(trackFilters[track]);
    Serial.printf("[AudioEngine] Track %d filter ACTIVE: %s (cutoff: %.1f Hz, Q: %.2f, gain: %.1f dB)\n",
                  track, getFilterName(type), cutoff, resonance, gain);
  } else {
    Serial.printf("[AudioEngine] Track %d filter CLEARED\n", track);
  }
  return true;
}

void AudioEngine::clearTrackFilter(int track) {
  if (track < 0 || track >= MAX_AUDIO_TRACKS) return;
  trackFilters[track].filterType = FILTER_NONE;
  trackFilterActive[track] = false;
  Serial.printf("[AudioEngine] Track %d filter cleared\n", track);
}

FilterType AudioEngine::getTrackFilter(int track) {
  if (track < 0 || track >= MAX_AUDIO_TRACKS) return FILTER_NONE;
  return trackFilters[track].filterType;
}

int AudioEngine::getActiveTrackFiltersCount() {
  int count = 0;
  for (int i = 0; i < MAX_AUDIO_TRACKS; i++) {
    if (trackFilterActive[i]) count++;
  }
  return count;
}

// ============= PER-PAD FILTER MANAGEMENT =============

bool AudioEngine::setPadFilter(int pad, FilterType type, float cutoff, float resonance, float gain) {
  if (pad < 0 || pad >= MAX_PADS) return false;
  
  // Check if enabling a new filter would exceed the limit of 8
  if (type != FILTER_NONE && !padFilterActive[pad]) {
    if (getActivePadFiltersCount() >= 8) {
      Serial.println("[AudioEngine] ERROR: Max 8 pad filters active");
      return false;
    }
  }
  
  padFilters[pad].filterType = type;
  padFilters[pad].cutoff = constrain(cutoff, 100.0f, 16000.0f);
  padFilters[pad].resonance = constrain(resonance, 0.5f, 20.0f);
  padFilters[pad].gain = constrain(gain, -12.0f, 12.0f);
  padFilterActive[pad] = (type != FILTER_NONE);
  
  // Calculate coefficients for this filter
  if (type != FILTER_NONE) {
    calculateBiquadCoeffs(padFilters[pad]);
  }
  
  Serial.printf("[AudioEngine] Pad %d filter: %s (cutoff: %.1f Hz, Q: %.2f, gain: %.1f dB)\n",
                pad, getFilterName(type), cutoff, resonance, gain);
  return true;
}

void AudioEngine::clearPadFilter(int pad) {
  if (pad < 0 || pad >= MAX_PADS) return;
  padFilters[pad].filterType = FILTER_NONE;
  padFilterActive[pad] = false;
  Serial.printf("[AudioEngine] Pad %d filter cleared\n", pad);
}

FilterType AudioEngine::getPadFilter(int pad) {
  if (pad < 0 || pad >= MAX_PADS) return FILTER_NONE;
  return padFilters[pad].filterType;
}

int AudioEngine::getActivePadFiltersCount() {
  int count = 0;
  for (int i = 0; i < MAX_PADS; i++) {
    if (padFilterActive[i]) count++;
  }
  return count;
}

// ============= FILTER PRESETS =============

const FilterPreset* AudioEngine::getFilterPreset(FilterType type) {
  static const FilterPreset presets[] = {
    {FILTER_NONE, 0.0f, 1.0f, 0.0f, "None"},
    {FILTER_LOWPASS, 800.0f, 3.0f, 0.0f, "Low Pass"},
    {FILTER_HIGHPASS, 800.0f, 3.0f, 0.0f, "High Pass"},
    {FILTER_BANDPASS, 1200.0f, 4.0f, 0.0f, "Band Pass"},
    {FILTER_NOTCH, 1000.0f, 5.0f, 0.0f, "Notch"},
    {FILTER_ALLPASS, 1000.0f, 3.0f, 0.0f, "All Pass"},
    {FILTER_PEAKING, 1000.0f, 3.0f, 9.0f, "Peaking EQ"},
    {FILTER_LOWSHELF, 200.0f, 1.0f, 9.0f, "Low Shelf"},
    {FILTER_HIGHSHELF, 5000.0f, 1.0f, 8.0f, "High Shelf"},
    {FILTER_RESONANT, 800.0f, 12.0f, 0.0f, "Resonant"}
  };
  
  if (type >= FILTER_NONE && type <= FILTER_RESONANT) {
    return &presets[type];
  }
  return &presets[0];
}

const char* AudioEngine::getFilterName(FilterType type) {
  const FilterPreset* preset = getFilterPreset(type);
  return preset ? preset->name : "Unknown";
}

// ============= EXTENDED BIQUAD COEFFICIENT CALCULATION =============

void AudioEngine::calculateBiquadCoeffs(FXParams& fxParam) {
  if (fxParam.filterType == FILTER_NONE) return;
  
  float omega = 2.0f * PI * fxParam.cutoff / SAMPLE_RATE;
  float sn = sinf(omega);
  float cs = cosf(omega);
  float alpha = sn / (2.0f * fxParam.resonance);
  float A = powf(10.0f, fxParam.gain / 40.0f); // For shelf/peaking filters
  
  switch (fxParam.filterType) {
    case FILTER_LOWPASS:
      fxParam.coeffs.b0 = (1.0f - cs) / 2.0f;
      fxParam.coeffs.b1 = 1.0f - cs;
      fxParam.coeffs.b2 = (1.0f - cs) / 2.0f;
      fxParam.coeffs.a1 = -2.0f * cs;
      fxParam.coeffs.a2 = 1.0f - alpha;
      break;
      
    case FILTER_HIGHPASS:
      fxParam.coeffs.b0 = (1.0f + cs) / 2.0f;
      fxParam.coeffs.b1 = -(1.0f + cs);
      fxParam.coeffs.b2 = (1.0f + cs) / 2.0f;
      fxParam.coeffs.a1 = -2.0f * cs;
      fxParam.coeffs.a2 = 1.0f - alpha;
      break;
      
    case FILTER_BANDPASS:
      fxParam.coeffs.b0 = alpha;
      fxParam.coeffs.b1 = 0.0f;
      fxParam.coeffs.b2 = -alpha;
      fxParam.coeffs.a1 = -2.0f * cs;
      fxParam.coeffs.a2 = 1.0f - alpha;
      break;
      
    case FILTER_NOTCH:
      fxParam.coeffs.b0 = 1.0f;
      fxParam.coeffs.b1 = -2.0f * cs;
      fxParam.coeffs.b2 = 1.0f;
      fxParam.coeffs.a1 = -2.0f * cs;
      fxParam.coeffs.a2 = 1.0f - alpha;
      break;
      
    case FILTER_ALLPASS:
      fxParam.coeffs.b0 = 1.0f - alpha;
      fxParam.coeffs.b1 = -2.0f * cs;
      fxParam.coeffs.b2 = 1.0f + alpha;
      fxParam.coeffs.a1 = -2.0f * cs;
      fxParam.coeffs.a2 = 1.0f - alpha;
      break;
      
    case FILTER_PEAKING: {
      float beta = sqrtf(A) / fxParam.resonance;
      fxParam.coeffs.b0 = 1.0f + alpha * A;
      fxParam.coeffs.b1 = -2.0f * cs;
      fxParam.coeffs.b2 = 1.0f - alpha * A;
      fxParam.coeffs.a1 = -2.0f * cs;
      fxParam.coeffs.a2 = 1.0f - alpha / A;
      break;
    }
      
    case FILTER_LOWSHELF: {
      float sqrtA = sqrtf(A);
      fxParam.coeffs.b0 = A * ((A + 1.0f) - (A - 1.0f) * cs + 2.0f * sqrtA * alpha);
      fxParam.coeffs.b1 = 2.0f * A * ((A - 1.0f) - (A + 1.0f) * cs);
      fxParam.coeffs.b2 = A * ((A + 1.0f) - (A - 1.0f) * cs - 2.0f * sqrtA * alpha);
      fxParam.coeffs.a1 = -2.0f * ((A - 1.0f) + (A + 1.0f) * cs);
      fxParam.coeffs.a2 = (A + 1.0f) + (A - 1.0f) * cs - 2.0f * sqrtA * alpha;
      break;
    }
      
    case FILTER_HIGHSHELF: {
      float sqrtA = sqrtf(A);
      fxParam.coeffs.b0 = A * ((A + 1.0f) + (A - 1.0f) * cs + 2.0f * sqrtA * alpha);
      fxParam.coeffs.b1 = -2.0f * A * ((A - 1.0f) + (A + 1.0f) * cs);
      fxParam.coeffs.b2 = A * ((A + 1.0f) + (A - 1.0f) * cs - 2.0f * sqrtA * alpha);
      fxParam.coeffs.a1 = 2.0f * ((A - 1.0f) - (A + 1.0f) * cs);
      fxParam.coeffs.a2 = (A + 1.0f) - (A - 1.0f) * cs - 2.0f * sqrtA * alpha;
      break;
    }
      
    case FILTER_RESONANT:
      // High resonance lowpass
      fxParam.coeffs.b0 = (1.0f - cs) / 2.0f;
      fxParam.coeffs.b1 = 1.0f - cs;
      fxParam.coeffs.b2 = (1.0f - cs) / 2.0f;
      fxParam.coeffs.a1 = -2.0f * cs;
      fxParam.coeffs.a2 = 1.0f - alpha;
      break;
      
    default:
      break;
  }
  
  // Normalize by a0
  float a0 = (fxParam.filterType == FILTER_LOWSHELF || fxParam.filterType == FILTER_HIGHSHELF) 
             ? ((fxParam.filterType == FILTER_LOWSHELF) 
                ? ((powf(10.0f, fxParam.gain / 40.0f) + 1.0f) + (powf(10.0f, fxParam.gain / 40.0f) - 1.0f) * cs + 2.0f * sqrtf(powf(10.0f, fxParam.gain / 40.0f)) * alpha)
                : ((powf(10.0f, fxParam.gain / 40.0f) + 1.0f) - (powf(10.0f, fxParam.gain / 40.0f) - 1.0f) * cs + 2.0f * sqrtf(powf(10.0f, fxParam.gain / 40.0f)) * alpha))
             : (1.0f + alpha);
             
  fxParam.coeffs.b0 /= a0;
  fxParam.coeffs.b1 /= a0;
  fxParam.coeffs.b2 /= a0;
  fxParam.coeffs.a1 /= a0;
  fxParam.coeffs.a2 /= a0;
}

// ============= EXTENDED FILTER PROCESSING =============

inline int16_t AudioEngine::applyFilter(int16_t input, FXParams& fxParam) {
  if (fxParam.filterType == FILTER_NONE) return input;
  
  float x = (float)input;
  float y = fxParam.coeffs.b0 * x + fxParam.state.x1;
  
  fxParam.state.x1 = fxParam.coeffs.b1 * x - fxParam.coeffs.a1 * y + fxParam.state.x2;
  fxParam.state.x2 = fxParam.coeffs.b2 * x - fxParam.coeffs.a2 * y;
  
  // Clamp to prevent overflow
  if (y > 32767.0f) y = 32767.0f;
  else if (y < -32768.0f) y = -32768.0f;
  
  return (int16_t)y;
}
