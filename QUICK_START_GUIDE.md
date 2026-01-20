# 🚀 Guia de Començament Ràpid - Drum Machine ESP32-S3

## 📋 El Que Tens

✅ ESP32-S3 + placa expansió  
✅ Display ST7789 240x240  
✅ DAC PCM5102A  
✅ Botonera 4 botons  
✅ 3 kits de samples 808  

---

## 🎯 Objectiu Final

Crear una **drum machine professional** amb:
- 🎹 16 pads virtuals (iPad)
- 🎚️ 3 kits 808 intercanviables
- 🔊 Audio alta qualitat
- 📱 Control WiFi
- 📊 Display amb stats

---

## 📖 Pas a Pas Complet

### FASE 1: Software (30 mins)

#### 1.1 Instal·lar Arduino IDE
```
✓ Descarrega: https://www.arduino.cc/en/software
✓ Instal·la Arduino IDE 2.x
✓ Obre Arduino IDE
```

#### 1.2 Instal·lar ESP32 Core
```
✓ File → Preferences
✓ Additional Boards Manager URLs:
  https://espressif.github.io/arduino-esp32/package_esp32_index.json
✓ OK
✓ Tools → Board → Boards Manager
✓ Busca: esp32
✓ Instal·la: "esp32 by Espressif Systems" (v3.0.0+)
✓ Espera 5-10 mins
```

#### 1.3 Instal·lar Llibreries
```
✓ Sketch → Include Library → Manage Libraries

Llibreria 1:
  Busca: TFT_eSPI
  Instal·la (v2.5.x)

Llibreria 2 (opcional, per LED RGB):
  Busca: Adafruit NeoPixel
  Instal·la (v1.12.x)
```

#### 1.4 Configurar TFT_eSPI
```
✓ Obre: Documents/Arduino/libraries/TFT_eSPI/User_Setup.h
✓ Descomenta: #define ST7789_DRIVER
✓ Configura:
  #define TFT_WIDTH  240
  #define TFT_HEIGHT 240
  #define TFT_MOSI 13
  #define TFT_SCLK 14
  #define TFT_CS   10
  #define TFT_DC   11
  #define TFT_RST  12
✓ Guarda

(O copia TFT_eSPI_Config.txt del projecte)
```

---

### FASE 2: Hardware (15 mins)

#### 2.1 Connexions PCM5102A (Audio)
```
ESP32-S3    PCM5102A
--------    --------
GPIO 42  →  BCK
GPIO 41  →  LRCK
GPIO 2   →  DIN
3.3V     →  VIN
GND      →  GND

Config PCM5102A jumpers:
SCK  → GND
FMT  → GND
XMT  → 3.3V
```

#### 2.2 Connexions ST7789 (Display)
```
ESP32-S3    ST7789
--------    ------
GPIO 10  →  CS
GPIO 11  →  DC
GPIO 12  →  RST
GPIO 13  →  MOSI
GPIO 14  →  SCLK
3.3V     →  VCC
3.3V     →  BL (backlight directe)
GND      →  GND
```

#### 2.3 Connexions Botons
```
ESP32-S3    Botó
--------    ----
GPIO 15  →  UP → GND
GPIO 16  →  DOWN → GND
GPIO 17  →  SELECT → GND
GPIO 18  →  BACK → GND
```

**Total: 12 pins GPIO + alimentació**

---

### FASE 3: Preparar Samples (10 mins)

#### 3.1 Organitzar Kits 808
```bash
# Al terminal / cmd
cd [directori_on_has_descarregat_el_projecte]

python3 organize_808_kits.py ./els_teus_3_directoris_808 ./data

# Això crea:
# data/
#   kits/
#     kit1.txt
#     kit2.txt  
#     kit3.txt
#   samples/
#     kit1_kick.wav
#     kit1_snare.wav
#     ... tots els samples
```

