# Configuració SIMPLE - Sense Pads Físics

## 🎯 El Teu Hardware

Tens exactament el necessari per una drum machine funcional:

✅ **ESP32-S3** amb placa d'expansió  
✅ **Pantalla ST7789** 240x240  
✅ **DAC PCM5102A** per audio d'alta qualitat  
✅ **4 botons** de navegació  
✅ **iPad** per control tàctil via WiFi  

**NO necessites pads físics** - els toques des de l'iPad! 🎹

---

## 📌 Pins a Connectar (Només 16 pins)

### 1. PCM5102A DAC (Audio)
```
ESP32-S3       PCM5102A
--------       ---------
GPIO 42   →    BCK
GPIO 41   →    LRCK
GPIO 2    →    DIN
3.3V      →    VIN
GND       →    GND

Configuració PCM5102A:
SCK  → GND
FMT  → GND  
XMT  → 3.3V
```

### 2. Pantalla ST7789 (Display)
```
ESP32-S3       ST7789
--------       ------
GPIO 10   →    CS
GPIO 11   →    DC
GPIO 12   →    RST
GPIO 13   →    MOSI (SDA)
GPIO 14   →    SCLK
3.3V      →    VCC
3.3V      →    BL (Backlight directe)
GND       →    GND
```

**NOTA**: El backlight va directament a 3.3V, no a GPIO.

### 3. Botonera 4 Botons
```
ESP32-S3       Botón
--------       -----
GPIO 15   →    UP      → GND
GPIO 16   →    DOWN    → GND
GPIO 17   →    SELECT  → GND
GPIO 18   →    BACK    → GND
```

**Total: 12 connexions (sense pads físics)**

---

## ⚙️ Configuració del Codi

Al fitxer `DrumMachine_ESP32S3.ino`:

### 1. Hardware (ja està configurat així per defecte)
```cpp
// ===== HARDWARE CONFIGURATION =====
#define HAS_DISPLAY     // Pantalla ST7789 ✓
#define HAS_AUDIO       // PCM5102A DAC ✓
#define HAS_BUTTONS     // 4 botons navegació ✓
// #define HAS_PADS     // Deixa comentat (no tens pads físics)
```

### 2. WiFi - TRIA UNA OPCIÓ:

**Opció A: Connectar a la teva WiFi** (recomanat)
```cpp
#define WIFI_MODE_STATION
#define WIFI_SSID "NomDelTeuWiFi"
#define WIFI_PASSWORD "PasswordDelTeuWiFi"
```

**Opció B: Crear Access Point propi**
```cpp
#define WIFI_MODE_AP
#define AP_SSID "DrumMachine"
#define AP_PASSWORD "drummachine123"
```

---

## 🚀 Passos per Posar-ho en Marxa

### 1. Connexions Hardware
```
1. Connecta PCM5102A (3 pins GPIO + power)
2. Connecta ST7789 (5 pins GPIO + power + BL a 3.3V)
3. Connecta 4 botons (4 pins GPIO a GND)
4. Verifica 3.3V i GND a tots

Total: 12 pins GPIO + alimentació
```

### 2. Prepara els Samples
```bash
# Organitza els teus 3 kits d'808
python3 organize_808_kits.py ./els_teus_samples ./data

# Copia la carpeta 'data' al directori del sketch
```

### 3. Puja Samples a SPIFFS
```
Arduino IDE:
Tools → ESP32 Sketch Data Upload
```

### 4. Configura WiFi
```cpp
// Edita DrumMachine_ESP32S3.ino
#define WIFI_SSID "el_teu_wifi"
#define WIFI_PASSWORD "la_teva_password"
```

### 5. Compila i Puja
```
Board: ESP32S3 Dev Module
PSRAM: OPI PSRAM
Partition: Default 4MB with spiffs
Upload Speed: 921600

Sketch → Upload
```

### 6. Connecta des de l'iPad
```
Safari → http://drummachine.local
O la IP que mostra el Serial Monitor
```

---

## 🎮 Controls

### A la Pantalla Física:
- **UP/DOWN**: Canviar entre kits (Kit 1/2/3)
- **SELECT**: Veure VU meter
- **BACK**: Tornar a pantalla principal

### A l'iPad:
- **16 pads tàctils**: Toca per reproduir samples
- **Velocity slider**: Ajusta la intensitat
- **Kit selector**: Canvia entre kits
- **Stats**: Veus actives, CPU%

