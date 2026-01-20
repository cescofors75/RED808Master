/*
 * KitManager.cpp
 * Implementació del gestor de kits
 */

#include "KitManager.h"
#include "DisplayManager.h"
#include <TFT_eSPI.h>

extern SampleManager sampleManager;
extern DisplayManager display;

KitManager::KitManager() : kitCount(0), currentKit(-1) {
  for (int i = 0; i < MAX_KITS; i++) {
    memset(kits[i].name, 0, 32);
    kits[i].sampleCount = 0;
  }
}

KitManager::~KitManager() {
}

bool KitManager::begin() {
  Serial.println("Initializing Kit Manager...");
  
  // Scan for available kits
  int count = scanKits();
  
  if (count > 0) {
    Serial.printf("Found %d kits\n", count);
    
    // Load first kit by default
    loadKit(0);
    return true;
  } else {
    Serial.println("No kits found!");
    return false;
  }
}

int KitManager::scanKits() {
  kitCount = 0;
  
  // Scan directories /01, /02, /03
  const char* kitDirs[] = {"/01", "/02", "/03"};
  const char* kitNames[] = {"Kit 1", "Kit 2", "Kit 3"};
  
  for (int i = 0; i < 3 && kitCount < MAX_KITS; i++) {
    fs::File dir = SPIFFS.open(kitDirs[i]);
    if (dir && dir.isDirectory()) {
      Serial.printf("Found kit directory: %s\n", kitDirs[i]);
      
      // Create kit from directory
      Kit& kit = kits[kitCount];
      strncpy(kit.name, kitNames[i], 31);
      kit.name[31] = '\0';
      kit.sampleCount = 0;
      
      // Load samples 001.wav to 008.wav
      for (int s = 1; s <= 8 && kit.sampleCount < MAX_SAMPLES_PER_KIT; s++) {
        char filename[32];
        sprintf(filename, "%s/%03d.wav", kitDirs[i], s);
        
        // Check if file exists (case insensitive)
        fs::File testFile = SPIFFS.open(filename);
        if (!testFile) {
          // Try uppercase .WAV
          sprintf(filename, "%s/%03d.WAV", kitDirs[i], s);
          testFile = SPIFFS.open(filename);
        }
        
        if (testFile) {
          testFile.close();
          kit.samples[kit.sampleCount].padIndex = s - 1; // Pad 0-7
          strncpy(kit.samples[kit.sampleCount].filename, filename, 63);
          kit.samples[kit.sampleCount].filename[63] = '\0';
          kit.sampleCount++;
          Serial.printf("  Added: %s -> Pad %d\n", filename, s - 1);
        }
      }
      
      if (kit.sampleCount > 0) {
        Serial.printf("Loaded kit '%s' with %d samples\n", kit.name, kit.sampleCount);
        kitCount++;
      }
      dir.close();
    } else {
      Serial.printf("Kit directory not found: %s\n", kitDirs[i]);
    }
  }
  
  return kitCount;
}

bool KitManager::parseKitFile(const char* filename, int kitIndex) {
  fs::File file = SPIFFS.open(filename);
  if (!file) {
    Serial.printf("Failed to open: %s\n", filename);
    return false;
  }
  
  Kit& kit = kits[kitIndex];
  kit.sampleCount = 0;
  
  // Extract kit name from filename (kit1.txt -> kit1)
  String name = String(filename);
  int start = name.lastIndexOf('/') + 1;
  int end = name.lastIndexOf('.');
  name = name.substring(start, end);
  name.toCharArray(kit.name, 32);
  
  // Parse file
  while (file.available() && kit.sampleCount < MAX_SAMPLES_PER_KIT) {
    String line = file.readStringUntil('\n');
    line.trim();
    
    // Skip empty lines and comments
    if (line.length() == 0 || line.startsWith("#")) {
      // Try to extract kit name from comment
      if (line.startsWith("# ") && kit.name[0] == 'k') {
        String nameFromComment = line.substring(2);
        nameFromComment.trim();
        if (nameFromComment.length() > 0 && nameFromComment.length() < 32) {
          nameFromComment.toCharArray(kit.name, 32);
        }
      }
      continue;
    }
    
    // Parse line: "pad_index filename.wav"
    int spaceIdx = line.indexOf(' ');
    if (spaceIdx > 0) {
      int pad = line.substring(0, spaceIdx).toInt();
      String sampleFile = line.substring(spaceIdx + 1);
      sampleFile.trim();
      
      if (pad >= 0 && pad < 16 && sampleFile.length() > 0) {
        kit.samples[kit.sampleCount].padIndex = pad;
        sampleFile.toCharArray(kit.samples[kit.sampleCount].filename, 64);
        kit.sampleCount++;
      }
    }
  }
  
  file.close();
  
  Serial.printf("Loaded kit '%s' with %d samples\n", kit.name, kit.sampleCount);
  return kit.sampleCount > 0;
}

bool KitManager::loadKit(int kitIndex) {
  if (kitIndex < 0 || kitIndex >= kitCount) {
    Serial.printf("Invalid kit index: %d\n", kitIndex);
    return false;
  }
  
  Kit& kit = kits[kitIndex];
  
  Serial.printf("Loading kit %d: %s\n", kitIndex, kit.name);
  display.showMessage(kit.name, TFT_CYAN);
  
  // Unload current samples
  sampleManager.unloadAll();
  
  // Load all samples from this kit (filename already has full path)
  int loaded = 0;
  for (int i = 0; i < kit.sampleCount; i++) {
    if (sampleManager.loadSample(kit.samples[i].filename, kit.samples[i].padIndex)) {
      loaded++;
    } else {
      Serial.printf("Failed to load: %s\n", kit.samples[i].filename);
    }
  }
  
  currentKit = kitIndex;
  
  Serial.printf("Kit loaded: %d/%d samples\n", loaded, kit.sampleCount);
  
  char msg[32];
  sprintf(msg, "%s (%d samples)", kit.name, loaded);
  display.showMessage(msg, TFT_GREEN);
  delay(1000);
  
  return loaded > 0;
}

int KitManager::getKitCount() {
  return kitCount;
}

int KitManager::getCurrentKit() {
  return currentKit;
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
  
  Serial.println("========================================");
  Serial.printf("Kit %d: %s\n", kitIndex, kit.name);
  Serial.println("----------------------------------------");
  Serial.printf("Samples: %d\n", kit.sampleCount);
  Serial.println("----------------------------------------");
  
  for (int i = 0; i < kit.sampleCount; i++) {
    Serial.printf("  Pad %2d: %s\n", 
                  kit.samples[i].padIndex, 
                  kit.samples[i].filename);
  }
  
  Serial.println("========================================");
}
