/*
 * SampleManager.cpp
 * Implementació del gestor de samples
 */

#include "SampleManager.h"

extern AudioEngine audioEngine;

SampleManager::SampleManager() {
  for (int i = 0; i < MAX_SAMPLES; i++) {
    sampleBuffers[i] = nullptr;
    sampleLengths[i] = 0;
    memset(sampleNames[i], 0, 32);
  }
}

SampleManager::~SampleManager() {
  unloadAll();
}

bool SampleManager::begin() {
  if (!psramFound()) {
    Serial.println("ERROR: PSRAM not found!");
    return false;
  }
  
  Serial.printf("PSRAM available: %d bytes\n", ESP.getFreePsram());
  return true;
}

bool SampleManager::loadSample(const char* filename, int padIndex) {
  if (padIndex < 0 || padIndex >= MAX_SAMPLES) {
    Serial.println("Invalid pad index");
    return false;
  }
  
  // Unload existing sample if any
  if (sampleBuffers[padIndex] != nullptr) {
    unloadSample(padIndex);
  }
  
  // Open file
  fs::File file = LittleFS.open(filename, "r");
  if (!file) {
    Serial.printf("Failed to open file: %s\n", filename);
    return false;
  }
  
  bool success = false;
  String fname = String(filename);

  // DETECT .RAW OR .WAV
  if (fname.endsWith(".raw") || fname.endsWith(".RAW")) {
    // --- LOAD RAW (No Header, 16-bit signed, Mono) ---
    size_t fileSize = file.size();
    Serial.printf("[SampleManager] Reading RAW file %s (%d bytes)...\n", filename, fileSize);
    
    uint32_t numSamples = fileSize / 2; // 16-bit = 2 bytes per sample
    
    if (allocateSampleBuffer(padIndex, numSamples)) {
      size_t bytesRead = file.read((uint8_t*)sampleBuffers[padIndex], fileSize);
      if (bytesRead == fileSize) {
        sampleLengths[padIndex] = numSamples;
        success = true;
      } else {
        Serial.println("Failed to read RAW data");
        freeSampleBuffer(padIndex);
      }
    }
  } else {
    // --- LOAD WAV (With Header Parsing) ---
    // Parse WAV file
    success = parseWavFile(file, padIndex);
  }
  
  file.close();
  
  if (!success) {
    Serial.printf("❌ FAILED to load: %s\n", filename);
    return false; 
  }
  
  // Store sample name
  const char* name = strrchr(filename, '/');
  if (name) name++; // Skip '/'
  else name = filename;
  strncpy(sampleNames[padIndex], name, 31);
  
  // Register with audio engine
  audioEngine.setSampleBuffer(padIndex, sampleBuffers[padIndex], sampleLengths[padIndex]);
  
  Serial.printf("[SampleManager] ✓ Sample loaded: %s (%d samples) -> Pad %d\n", 
                sampleNames[padIndex], sampleLengths[padIndex], padIndex);
  Serial.printf("[SampleManager]   Buffer address: %p, Free PSRAM: %d bytes\n",
                sampleBuffers[padIndex], ESP.getFreePsram());
  
  return true;
}

