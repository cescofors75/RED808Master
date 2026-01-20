# ESP32-S3 Drum Machine

Drum machine professional amb ESP32-S3, PCM5102A DAC, pantalla ST7789 i 16 pads.

## Hardware Necessari

- **ESP32-S3** amb placa d'expansió i PSRAM
- **PCM5102A DAC** per sortida d'àudio d'alta qualitat
- **Pantalla ST7789 240x240** per interfície visual
- **4 botons** per navegació (UP, DOWN, SELECT, BACK)
- **16 pads/triggers** (botons o sensors piezo)

## Connexions

### I2S (PCM5102A)
```
ESP32-S3         PCM5102A
GPIO 42    -->   BCK (Bit Clock)
GPIO 41    -->   LRCK (Word Select)
GPIO 2     -->   DIN (Data)
GND        -->   GND
3.3V       -->   VIN
           -->   SCK (a GND per mode I2S)
           -->   FMT (a GND per format I2S)
```

### SPI (ST7789 Display)
```
ESP32-S3         ST7789
GPIO 10    -->   CS
GPIO 11    -->   DC
GPIO 12    -->   RST
GPIO 13    -->   MOSI (SDA)
GPIO 14    -->   SCLK
GPIO 15    -->   BL (Backlight)
3.3V       -->   VCC
GND        -->   GND
```

### Botons de Navegació
```
ESP32-S3         Button
GPIO 16    -->   UP
GPIO 17    -->   DOWN
GPIO 18    -->   SELECT
GPIO 19    -->   BACK
```
Tots els botons van a GND quan es premen (pull-up intern)

### Pads/Triggers (16 pads)
```
Pad 1  --> GPIO 4      Pad 9  --> GPIO 35
Pad 2  --> GPIO 5      Pad 10 --> GPIO 36
Pad 3  --> GPIO 6      Pad 11 --> GPIO 37
Pad 4  --> GPIO 7      Pad 12 --> GPIO 38
Pad 5  --> GPIO 8      Pad 13 --> GPIO 39
Pad 6  --> GPIO 9      Pad 14 --> GPIO 40
Pad 7  --> GPIO 20     Pad 15 --> GPIO 45
Pad 8  --> GPIO 21     Pad 16 --> GPIO 46
```

## Configuració Arduino IDE

### Llibreries Necessàries
1. **TFT_eSPI** - Per la pantalla ST7789
   ```
   Sketch -> Include Library -> Manage Libraries -> Buscar "TFT_eSPI"
   ```

2. **WiFi** i **WebServer** - Incloses amb ESP32 core (no cal instal·lar)

3. **ESPmDNS** - Inclosa amb ESP32 core (no cal instal·lar)

2. **Configurar TFT_eSPI** per ST7789:
   Edita: `Arduino/libraries/TFT_eSPI/User_Setup.h`
   ```cpp
   #define ST7789_DRIVER
   #define TFT_WIDTH  240
   #define TFT_HEIGHT 240
   #define TFT_CS   10
   #define TFT_DC   11
   #define TFT_RST  12
   ```

### Configuració Board
1. Instal·la **ESP32** board package
   - File -> Preferences
   - Additional Boards Manager URLs: `https://espressif.github.io/arduino-esp32/package_esp32_index.json`
   - Tools -> Board -> Boards Manager -> Buscar "ESP32"

2. Selecciona:
   - Board: **ESP32S3 Dev Module**
   - PSRAM: **OPI PSRAM**
   - Partition Scheme: **Default 4MB with spiffs (1.2MB APP/1.5MB SPIFFS)**
   - Upload Speed: **921600**

## Configuració WiFi (Opcional - per Web Interface)

### Opció 1: Connectar a la teva WiFi

Edita `DrumMachine_ESP32S3.ino`:

```cpp
#define WIFI_MODE_STATION  // Descomenta aquesta línia
#define WIFI_SSID "EL_TEU_WIFI"
#define WIFI_PASSWORD "LA_TEVA_PASSWORD"
```

**Accés:**
- URL: `http://drummachine.local` o `http://[IP_MOSTRADA]`
- Comprova Serial Monitor per veure la IP

### Opció 2: Access Point (sense WiFi existent)

```cpp
#define WIFI_MODE_AP  // Descomenta aquesta línia
#define AP_SSID "DrumMachine"
#define AP_PASSWORD "drummachine123"
```

**Accés:**
1. Connecta el teu iPad/mòbil a la xarxa "DrumMachine"
2. URL: `http://192.168.4.1`

