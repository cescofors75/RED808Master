/*
 * AudioEngine.cpp
 * Implementació del motor d'àudio
 */

#include "AudioEngine.h"

AudioEngine::AudioEngine() : i2sPort(I2S_NUM_0),
                             processCount(0), lastCpuCheck(0), cpuLoad(0.0f) {
  // Initialize voices
  for (int i = 0; i < MAX_VOICES; i++) {
    resetVoice(i);
  }
  
  // Initialize sample buffers
  for (int i = 0; i < 16; i++) {
    sampleBuffers[i] = nullptr;
    sampleLengths[i] = 0;
  }
  
  // Initialize FX
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
  
  for (int i = 0; i < 16; i++) {
    padFilters[i].filterType = FILTER_NONE;
    padFilters[i].cutoff = 1000.0f;
    padFilters[i].resonance = 1.0f;
    padFilters[i].gain = 0.0f;
    padFilters[i].state.x1 = padFilters[i].state.x2 = 0.0f;
    padFilters[i].state.y1 = padFilters[i].state.y2 = 0.0f;
    padFilterActive[i] = false;
  }
  
  // Initialize volume
  masterVolume = 100; // Master stays at 100% by default
  sequencerVolume = 10; // Start at 10% to avoid loud startup
  liveVolume = 80; // Live pads at 80% for better balance
  
  // Initialize visualization
  captureIndex = 0;
  memset(captureBuffer, 0, sizeof(captureBuffer));
}

AudioEngine::~AudioEngine() {
  i2s_driver_uninstall(i2sPort);
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
    .use_apll = false,
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
  if (padIndex < 0 || padIndex >= 8) return false;
  
  sampleBuffers[padIndex] = buffer;
  sampleLengths[padIndex] = length;
  
  Serial.printf("[AudioEngine] Sample buffer set: Pad %d, Buffer: %p, Length: %d samples\n", 
                padIndex, buffer, length);
  
  return true;
}

void AudioEngine::triggerSample(int padIndex, uint8_t velocity) {
  triggerSampleLive(padIndex, velocity);
}

void AudioEngine::triggerSampleSequencer(int padIndex, uint8_t velocity) {
  if (padIndex < 0 || padIndex >= 8) {
    Serial.printf("[AudioEngine] ERROR: Invalid pad index %d\n", padIndex);
    return;
  }
  if (sampleBuffers[padIndex] == nullptr) {
    Serial.printf("[AudioEngine] ERROR: No sample buffer for pad %d\n", padIndex);
    return;
  }
  int voiceIndex = findFreeVoice();
  if (voiceIndex < 0) {
    voiceIndex = 0;
    Serial.println("[AudioEngine] No free voice, stealing voice 0");
  }
  voices[voiceIndex].buffer = sampleBuffers[padIndex];
  voices[voiceIndex].position = 0;
  voices[voiceIndex].length = sampleLengths[padIndex];
  voices[voiceIndex].active = true;
  voices[voiceIndex].velocity = velocity;
  voices[voiceIndex].volume = sequencerVolume;
  voices[voiceIndex].pitchShift = 1.0f;
  voices[voiceIndex].loop = false;
  voices[voiceIndex].padIndex = padIndex;
  voices[voiceIndex].isLivePad = false;
  
  const char* filterStatus = trackFilterActive[padIndex] ? "FILTER ON" : "no filter";
  Serial.printf("[AudioEngine] *** SEQ TRACK %d -> Voice %d, Length: %d, Vel: %d, %s ***\n",
                padIndex, voiceIndex, sampleLengths[padIndex], velocity, filterStatus);
}