#### 3.2 Copiar a Sketch
```
✓ Copia la carpeta "data/" 
✓ Enganxa-la dins de: DrumMachine_ESP32S3/
  
Estructura final:
DrumMachine_ESP32S3/
├── DrumMachine_ESP32S3.ino
├── AudioEngine.h
├── ...
└── data/          ← Aquí!
    ├── kits/
    └── samples/
```

---

### FASE 4: Configurar Codi (5 mins)

#### 4.1 Configurar WiFi

Obre `DrumMachine_ESP32S3.ino` i edita:

**Opció A: Connectar a la teva WiFi (recomanat)**
```cpp
#define WIFI_MODE_STATION  // Descomenta aquesta línia
#define WIFI_SSID "NomDelTeuWiFi"
#define WIFI_PASSWORD "PasswordDelTeuWiFi"

// Comenta aquestes:
// #define WIFI_MODE_AP
```

**Opció B: Access Point (sense WiFi)**
```cpp
// Comenta aquestes:
// #define WIFI_MODE_STATION

// Descomenta aquestes:
#define WIFI_MODE_AP
#define AP_SSID "DrumMachine"
#define AP_PASSWORD "drummachine123"
```

---

### FASE 5: Upload (10 mins)

#### 5.1 Configurar Board
```
Arduino IDE → Tools →

Board: "ESP32S3 Dev Module"
USB CDC On Boot: "Enabled" ⭐
USB Mode: "Hardware CDC and JTAG"
PSRAM: "OPI PSRAM" ⭐
Partition Scheme: "Default 4MB with spiffs"
Upload Speed: "921600"
```

#### 5.2 Primer Upload - Codi
```
1. Mode BOOT:
   ✓ Desconnecta USB
   ✓ Mantén BOOT
   ✓ Connecta USB
   ✓ Espera 2 segons
   ✓ Deixa anar BOOT

2. Selecciona port:
   ✓ Tools → Port → COMx (o /dev/ttyACM0)

3. Upload:
   ✓ Sketch → Upload
   ✓ Espera "Done uploading"

4. Reset:
   ✓ Prem botó RESET
```

#### 5.3 Segon Upload - Samples (SPIFFS)
```
1. Tanca Serial Monitor si està obert

2. Upload samples:
   ✓ Tools → ESP32 Sketch Data Upload
   ✓ Espera 2-5 mins (puja samples a SPIFFS)
   ✓ "SPIFFS Upload complete"

3. Reset:
   ✓ Prem RESET
```

---

### FASE 6: Test (5 mins)

#### 6.1 Verificar Display
```
Hauries de veure:
✓ "Drum Machine" al display
✓ "Loading kits..."
✓ "Kit: [nom_kit]"
✓ Grid 4x4 pads
✓ CPU%, PSRAM stats
✓ IP WiFi
```

#### 6.2 Verificar Serial Monitor
```
Tools → Serial Monitor (115200 baud)

Hauries de veure:
✓ "SPIFFS mounted"
✓ "PSRAM found: 8388608 bytes"
✓ "Display initialized"
✓ "Found 3 kits"
✓ "WiFi connected!"
✓ "IP address: 192.168.x.x"
✓ "System ready!"
```

#### 6.3 Test Botons
```
Prem botons físics:
✓ UP → Canvia kit
✓ DOWN → Canvia kit
✓ SELECT → VU meter
✓ BACK → Torna
```

---

### FASE 7: Connectar iPad (5 mins)

#### 7.1 Connectar a WiFi
```
iPad → Settings → WiFi

Opció A (WiFi Station):
✓ Connecta a la teva WiFi habitual
✓ Safari → http://drummachine.local
  O http://[IP_mostrada_al_display]

Opció B (Access Point):
✓ Connecta a "DrumMachine"
✓ Password: drummachine123
✓ Safari → http://192.168.4.1
```

#### 7.2 Test Pads
```
A Safari veuràs:
✓ 16 pads tàctils (4x4)
✓ Kit selector (Prev/Next)
✓ Velocity slider
✓ Stats (CPU, kit actual)

Toca un pad → Hauria de sonar! 🎵
```

