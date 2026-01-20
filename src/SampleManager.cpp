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
  
  // Parse WAV file
  if (!parseWavFile(file, padIndex)) {
    file.close();
    return false;
  }
  
  file.close();
  
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
  
  // Calculate sample length
  uint32_t numSamples = header.dataSize / (header.bitsPerSample / 8);
  
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
  
  // Read sample data
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
    Serial.printf("Sample too large: %d bytes (max %d)\n", bytes, MAX_SAMPLE_SIZE);
    return false;
  }
  
  // Allocate in PSRAM
  sampleBuffers[padIndex] = (int16_t*)ps_malloc(bytes);
  
  if (sampleBuffers[padIndex] == nullptr) {
    Serial.printf("Failed to allocate %d bytes in PSRAM\n", bytes);
    return false;
  }
  
  Serial.printf("Allocated %d bytes in PSRAM for pad %d\n", bytes, padIndex + 1);
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
