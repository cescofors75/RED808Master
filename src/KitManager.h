/*
 * KitManager.h
 * Gestió de múltiples kits de samples 808
 */

#ifndef KITMANAGER_H
#define KITMANAGER_H

#include <Arduino.h>
#include <LittleFS.h>
#include <FS.h>
#include "SampleManager.h"

#define MAX_KITS 8
#define MAX_SAMPLES_PER_KIT 16

struct KitSample {
  char filename[64];
  int padIndex;
};

struct Kit {
  char name[32];
  int sampleCount;
  KitSample samples[MAX_SAMPLES_PER_KIT];
};

class KitManager {
public:
  KitManager();
  ~KitManager();
  
  // Initialization
  bool begin();
  
  // Kit management
  int scanKits();
  bool loadKit(int kitIndex);
  int getKitCount() { return kitCount; }
  int getCurrentKit() { return currentKit; }
  const char* getKitName(int kitIndex);
  const char* getCurrentKitName() { 
    if (currentKit >= 0 && currentKit < kitCount) {
      return kits[currentKit].name;
    }
    return "Unknown";
  }
  
  // Kit info
  void printKitInfo(int kitIndex);
  
private:
  Kit kits[MAX_KITS];
  int kitCount;
  int currentKit;
};

#endif // KITMANAGER_H