bool SampleManager::parseWavFile(fs::File& file, int padIndex) {
  WavHeader header;
  
  size_t fileSize = file.size();
  Serial.printf("[SampleManager] Leyendo %s (Flash Size: %d bytes)...\n", file.name(), (int)fileSize);

  if (fileSize < 44) {
    Serial.printf("❌ Archivo %s demasiado pequeño en SPIFFS (%d bytes)\n", file.name(), (int)fileSize);
    return false;
  }

  // Asegurarnos de estar al principio del archivo
  file.seek(0);
  size_t readSize = file.read((uint8_t*)&header, 44);

  if (readSize != 44) {
    Serial.printf("❌ Fallo leyendo header: leídos %d de 44\n", (int)readSize);
    return false;
  }
  
  // Verify RIFF/WAVE (algunos archivos pueden tener "RIFFX")
  if (memcmp(header.riff, "RIFF", 4) != 0 || memcmp(header.wave, "WAVE", 4) != 0) {
    Serial.printf("❌ No es un WAV válido (Header: %.4s %.4s)\n", header.riff, header.wave);
    return false;
  }
  
  // Check format
  if (header.audioFormat != 1) {
    Serial.println("Only PCM WAV files supported");
    return false;
  }
  
  // Check bits per sample
  if (header.bitsPerSample != 16) {
    Serial.println("Only 16-bit WAV files supported");
    return false;
  }
  
  // ===== BUSCAR EL CHUNK "data" (puede no estar en posición 44) =====
  file.seek(36); // Empezar después del header básico RIFF/WAVE/fmt
  
  uint32_t actualDataSize = 0;
  bool foundDataChunk = false;
  
  while (file.available() >= 8) {
    char chunkId[4];
    uint32_t chunkSize;
    
    if (file.read((uint8_t*)chunkId, 4) != 4) break;
    if (file.read((uint8_t*)&chunkSize, 4) != 4) break;
    
    if (memcmp(chunkId, "data", 4) == 0) {
      actualDataSize = chunkSize;
      foundDataChunk = true;
      Serial.printf("✓ Data chunk encontrado en posición %d, tamaño: %d bytes\n", file.position() - 8, chunkSize);
      break;
    } else {
      // Skip this chunk (puede ser LIST, INFO, etc.)
      Serial.printf("  Saltando chunk '%.4s' (%d bytes)\n", chunkId, chunkSize);
      file.seek(file.position() + chunkSize);
    }
  }
  
  if (!foundDataChunk) {
    Serial.println("❌ No se encontró el chunk 'data' en el WAV");
    return false;
  }
  
  // Calculate sample length usando el tamaño REAL del data chunk
  uint32_t numSamples = actualDataSize / (header.bitsPerSample / 8);
  
  // If stereo, we'll mix down to mono
  if (header.numChannels == 2) {
    numSamples /= 2;
  }
  
  Serial.printf("WAV Info: %d Hz, %d channels, %d bits, %d samples\n",
                header.sampleRate, header.numChannels, header.bitsPerSample, numSamples);
  
  // Allocate PSRAM buffer
  if (!allocateSampleBuffer(padIndex, numSamples)) {
    return false;
  }
  
  // Read sample data (file está posicionado justo después del header del data chunk)
  if (header.numChannels == 1) {
    // Mono - direct read
    size_t bytesRead = file.read((uint8_t*)sampleBuffers[padIndex], numSamples * 2);
    if (bytesRead != numSamples * 2) {
      Serial.println("Failed to read sample data");
      freeSampleBuffer(padIndex);
      return false;
    }
  } else if (header.numChannels == 2) {
    // Stereo - mix down to mono
    int16_t stereoBuffer[2];
    for (uint32_t i = 0; i < numSamples; i++) {
      if (file.read((uint8_t*)stereoBuffer, 4) != 4) {
        Serial.println("Failed to read stereo data");
        freeSampleBuffer(padIndex);
        return false;
      }
      // Mix: (L + R) / 2
      sampleBuffers[padIndex][i] = (stereoBuffer[0] / 2) + (stereoBuffer[1] / 2);
    }
  }
  
  sampleLengths[padIndex] = numSamples;
  return true;
}

bool SampleManager::allocateSampleBuffer(int padIndex, uint32_t size) {
  size_t bytes = size * sizeof(int16_t);
  
  if (bytes > MAX_SAMPLE_SIZE) {
    Serial.printf("❌ Sample demasiado grande: %d bytes (máx %d = %.1fMB)\n", 
                  bytes, MAX_SAMPLE_SIZE, MAX_SAMPLE_SIZE / (1024.0 * 1024.0));
    return false;
  }
  
  // Verificar PSRAM disponible
  size_t freePsram = ESP.getFreePsram();
  size_t minRequired = bytes + (100 * 1024); // +100KB margen de seguridad
  
  if (freePsram < minRequired) {
    Serial.printf("❌ PSRAM insuficiente: necesita %d bytes, disponible %d bytes\n", 
                  minRequired, freePsram);
    return false;
  }
  
  // Allocate in PSRAM
  sampleBuffers[padIndex] = (int16_t*)ps_malloc(bytes);
  
  if (sampleBuffers[padIndex] == nullptr) {
    Serial.printf("❌ Fallo al alocar %d bytes en PSRAM (libre: %d bytes)\n", bytes, freePsram);
    return false;
  }
  
  Serial.printf("✅ Alocados %d bytes (%.1fKB) en PSRAM para pad %d (libre: %d bytes)\n", 
                bytes, bytes / 1024.0, padIndex + 1, ESP.getFreePsram());
  return true;
}