📱 **Guia completa iPad**: Consulta `IPAD_GUIDE.md`

## Preparació de Samples
- **Format**: WAV (PCM)
- **Bits**: 16-bit
- **Canals**: Mono o Stereo (es converteix a mono automàticament)
- **Sample Rate**: 44100 Hz (recomanat)
- **Longitud**: Màxim 512KB per sample

### Conversió amb FFmpeg
```bash
# Convertir a format correcte
ffmpeg -i input.wav -ar 44100 -ac 1 -sample_fmt s16 output.wav

# Normalitzar volum
ffmpeg -i input.wav -ar 44100 -ac 1 -sample_fmt s16 -af "loudnorm" output.wav
```

### Estructura de Fitxers SPIFFS
```
/samples/
  ├── kick.wav
  ├── snare.wav
  ├── hihat.wav
  ├── clap.wav
  ├── tom1.wav
  ├── tom2.wav
  ├── tom3.wav
  ├── crash.wav
  ├── ride.wav
  ├── perc1.wav
  ├── perc2.wav
  ├── perc3.wav
  ├── perc4.wav
  ├── perc5.wav
  ├── perc6.wav
  └── perc7.wav
```

### Pujar Samples a SPIFFS
1. Crea carpeta `data/samples/` al directori del sketch
2. Copia els WAV files a `data/samples/`
3. Tools -> ESP32 Sketch Data Upload
4. Espera que es completi la pujada

## Ús de la Drum Machine

### Interfície Principal (MODE_MAIN)
- Mostra grid de 16 pads (4x4)
- Pads carregats es mostren en gris fosc
- Pads actius (tocats) es mostren en verd
- Flash groc quan es toca un pad
- Stats a la part inferior: Veus actives, CPU%, PSRAM usada

### Controls de Navegació

**Des de pantalla principal:**
- **UP**: Canvia a mode VU Meter
- **DOWN**: Canvia a selecció de samples
- **SELECT**: Canvia a configuració

**Mode VU Meter:**
- Mostra nivells d'àudio en temps real
- **BACK**: Torna a pantalla principal

**Mode Sample Select:**
- **UP/DOWN**: Navega per la llista de samples
- **SELECT**: Carrega el sample seleccionat
- **BACK**: Torna a pantalla principal

### Funcionalitats Implementades

✅ Reproducció simultània de 16 veus
✅ Samples des de PSRAM per latència mínima
✅ Mixing en temps real amb soft clipping
✅ Suport per velocity (0-127)
✅ Interfície visual amb feedback en temps real
✅ VU meter per nivells d'àudio
✅ Gestió de samples dinàmica
✅ Dual-core: Core 0 UI, Core 1 Audio
✅ **Múltiples kits 808 amb canvi ràpid**
✅ **Web Interface per control des d'iPad/navegador**
✅ **WiFi Station o Access Point**

### Funcionalitats per Afegir (Futures)

- [ ] Sequencer de 16 steps
- [ ] Pitch shift per pad
- [ ] Efectes (reverb, delay, filter)
- [ ] MIDI input/output
- [ ] Gravació de patterns
- [ ] Save/Load presets a SPIFFS
- [ ] Velocity sense entrada via ADC o piezo sensors
- [ ] LFO per modulació

## Troubleshooting

### No hi ha àudio
1. Verifica connexions I2S
2. Comprova que els samples s'han carregat correctament (mira Serial Monitor)
3. Revisa que el PCM5102A té alimentació (LED encès)

### Pantalla en blanc
1. Verifica connexions SPI
2. Comprova configuració TFT_eSPI
3. Ajusta pins en `User_Setup.h`

### PSRAM no detectada
1. Assegura't que la placa té PSRAM
2. Selecciona "OPI PSRAM" a Arduino IDE
3. Mira Serial Monitor per confirmar PSRAM

### Samples no es carreguen
1. Verifica que els fitxers estan a `/samples/` en SPIFFS
2. Format correcte: WAV 16-bit mono/stereo 44100Hz
3. Mida < 512KB per sample

## Estructura del Codi

```
DrumMachine_ESP32S3/
├── DrumMachine_ESP32S3.ino  # Main file
├── AudioEngine.h/.cpp        # Motor d'àudio I2S
├── SampleManager.h/.cpp      # Gestió SPIFFS->PSRAM
├── DisplayManager.h/.cpp     # Pantalla ST7789
└── InputManager.h/.cpp       # Botons i navegació
```

## Llicència

MIT License - Cesco 2025
```
