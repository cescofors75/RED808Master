/*
 * SampleManager.h
 * Gestió de càrrega de samples des de SPIFFS a PSRAM
 */

#ifndef SAMPLEMANAGER_H
#define SAMPLEMANAGER_H

#include <Arduino.h>
#include <LittleFS.h>
#include <FS.h>
#include "AudioEngine.h"

#define MAX_SAMPLES 8
#define MAX_SAMPLE_SIZE (2 * 1024 * 1024) // 2MB per sample (suficiente para samples largos)

// WAV file header structure
struct WavHeader {
  char riff[4];           // "RIFF"
  uint32_t fileSize;
  char wave[4];           // "WAVE"
  char fmt[4];            // "fmt "
  uint32_t fmtSize;
  uint16_t audioFormat;
  uint16_t numChannels;
  uint32_t sampleRate;
  uint32_t byteRate;
  uint16_t blockAlign;
  uint16_t bitsPerSample;
  char data[4];           // "data"
  uint32_t dataSize;
} __attribute__((packed));

class SampleManager {
public:
  SampleManager();
  ~SampleManager();
  
  // Initialization
  bool begin();
  
  // Sample loading
  bool loadSample(const char* filename, int padIndex);
  bool unloadSample(int padIndex);
  void unloadAll();
  
  // Sample info
  bool isSampleLoaded(int padIndex);
  uint32_t getSampleLength(int padIndex);
  const char* getSampleName(int padIndex);
  int getLoadedSamplesCount();
  
  // Memory info
  size_t getTotalPSRAMUsed();
  size_t getTotalMemoryUsed(); // Alias para compatibilidad
  size_t getFreePSRAM();
  
private:
  int16_t* sampleBuffers[MAX_SAMPLES];
  uint32_t sampleLengths[MAX_SAMPLES];
  char sampleNames[MAX_SAMPLES][32];
  
  bool parseWavFile(fs::File& file, int padIndex);
  bool allocateSampleBuffer(int padIndex, uint32_t size);
  void freeSampleBuffer(int padIndex);
};

#endif // SAMPLEMANAGER_H
