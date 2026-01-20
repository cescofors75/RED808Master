#!/usr/bin/env python3
"""
organize_808_kits.py
Script per organitzar múltiples kits de samples 808 per ESP32-S3 Drum Machine

Estructura esperada:
  raw_samples/
    ├── kit1_808/
    │   ├── kick.wav
    │   ├── snare.wav
    │   └── ...
    ├── kit2_808/
    │   └── ...
    └── kit3_808/
        └── ...

Estructura de sortida per SPIFFS:
  data/
    ├── kits/
    │   ├── kit1.txt (metadata)
    │   ├── kit2.txt
    │   └── kit3.txt
    └── samples/
        ├── kit1_kick.wav
        ├── kit1_snare.wav
        ├── kit2_kick.wav
        └── ...
"""

import os
import sys
import shutil
import subprocess
from pathlib import Path

# Mapeo estàndard de noms de samples a pads
SAMPLE_MAPPING = {
    # Noms comuns → Pad index
    'kick': 0,
    'bd': 0,
    'bassdrum': 0,
    
    'snare': 1,
    'sd': 1,
    'snaredrum': 1,
    
    'hihat': 2,
    'hh': 2,
    'closedhat': 2,
    'ch': 2,
    
    'clap': 3,
    'cp': 3,
    'handclap': 3,
    
    'tom1': 4,
    'lowtom': 4,
    'lt': 4,
    
    'tom2': 5,
    'midtom': 5,
    'mt': 5,
    
    'tom3': 6,
    'hightom': 6,
    'ht': 6,
    
    'crash': 7,
    'cymbal': 7,
    'cy': 7,
    
    'ride': 8,
    'rd': 8,
    
    'openhat': 9,
    'oh': 9,
    'openhi': 9,
    
    'perc1': 10,
    'conga': 10,
    'cowbell': 10,
    'cb': 10,
    
    'perc2': 11,
    'rimshot': 11,
    'rim': 11,
    'rs': 11,
    
    'perc3': 12,
    'claves': 12,
    'cl': 12,
    
    'perc4': 13,
    'maracas': 13,
    'ma': 13,
    
    'perc5': 14,
    'shaker': 14,
    
    'perc6': 15,
}

def get_pad_index(filename):
    """Determina el pad index basant-se en el nom del fitxer"""
    name = filename.lower().replace('.wav', '').replace('_', '').replace('-', '')
    
    for key, pad in SAMPLE_MAPPING.items():
        if key in name:
            return pad
    
    return None

def analyze_kit(kit_path):
    """Analitza un kit i retorna la info dels samples"""
    samples = []
    
    wav_files = list(kit_path.glob('*.wav')) + list(kit_path.glob('*.WAV'))
    
    for wav_file in wav_files:
        pad_index = get_pad_index(wav_file.name)
        if pad_index is not None:
            samples.append({
                'file': wav_file,
                'pad': pad_index,
                'name': wav_file.stem
            })
    
    return sorted(samples, key=lambda x: x['pad'])

def verify_wav_format(filepath):
    """Verifica que el WAV sigui del format correcte"""
    try:
        cmd = [
            'ffprobe',
            '-v', 'quiet',
            '-print_format', 'json',
            '-show_streams',
            str(filepath)
        ]
        
        result = subprocess.run(cmd, capture_output=True, text=True, check=True)
        import json
        data = json.loads(result.stdout)
        
        if 'streams' not in data or len(data['streams']) == 0:
            return False, "No audio stream found"
        
        stream = data['streams'][0]
        sample_rate = int(stream.get('sample_rate', 0))
        channels = int(stream.get('channels', 0))
        bits = stream.get('bits_per_sample', 0)
        
        issues = []
        if sample_rate != 44100:
            issues.append(f"Sample rate: {sample_rate}Hz (expected 44100Hz)")
        if channels != 1:
            issues.append(f"Channels: {channels} (expected 1 - mono)")
        if bits and bits != 16:
            issues.append(f"Bit depth: {bits} (expected 16)")
        
        if issues:
            return False, "; ".join(issues)
        
        return True, "OK"
        
    except Exception as e:
        return False, str(e)