void SampleManager::freeSampleBuffer(int padIndex) {
  if (sampleBuffers[padIndex] != nullptr) {
    free(sampleBuffers[padIndex]);
    sampleBuffers[padIndex] = nullptr;
    sampleLengths[padIndex] = 0;
    memset(sampleNames[padIndex], 0, 32);
  }
}

bool SampleManager::unloadSample(int padIndex) {
  if (padIndex < 0 || padIndex >= MAX_SAMPLES) return false;
  
  freeSampleBuffer(padIndex);
  audioEngine.setSampleBuffer(padIndex, nullptr, 0);
  
  Serial.printf("Sample unloaded from pad %d\n", padIndex + 1);
  return true;
}

bool SampleManager::trimSample(int padIndex, float startNorm, float endNorm) {
  if (padIndex < 0 || padIndex >= MAX_SAMPLES || !sampleBuffers[padIndex]) return false;
  if (startNorm < 0.0f) startNorm = 0.0f;
  if (endNorm > 1.0f) endNorm = 1.0f;
  if (startNorm >= endNorm) return false;
  
  uint32_t origLen = sampleLengths[padIndex];
  uint32_t newStart = (uint32_t)(startNorm * origLen);
  uint32_t newEnd = (uint32_t)(endNorm * origLen);
  uint32_t newLen = newEnd - newStart;
  if (newLen < 64) return false;  // Minimum sample length
  
  // Allocate new buffer
  int16_t* newBuf = (int16_t*)ps_malloc(newLen * sizeof(int16_t));
  if (!newBuf) {
    Serial.println("[Trim] Failed to allocate trimmed buffer");
    return false;
  }
  
  // Copy trimmed region
  memcpy(newBuf, sampleBuffers[padIndex] + newStart, newLen * sizeof(int16_t));
  
  // Free old buffer and replace
  free(sampleBuffers[padIndex]);
  sampleBuffers[padIndex] = newBuf;
  sampleLengths[padIndex] = newLen;
  
  // Update audio engine
  audioEngine.setSampleBuffer(padIndex, newBuf, newLen);
  
  Serial.printf("[Trim] Pad %d: %u -> %u samples (%.1f%% - %.1f%%)\n",
                padIndex, origLen, newLen, startNorm * 100, endNorm * 100);
  return true;
}

bool SampleManager::applyFade(int padIndex, float fadeInSec, float fadeOutSec) {
  if (padIndex < 0 || padIndex >= MAX_SAMPLES || !sampleBuffers[padIndex]) return false;
  if (fadeInSec < 0.0f) fadeInSec = 0.0f;
  if (fadeOutSec < 0.0f) fadeOutSec = 0.0f;
  
  uint32_t len = sampleLengths[padIndex];
  if (len < 4) return false;
  
  // Fade in: ramp up gain from 0 to 1 over the first fadeInSamples
  if (fadeInSec > 0.001f) {
    uint32_t fadeInSamples = (uint32_t)(fadeInSec * 44100.0f);
    if (fadeInSamples > len / 2) fadeInSamples = len / 2;  // Max half the sample
    for (uint32_t i = 0; i < fadeInSamples; i++) {
      float t = (float)i / (float)fadeInSamples;  // 0.0 to 1.0
      sampleBuffers[padIndex][i] = (int16_t)((float)sampleBuffers[padIndex][i] * t);
    }
  }
  
  // Fade out: ramp down gain from 1 to 0 over the last fadeOutSamples
  if (fadeOutSec > 0.001f) {
    uint32_t fadeOutSamples = (uint32_t)(fadeOutSec * 44100.0f);
    if (fadeOutSamples > len / 2) fadeOutSamples = len / 2;  // Max half the sample
    uint32_t fadeOutStart = len - fadeOutSamples;
    for (uint32_t i = 0; i < fadeOutSamples; i++) {
      float t = 1.0f - ((float)i / (float)fadeOutSamples);  // 1.0 to 0.0
      sampleBuffers[padIndex][fadeOutStart + i] = (int16_t)((float)sampleBuffers[padIndex][fadeOutStart + i] * t);
    }
  }
  
  Serial.printf("[Fade] Pad %d: FadeIn=%.3fs FadeOut=%.3fs\n", padIndex, fadeInSec, fadeOutSec);
  return true;
}