void AudioEngine::triggerSampleLive(int padIndex, uint8_t velocity) {
  if (padIndex < 0 || padIndex >= 8) {
    Serial.printf("[AudioEngine] ERROR: Invalid pad index %d\n", padIndex);
    return;
  }
  if (sampleBuffers[padIndex] == nullptr) {
    Serial.printf("[AudioEngine] ERROR: No sample buffer for pad %d\n", padIndex);
    return;
  }
  
  // Find free voice
  int voiceIndex = findFreeVoice();
  if (voiceIndex < 0) {
    // No free voice, steal oldest
    voiceIndex = 0;
    Serial.println("[AudioEngine] No free voice, stealing voice 0");
  }
  
  // Setup voice
  voices[voiceIndex].buffer = sampleBuffers[padIndex];
  voices[voiceIndex].position = 0;
  voices[voiceIndex].length = sampleLengths[padIndex];
  voices[voiceIndex].active = true;
  voices[voiceIndex].velocity = velocity;
  // Apply 20% boost to livepads so they sound louder than sequencer at same volume setting
  voices[voiceIndex].volume = (liveVolume * 120) / 100;
  voices[voiceIndex].pitchShift = 1.0f;
  voices[voiceIndex].loop = false;
  voices[voiceIndex].padIndex = padIndex;
  voices[voiceIndex].isLivePad = true;
  
  Serial.printf("[AudioEngine] *** LIVE PAD %d -> Voice %d, Length: %d samples, Velocity: %d ***\n",
                padIndex, voiceIndex, sampleLengths[padIndex], velocity);
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

void AudioEngine::process() {
  static uint32_t logCounter = 0;
  static uint32_t lastLogTime = 0;
  
  // Fill mix buffer
  fillBuffer(mixBuffer, DMA_BUF_LEN);
  
  // Write to I2S External DAC
  size_t bytes_written;
  i2s_write(i2sPort, mixBuffer, DMA_BUF_LEN * 4, &bytes_written, portMAX_DELAY);
  
  // Log every 5 seconds
  logCounter++;
  if (millis() - lastLogTime > 5000) {
    int activeVoices = 0;
    for (int i = 0; i < MAX_VOICES; i++) {
      if (voices[i].active) activeVoices++;
    }
    Serial.printf("[AudioEngine] Process loop running OK, active voices: %d, calls: %d/5sec\n", 
                  activeVoices, logCounter);
    lastLogTime = millis();
    logCounter = 0;
  }
  
  // Update CPU load calculation
  processCount++;
  uint32_t now = millis();
  if (now - lastCpuCheck > 1000) {
    cpuLoad = (processCount * DMA_BUF_LEN * 1000.0f) / (SAMPLE_RATE * (now - lastCpuCheck));
    processCount = 0;
    lastCpuCheck = now;
  }
}

void AudioEngine::fillBuffer(int16_t* buffer, size_t samples) {
  // Clear output buffer
  memset(buffer, 0, samples * sizeof(int16_t) * 2);

  // Usar un acumulador de 32 bits para evitar distorsión/clipping durante el mix
  static int32_t mixAcc[DMA_BUF_LEN * 2];
  memset(mixAcc, 0, sizeof(mixAcc));
  
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
      
      // Get sample
      int16_t sample = voice.buffer[voice.position];
      
      // Apply velocity and per-source volume
      int32_t scaled = ((int32_t)sample * voice.velocity) / 127;
      scaled = (scaled * voice.volume) / 100;
      
      // Apply per-pad or per-track filter if active
      int16_t filtered = (int16_t)constrain(scaled, -32768, 32767);
      if (voice.padIndex >= 0 && voice.padIndex < MAX_PADS) {
        // Check if pad has filter (for live pads)
        if (voice.isLivePad && padFilterActive[voice.padIndex]) {
          filtered = applyFilter(filtered, padFilters[voice.padIndex]);
        }
        // Check if track has filter (for sequencer tracks)
        else if (!voice.isLivePad && voice.padIndex < MAX_AUDIO_TRACKS && trackFilterActive[voice.padIndex]) {
          filtered = applyFilter(filtered, trackFilters[voice.padIndex]);
          // Debug: print only once per voice activation
          if (voice.position == 0) {
            Serial.printf("[FILTER APPLIED] Track %d, Type: %d\n", voice.padIndex, trackFilters[voice.padIndex].filterType);
          }
        }
      }
      
      // Mix to accumulator
      mixAcc[i * 2] += filtered;      // Left
      mixAcc[i * 2 + 1] += filtered;  // Right
      
      voice.position++;
    }
  }
  
  // Soft clipping and conversion to 16bit with FX and volume
  for (size_t i = 0; i < samples * 2; i++) {
    int32_t val = mixAcc[i];
    
    // Apply master volume (0-100)
    val = (val * masterVolume) / 100;
    
    if (val > 32767) val = 32767;
    else if (val < -32768) val = -32768;
    
    // Apply FX chain
    buffer[i] = processFX((int16_t)val);
    
    // Capture for visualization (every 2 samples for decimation)
    if ((i % 2 == 0) && (captureIndex < 256)) {
      captureBuffer[captureIndex++] = buffer[i];
      if (captureIndex >= 256) captureIndex = 0;
    }
  }
}

