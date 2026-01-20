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
  
  // Kit 01: TR-808 Classic (50/50 balance)
  Kit& kit1 = kits[kitCount];
  strncpy(kit1.name, "TR-808 Classic", 31);
  kit1.sampleCount = 0;

  const char* files1[] = {
    "/BD/BD5050.WAV",  // Bass Drum - TUNING:50, DECAY:50
    "/SD/SD0000.WAV",  // Snare Drum - TONE:0, SNAPPY:0
    "/CH/CH.WAV",      // Closed Hi-Hat
    "/OH/OH00.WAV",    // Open Hi-Hat
    "/CP/CP.WAV",      // Hand Clap
    "/CB/CB.WAV",      // Cowbell
    "/RS/RS.WAV",      // Rimshot
    "/OH/OH00.WAV"     // Open Hat (alt)
  };

  for (int i = 0; i < 8; i++) {
    if (LittleFS.exists(files1[i])) {
      kit1.samples[kit1.sampleCount].padIndex = i;
      strncpy(kit1.samples[kit1.sampleCount].filename, files1[i], 63);
      kit1.sampleCount++;
      Serial.printf("  [Kit 1] Added: %s -> Pad %d\n", files1[i], i);
    }
  }
  if (kit1.sampleCount > 0) kitCount++;

  // Kit 02: TR-808 Heavy (75/10 agresivo)
  if (kitCount < MAX_KITS) {
    Kit& kit2 = kits[kitCount];
    strncpy(kit2.name, "TR-808 Heavy", 31);
    kit2.sampleCount = 0;

    const char* files2[] = {
      "/BD/BD7510.WAV",  // Bass Drum - TUNING:75, DECAY:10 (grave y corto)
      "/SD/SD5000.WAV",  // Snare Drum - TONE:50, SNAPPY:0 (cuerpo)
      "/CH/CH.WAV",      // Closed HH
      "/OH/OH00.WAV",    // Open HH
      "/CP/CP.WAV",      // Clap
      "/CB/CB.WAV",      // Cowbell
      "/RS/RS.WAV",      // Rimshot
      "/CB/CB.WAV"       // Cowbell alt
    };

    for (int i = 0; i < 8; i++) {
      if (LittleFS.exists(files2[i])) {
        kit2.samples[kit2.sampleCount].padIndex = i;
        strncpy(kit2.samples[kit2.sampleCount].filename, files2[i], 63);
        kit2.sampleCount++;
      }
    }
    if (kit2.sampleCount > 0) kitCount++;
  }

  // Kit 03: TR-808 Soft (25/25 suave)
  if (kitCount < MAX_KITS) {
    Kit& kit3 = kits[kitCount];
    strncpy(kit3.name, "TR-808 Soft", 31);
    kit3.sampleCount = 0;

    const char* files3[] = {
      "/BD/BD2525.WAV",  // Bass Drum - TUNING:25, DECAY:25 (suave)
      "/SD/SD0050.WAV",  // Snare Drum - TONE:0, SNAPPY:50 (bright)
      "/CH/CH.WAV",      // Closed HH
      "/CH/CH.WAV",      // Closed HH (doble)
      "/OH/OH00.WAV",    // Open HH
      "/RS/RS.WAV",      // Rimshot
      "/CP/CP.WAV",      // Clap
      "/CB/CB.WAV"       // Cowbell
    };

    for (int i = 0; i < 8; i++) {
      if (LittleFS.exists(files3[i])) {
        kit3.samples[kit3.sampleCount].padIndex = i;
        strncpy(kit3.samples[kit3.sampleCount].filename, files3[i], 63);
        kit3.sampleCount++;
      }
    }
    if (kit3.sampleCount > 0) kitCount++;
  }

  Serial.printf("✓ Kits encontrados: %d\n", kitCount);
  return kitCount;
}

bool KitManager::parseKitFile(const char* filename, int kitIndex) {
  fs::File file = LittleFS.open(filename);
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
    Serial.printf("Error: Kit %d no existe\n", kitIndex);
    return false;
  }
  
  currentKit = kitIndex;
  Kit& kit = kits[kitIndex];
  
  Serial.printf("\n========== CARGANDO KIT %d: %s ==========\n", kitIndex, kit.name);
  
  // Unload current samples
  sampleManager.unloadAll();
  
  // Load all samples from this kit
  int loaded = 0;
  for (int i = 0; i < kit.sampleCount; i++) {
    int padIndex = kit.samples[i].padIndex;
    const char* filename = kit.samples[i].filename;
    
    Serial.printf("  Pad %d -> %s\n", padIndex, filename);
    
    if (sampleManager.loadSample(filename, padIndex)) {
      loaded++;
      Serial.printf("    OK\n");
    } else {
      Serial.printf("    ERROR cargando sample!\n");
    }
  }
  
  Serial.printf("========== KIT CARGADO: %d/%d samples ==========\n\n", loaded, kit.sampleCount);
  
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