void SampleManager::unloadAll() {
  for (int i = 0; i < MAX_SAMPLES; i++) {
    if (sampleBuffers[i] != nullptr) {
      unloadSample(i);
    }
  }
}

bool SampleManager::isSampleLoaded(int padIndex) {
  if (padIndex < 0 || padIndex >= MAX_SAMPLES) return false;
  return sampleBuffers[padIndex] != nullptr;
}

uint32_t SampleManager::getSampleLength(int padIndex) {
  if (padIndex < 0 || padIndex >= MAX_SAMPLES) return 0;
  return sampleLengths[padIndex];
}

const char* SampleManager::getSampleName(int padIndex) {
  if (padIndex < 0 || padIndex >= MAX_SAMPLES) return "";
  return sampleNames[padIndex];
}

size_t SampleManager::getTotalPSRAMUsed() {
  size_t total = 0;
  for (int i = 0; i < MAX_SAMPLES; i++) {
    if (sampleBuffers[i] != nullptr) {
      total += sampleLengths[i] * sizeof(int16_t);
    }
  }
  return total;
}

size_t SampleManager::getFreePSRAM() {
  return ESP.getFreePsram();
}

int SampleManager::getLoadedSamplesCount() {
  int count = 0;
  for (int i = 0; i < MAX_SAMPLES; i++) {
    if (sampleBuffers[i] != nullptr) {
      count++;
    }
  }
  return count;
}

size_t SampleManager::getTotalMemoryUsed() {
  return getTotalPSRAMUsed();
}

int16_t* SampleManager::getSampleBuffer(int padIndex) {
  if (padIndex < 0 || padIndex >= MAX_SAMPLES) return nullptr;
  return sampleBuffers[padIndex];
}

int SampleManager::getWaveformPeaks(int padIndex, int8_t* outPeaks, int maxPoints) {
  if (padIndex < 0 || padIndex >= MAX_SAMPLES || !sampleBuffers[padIndex] || maxPoints <= 0) return 0;
  
  uint32_t len = sampleLengths[padIndex];
  if (len == 0) return 0;
  
  int points = (maxPoints > 200) ? 200 : maxPoints;
  uint32_t samplesPerPoint = len / points;
  if (samplesPerPoint == 0) samplesPerPoint = 1;
  
  for (int i = 0; i < points; i++) {
    uint32_t start = i * samplesPerPoint;
    uint32_t end = start + samplesPerPoint;
    if (end > len) end = len;
    
    int16_t maxVal = 0;
    int16_t minVal = 0;
    for (uint32_t j = start; j < end; j++) {
      int16_t s = sampleBuffers[padIndex][j];
      if (s > maxVal) maxVal = s;
      if (s < minVal) minVal = s;
    }
    // Scale 16-bit to 8-bit signed (-128..127)
    // Use the peak (max of abs(min), abs(max)) for upper, negative for lower
    outPeaks[i * 2]     = (int8_t)(maxVal >> 8);  // positive peak
    outPeaks[i * 2 + 1] = (int8_t)(minVal >> 8);  // negative peak
  }
  return points;
}