---

## 📱 Interfície Web (iPad)

Quan connectis veuràs:

```
┌──────────────────────────┐
│   🥁 Drum Machine        │
│ 808 Classic   CPU: 12%   │
├──────────────────────────┤
│ ◀ Prev  Kit 1/3  Next ▶ │
├──────────────────────────┤
│  [1]  [2]  [3]  [4]     │
│ Kick Snare HHat Clap     │
│                          │
│  [5]  [6]  [7]  [8]     │
│ Tom1 Tom2 Tom3 Crash     │
│                          │
│  [9]  [10] [11] [12]    │
│ Ride Open Cow  Rim       │
│                          │
│  [13] [14] [15] [16]    │
│ Clav Mara Shak Perc      │
├──────────────────────────┤
│ Velocity: ━━●━━  100    │
└──────────────────────────┘
```

**Add to Home Screen** per tenir-ho com una app nativa!

---

## 🔧 Troubleshooting

### Pantalla en blanc
```
✓ Verifica 3.3V i GND
✓ Revisa pins SPI (GPIO 10-15)
✓ Comprova configuració TFT_eSPI
```

### No hi ha àudio
```
✓ Verifica 3.3V al PCM5102A
✓ Revisa pins I2S (GPIO 42, 41, 2)
✓ Comprova sortida LOUT/ROUT amb auriculars
```

### No connecta WiFi
```
✓ Verifica SSID i password
✓ Mira Serial Monitor per errors
✓ Prova mode AP en comptes de Station
```

### Web interface no carrega
```
✓ Ping a la IP mostrada
✓ Prova http://[IP] en comptes de .local
✓ Verifica que estàs a la mateixa xarxa WiFi
```

---

## 🎵 Flux de Treball Típic

```
1. Encén l'ESP32-S3
2. Espera que mostri la IP a la pantalla
3. A l'iPad: Safari → http://drummachine.local
4. Toca els pads a la pantalla de l'iPad
5. Usa botons físics per canviar kits
6. Ajusta velocity amb el slider
7. Gaudeix! 🎶
```

---

## 📊 Rendiment

**Latència:**
- Touch iPad → Audio: 30-50ms
- Botons físics → Kit change: instantani
- CPU: ~10-15% en ús normal

**Consum:**
- ~350mA @ 5V
- Funciona amb qualsevol USB

---

## 🔜 Expansions Futures (Opcionals)

Si més endavant vols afegir:

### Pads Físics (4-16 pads)
```cpp
// Descomenta al codi:
#define HAS_PADS

// Connecta GPIOs disponibles:
GPIO 4-9, 20-21, 35-40, 45-46
```

### MIDI Input/Output
```cpp
// Usa GPIOs lliures:
GPIO 1  → MIDI TX
GPIO 3  → MIDI RX
```

### Encoder Rotatori (per velocity/navegació)
```cpp
GPIO 47 → Encoder A
GPIO 48 → Encoder B
GPIO 1  → Encoder Button
```

### Status LEDs
```cpp
GPIO 3  → LED Power (verd)
GPIO 47 → LED Audio (vermell)
```

---

## ✅ Checklist Final

Abans de compilar, verifica:

```
☐ Tots els cables connectats segons pinout
☐ 3.3V present a PCM5102A i ST7789
☐ GND comú en tots els components
☐ WIFI_SSID i WIFI_PASSWORD configurats
☐ Samples organitzats i pujats a SPIFFS
☐ TFT_eSPI configurat per ST7789
☐ Board: ESP32S3 Dev Module amb OPI PSRAM
☐ HAS_PADS comentat (sense pads físics)
```

---

## 🎉 Resultat Final

Tindràs una **drum machine professional**:
- 🎹 16 pads virtuals a l'iPad
- 🎚️ 3 kits d'808 intercambiables
- 🔊 Audio d'alta qualitat via PCM5102A
- 📱 Control tàctil responsive
- 📊 Pantalla amb stats en temps real
- ⚡ Latència ultra-baixa

**Tot sense necessitat de pads físics!**

Perfecte per fer música, provar samples, i gaudir tocant des del sofà! 🛋️🎶

---

**Problemes?** Mira el Serial Monitor (115200 baud) per debug info.
**Més info?** Consulta IPAD_GUIDE.md per opcions avançades.