def organize_kits(input_dir, output_dir, max_size_kb=512):
    """Organitza múltiples kits en estructura SPIFFS"""
    input_path = Path(input_dir)
    output_path = Path(output_dir)
    
    # Crea directoris
    samples_dir = output_path / 'samples'
    kits_dir = output_path / 'kits'
    samples_dir.mkdir(parents=True, exist_ok=True)
    kits_dir.mkdir(parents=True, exist_ok=True)
    
    # Busca kits (directoris amb samples)
    kit_dirs = [d for d in input_path.iterdir() if d.is_dir()]
    
    if not kit_dirs:
        print(f"No s'han trobat directoris de kits a {input_dir}")
        return
    
    print(f"Trobats {len(kit_dirs)} kits")
    print("=" * 60)
    
    total_samples = 0
    total_size = 0
    issues = []
    
    for kit_idx, kit_dir in enumerate(kit_dirs, 1):
        kit_name = kit_dir.name
        print(f"\n[Kit {kit_idx}] {kit_name}")
        print("-" * 60)
        
        # Analitza samples del kit
        samples = analyze_kit(kit_dir)
        
        if not samples:
            print("  ⚠️  No s'han trobat samples vàlids")
            continue
        
        # Crea fitxer de metadata del kit
        kit_meta_file = kits_dir / f"kit{kit_idx}.txt"
        with open(kit_meta_file, 'w') as f:
            f.write(f"# {kit_name}\n")
            f.write(f"# Samples: {len(samples)}\n\n")
        
        # Processa cada sample
        for sample in samples:
            src_file = sample['file']
            dst_name = f"kit{kit_idx}_{sample['name']}.wav"
            dst_file = samples_dir / dst_name
            
            # Verifica format
            is_valid, msg = verify_wav_format(src_file)
            
            # Copia sample
            shutil.copy2(src_file, dst_file)
            
            # Comprova mida
            size_kb = dst_file.stat().st_size / 1024
            total_size += size_kb
            
            # Status
            status = "✓"
            if not is_valid:
                status = "⚠️"
                issues.append(f"{kit_name}/{src_file.name}: {msg}")
            elif size_kb > max_size_kb:
                status = "⚠️"
                issues.append(f"{kit_name}/{src_file.name}: {size_kb:.1f}KB > {max_size_kb}KB")
            
            print(f"  {status} Pad {sample['pad']:2d}: {sample['name']:20s} ({size_kb:6.1f}KB)")
            
            # Afegeix a metadata
            with open(kit_meta_file, 'a') as f:
                f.write(f"{sample['pad']:2d} {dst_name}\n")
            
            total_samples += 1
    
    # Resum final
    print("\n" + "=" * 60)
    print("RESUM")
    print("=" * 60)
    print(f"Total kits:    {len(kit_dirs)}")
    print(f"Total samples: {total_samples}")
    print(f"Mida total:    {total_size:.1f} KB ({total_size/1024:.2f} MB)")
    
    if issues:
        print(f"\n⚠️  {len(issues)} AVISOS:")
        for issue in issues[:10]:  # Mostra primers 10
            print(f"  - {issue}")
        if len(issues) > 10:
            print(f"  ... i {len(issues) - 10} més")
    
    print("\n✓ Organització completada!")
    print(f"\nSeguent pas:")
    print(f"  1. Revisa els fitxers a: {output_path}")
    print(f"  2. Copia tot el directori 'data' al teu sketch Arduino")
    print(f"  3. Arduino IDE: Tools → ESP32 Sketch Data Upload")

def create_kit_loader_code():
    """Genera codi Arduino per carregar kits"""
    code = '''
// ==================================================
// KIT LOADER - Afegeix això al teu sketch
// ==================================================

struct KitInfo {
  char name[32];
  int sampleCount;
  char samples[16][64];  // Max 16 samples per kit
  int pads[16];          // Pad assignments
};

KitInfo kits[8];  // Max 8 kits
int currentKit = 0;
int kitCount = 0;

bool loadKitMetadata(const char* kitFile, int kitIndex) {
  File file = SPIFFS.open(kitFile, "r");
  if (!file) return false;
  
  KitInfo& kit = kits[kitIndex];
  kit.sampleCount = 0;
  
  // Read metadata
  while (file.available() && kit.sampleCount < 16) {
    String line = file.readStringUntil('\\n');
    line.trim();
    
    // Skip comments
    if (line.startsWith("#") || line.length() == 0) {
      continue;
    }
    
    // Parse: "pad_index filename.wav"
    int spaceIdx = line.indexOf(' ');
    if (spaceIdx > 0) {
      int pad = line.substring(0, spaceIdx).toInt();
      String filename = line.substring(spaceIdx + 1);
      
      kit.pads[kit.sampleCount] = pad;
      filename.toCharArray(kit.samples[kit.sampleCount], 64);
      kit.sampleCount++;
    }
  }
  
  file.close();
  return true;
}

void loadAllKits() {
  // Scan for kit files
  File root = SPIFFS.open("/kits");
  if (!root) {
    Serial.println("Kits directory not found");
    return;
  }
  
  kitCount = 0;
  File file = root.openNextFile();
  while (file && kitCount < 8) {
    if (!file.isDirectory()) {
      String name = file.name();
      if (name.endsWith(".txt")) {
        if (loadKitMetadata(file.path(), kitCount)) {
          Serial.printf("Loaded kit %d: %s\\n", kitCount, file.name());
          kitCount++;
        }
      }
    }
    file = root.openNextFile();
  }
  
  Serial.printf("Total kits loaded: %d\\n", kitCount);
}

void selectKit(int kitIndex) {
  if (kitIndex < 0 || kitIndex >= kitCount) return;
  
  currentKit = kitIndex;
  KitInfo& kit = kits[currentKit];
  
  Serial.printf("Switching to kit %d\\n", currentKit);
  
  // Load all samples from this kit
  for (int i = 0; i < kit.sampleCount; i++) {
    String path = "/samples/" + String(kit.samples[i]);
    sampleManager.loadSample(path.c_str(), kit.pads[i]);
  }
  
  display.showMessage("Kit Loaded!", TFT_GREEN);
}

// Usage in setup():
// loadAllKits();
// selectKit(0);  // Load first kit
'''
    
    return code

def main():
    if len(sys.argv) < 3:
        print("Ús: python3 organize_808_kits.py <dir_kits> <dir_sortida>")
        print()
        print("Exemple:")
        print("  python3 organize_808_kits.py ./raw_samples ./data")
        print()
        print("Estructura esperada de directoris:")
        print("  raw_samples/")
        print("    ├── kit1_808/")
        print("    │   ├── kick.wav")
        print("    │   ├── snare.wav")
        print("    │   └── ...")
        print("    ├── kit2_808/")
        print("    └── kit3_808/")
        sys.exit(1)
    
    input_dir = sys.argv[1]
    output_dir = sys.argv[2]
    
    if not os.path.isdir(input_dir):
        print(f"ERROR: {input_dir} no existeix!")
        sys.exit(1)
    
    organize_kits(input_dir, output_dir)
    
    # Genera codi de loader
    loader_file = Path(output_dir) / 'KitLoader_Code.txt'
    with open(loader_file, 'w') as f:
        f.write(create_kit_loader_code())
    
    print(f"\n✓ Codi de kit loader guardat a: {loader_file}")

if __name__ == "__main__":
    main()