#### 7.3 Add to Home Screen
```
Safari:
✓ Share button (quadrat amb fletxa)
✓ "Add to Home Screen"
✓ Done!

Ara tens una app nativa! 📱
```

---

## ✅ Checklist Final

```
Hardware:
☐ 12 pins GPIO connectats
☐ 3.3V i GND comú
☐ PCM5102A jumpers configurats
☐ Display encès amb backlight
☐ LED power ESP32 encès

Software:
☐ Arduino IDE 2.x instal·lat
☐ ESP32 core instal·lat
☐ TFT_eSPI instal·lada i configurada
☐ Codi compila sense errors

Upload:
☐ Codi pujat correctament
☐ Samples pujats a SPIFFS
☐ Serial Monitor mostra "System ready"
☐ Display mostra interfície

Funcionament:
☐ Botons físics funcionen
☐ Display mostra stats
☐ WiFi connectat
☐ iPad mostra web interface
☐ Pads sonen quan es toquen
☐ Pots canviar entre kits

🎉 SUCCESS! Drum Machine funcionant!
```

---

## 🆘 Problemes Comuns

### Display negre
```
→ Verifica 3.3V al VCC
→ Revisa pins GPIO 10-14
→ Comprova User_Setup.h TFT_eSPI
```

### No surt port COM
```
→ Mode BOOT (mantén + connecta)
→ USB CDC On Boot = Enabled
→ Prova altre cable USB
```

### No hi ha àudio
```
→ Verifica GPIO 2, 41, 42
→ PCM5102A jumpers (SCK→GND, FMT→GND, XMT→3.3V)
→ Comprova sortida amb auriculars
```

### WiFi no connecta
```
→ Revisa SSID i password al codi
→ Mira Serial Monitor per errors
→ Prova mode AP en comptes de Station
```

### Samples no carreguen
```
→ Verifica "ESP32 Sketch Data Upload" fet
→ Serial Monitor: "SPIFFS mounted"
→ Torna a pujar samples
```

### Web no carrega a iPad
```
→ Ping a la IP mostrada
→ Prova http://[IP] en comptes de .local
→ Verifica mateix WiFi iPad-ESP32
```

---

## 📚 Guies Disponibles

```
Instal·lació:
├── INSTALACION_ARDUINO_IDE.md (aquesta guia)
├── ESP32S3_USB_TROUBLESHOOTING.md (problemes USB)

Hardware:
├── PINOUT_TU_CONFIG.md (pins exactes)
├── WIRING_SIMPLE.txt (diagrames connexions)
├── VOLTATGES_GUIDE.md (3.3V vs 5V)
├── LED_RGB_GUIDE.md (LED multicolor)

Configuració:
├── SIMPLE_SETUP.md (setup complet)
├── QUICK_START_808_KITS.md (organitzar samples)

Desenvolupament:
├── README.md (manual complet)
├── EXAMPLES.md (extensions)
├── IPAD_GUIDE.md (iPad avançat)
```

---

## ⏱️ Temps Total Estimat

```
Software setup:     30 mins
Hardware connexió:  15 mins
Preparar samples:   10 mins
Configurar codi:     5 mins
Upload:             10 mins
Test i ajustos:      5 mins
Connectar iPad:      5 mins
─────────────────────────
TOTAL:             ~80 mins (1h 20min)
```

**Primer cop**: 1-2 hores
**Següents cops**: 15-20 mins

---

## 🎉 Resultat Final

Tindràs:
- 🥁 Drum machine professional
- 🎹 16 pads virtuals iPad
- 🎚️ 3 kits 808 intercanviables  
- 🔊 Audio alta qualitat DAC
- 📱 Control WiFi tàctil
- 📊 Display amb feedback
- ⚡ Latència ultra-baixa (30-50ms)

**Tot funcional i llest per fer música!** 🎶

---

**Gaudeix la teva Drum Machine!** 🚀🥁

Per dubtes, consulta les altres guies al projecte.
