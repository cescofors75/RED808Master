/*
 * KitManager.cpp
 * Implementació del gestor de kits
 */

#include "KitManager.h"

extern SampleManager sampleManager;

KitManager::KitManager() : kitCount(0), currentKit(-1) {
  for (int i = 0; i < MAX_KITS; i++) {
    memset(kits[i].name, 0, 32);
    kits[i].sampleCount = 0;
  }
}

KitManager::~KitManager() {
}

bool KitManager::begin() {
  
  // Scan for available kits
  int count = scanKits();
  
  if (count > 0) {
    
    // Load first kit by default
    loadKit(0);
    return true;
  } else {
    return false;
  }
}

int KitManager::scanKits() {
  kitCount = 0;
  
  // 16 carpetas de instrumentos
  const char* folders[16] = {
    "/BD", "/SD", "/CH", "/OH", "/CP", "/CB", "/RS", "/CL",
    "/MA", "/CY", "/HT", "/LT", "/MC", "/MT", "/HC", "/LC"
  };
  
  // Kit Único: Todos los instrumentos disponibles
  Kit& kit = kits[0];
  strncpy(kit.name, "RED808 16-Track", 31);
  kit.sampleCount = 0;

  // Cargar primer sample de cada carpeta
  for (int i = 0; i < 16 && kit.sampleCount < MAX_SAMPLES_PER_KIT; i++) {
    File dir = LittleFS.open(folders[i]);
    if (!dir || !dir.isDirectory()) {
      continue;
    }
    
    // Buscar primer archivo .wav o .WAV
    File file = dir.openNextFile();
    bool found = false;
    while (file && !found) {
      String filename = file.name();
      filename.toUpperCase();
      
      if (!file.isDirectory() && filename.endsWith(".WAV")) {
        // Construir path completo
        char fullPath[128];
        snprintf(fullPath, 127, "%s/%s", folders[i], file.name());
        
        // Agregar al kit
        kit.samples[kit.sampleCount].padIndex = i;
        strncpy(kit.samples[kit.sampleCount].filename, fullPath, 63);
        kit.sampleCount++;
        
        found = true;
      }
      
      file = dir.openNextFile();
    }
    
    if (!found) {
    }
  }
  
  if (kit.sampleCount > 0) {
    kitCount = 1;
  } else {
  }
  
  return kitCount;
}

bool KitManager::loadKit(int kitIndex) {
  if (kitIndex < 0 || kitIndex >= kitCount) {
    return false;
  }
  
  currentKit = kitIndex;
  Kit& kit = kits[kitIndex];
  
  
  // Unload current samples
  sampleManager.unloadAll();
  
  // Load all samples from this kit
  int loaded = 0;
  for (int i = 0; i < kit.sampleCount; i++) {
    int padIndex = kit.samples[i].padIndex;
    const char* filename = kit.samples[i].filename;
    
    
    if (sampleManager.loadSample(filename, padIndex)) {
      loaded++;
    } else {
    }
  }
  
  
  return loaded > 0;
}

const char* KitManager::getKitName(int kitIndex) {
  if (kitIndex < 0 || kitIndex >= kitCount) {
    return "";
  }
  return kits[kitIndex].name;
}

void KitManager::printKitInfo(int kitIndex) {
  if (kitIndex < 0 || kitIndex >= kitCount) return;
  
  Kit& kit = kits[kitIndex];
  
  
  for (int i = 0; i < kit.sampleCount; i++) {
  }
  
}