int AudioEngine::findFreeVoice() {
  for (int i = 0; i < MAX_VOICES; i++) {
    if (!voices[i].active) return i;
  }
  return -1;
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

void AudioEngine::setSampleRateReduction(uint32_t rate) {
  fx.sampleRate = constrain(rate, 8000, SAMPLE_RATE);
  fx.srCounter = 0;
}

// Volume Control
void AudioEngine::setMasterVolume(uint8_t volume) {
  masterVolume = constrain(volume, 0, 150);
  Serial.printf("[AudioEngine] Master volume: %d%%\n", masterVolume);
}

uint8_t AudioEngine::getMasterVolume() {
  return masterVolume;
}

void AudioEngine::setSequencerVolume(uint8_t volume) {
  sequencerVolume = constrain(volume, 0, 150);
  Serial.printf("[AudioEngine] Sequencer volume: %d%%\n", sequencerVolume);
}

uint8_t AudioEngine::getSequencerVolume() {
  return sequencerVolume;
}

void AudioEngine::setLiveVolume(uint8_t volume) {
  liveVolume = constrain(volume, 0, 150);
  Serial.printf("[AudioEngine] Live volume: %d%%\n", liveVolume);
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

// Distortion (soft clipping with tanh-like approximation)
inline int16_t AudioEngine::applyDistortion(int16_t input) {
  if (fx.distortion < 0.1f) return input;
  
  // Fast distortion using soft clipping
  float x = (float)input / 32768.0f;
  float amount = fx.distortion / 100.0f;
  
  // Gain boost
  x *= (1.0f + amount * 3.0f);
  
  // Fast soft clip approximation (faster than tanh)
  if (x > 0.9f) x = 0.9f + (x - 0.9f) * 0.1f;
  else if (x < -0.9f) x = -0.9f + (x + 0.9f) * 0.1f;
  
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

// ============= AUDIO VISUALIZATION =============

void AudioEngine::captureAudioData(uint8_t* spectrum, uint8_t* waveform) {
  // Copiar del mixBuffer si está disponible, si no usar captureBuffer
  int16_t* sourceBuffer = captureBuffer;
  int sourceSize = 256;
  
  // Simple FFT-like spectrum approximation using band filtering
  // Split captured buffer into 64 frequency bands
  
  for (int band = 0; band < 64; band++) {
    float sum = 0.0f;
    int startIdx = (band * sourceSize) / 64;
    int endIdx = ((band + 1) * sourceSize) / 64;
    
    // Calculate RMS for this band
    for (int i = startIdx; i < endIdx; i++) {
      float sample = sourceBuffer[i] / 32768.0f;
      sum += sample * sample;
    }
    
    float rms = sqrtf(sum / (endIdx - startIdx));
    // Amplify significantly for better visibility
    rms = fminf(rms * 10.0f, 1.0f);
    spectrum[band] = (uint8_t)(rms * 255.0f);
  }
  
  // Waveform: decimate captured buffer to 128 samples
  for (int i = 0; i < 128; i++) {
    int idx = (i * sourceSize) / 128;
    // Keep the waveform centered at 128 (middle of 0-255 range)
    float sample = sourceBuffer[idx] / 32768.0f; // -1.0 to +1.0
    float normalized = (sample * 0.5f) + 0.5f;    // 0.0 to 1.0
    waveform[i] = (uint8_t)(constrain(normalized * 255.0f, 0.0f, 255.0f));
  }
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
  if (pad < 0 || pad >= 8) return false;
  
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
  if (pad < 0 || pad >= 8) return;
  padFilters[pad].filterType = FILTER_NONE;
  padFilterActive[pad] = false;
  Serial.printf("[AudioEngine] Pad %d filter cleared\n", pad);
}

FilterType AudioEngine::getPadFilter(int pad) {
  if (pad < 0 || pad >= 8) return FILTER_NONE;
  return padFilters[pad].filterType;
}

int AudioEngine::getActivePadFiltersCount() {
  int count = 0;
  for (int i = 0; i < 8; i++) {
    if (padFilterActive[i]) count++;
  }
  return count;
}

// ============= FILTER PRESETS =============

const FilterPreset* AudioEngine::getFilterPreset(FilterType type) {
  static const FilterPreset presets[] = {
    {FILTER_NONE, 0.0f, 1.0f, 0.0f, "None"},
    {FILTER_LOWPASS, 1000.0f, 1.0f, 0.0f, "Low Pass"},
    {FILTER_HIGHPASS, 1000.0f, 1.0f, 0.0f, "High Pass"},
    {FILTER_BANDPASS, 1000.0f, 2.0f, 0.0f, "Band Pass"},
    {FILTER_NOTCH, 1000.0f, 2.0f, 0.0f, "Notch"},
    {FILTER_ALLPASS, 1000.0f, 1.0f, 0.0f, "All Pass"},
    {FILTER_PEAKING, 1000.0f, 2.0f, 6.0f, "Peaking EQ"},
    {FILTER_LOWSHELF, 500.0f, 1.0f, 6.0f, "Low Shelf"},
    {FILTER_HIGHSHELF, 4000.0f, 1.0f, 6.0f, "High Shelf"},
    {FILTER_RESONANT, 1000.0f, 10.0f, 0.0f, "Resonant"}
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
