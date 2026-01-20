# 🚀 Instal·lació Completa Arduino IDE - Drum Machine ESP32-S3

## ✅ SÍ! Arduino IDE Instal·la Tot Automàticament

Arduino IDE pot gestionar **totes** les dependències automàticament:
- ✅ Core ESP32 (suport per ESP32-S3)
- ✅ Llibreries (TFT_eSPI, Adafruit NeoPixel, etc.)
- ✅ Drivers USB (en alguns casos)

---

## 📥 Pas 1: Instal·lar Arduino IDE

### Descarrega Arduino IDE 2.x (Recomanat)

**Windows / Mac / Linux:**
https://www.arduino.cc/en/software

**Descàrrega la versió 2.3.x** (la més recent)

Avantatges Arduino IDE 2.x:
- ✨ Interfície moderna
- ⚡ Més ràpid
- 🔍 Millor autocompletat
- 📦 Gestor de llibreries integrat

---

## 🔧 Pas 2: Configurar ESP32 Core (AUTOMÀTIC)

### Mètode Fàcil - Board Manager

1. **Obre Arduino IDE**

2. **File → Preferences** (o Arduino IDE → Settings en Mac)

3. **Additional Boards Manager URLs**, afegeix:
   ```
   https://espressif.github.io/arduino-esp32/package_esp32_index.json
   ```
   
   Si ja tens altres URLs, separa amb comes o nova línia.

4. **Click OK**

5. **Tools → Board → Boards Manager**

6. **Busca**: `esp32`

7. **Instal·la**: `esp32 by Espressif Systems`
   - Versió recomanada: **3.0.0** o superior
   - Click **Install**
   
8. **Espera** (pot trigar 5-10 minuts, descarrega ~300MB)

9. **Done!** ✅

---

## 📚 Pas 3: Instal·lar Llibreries (AUTOMÀTIC)

### Opció A: Instal·lació Automàtica des del Codi

**Arduino IDE 2.x detecta llibreries que falten automàticament!**

1. **Obre** el fitxer `DrumMachine_ESP32S3.ino`

2. Arduino IDE detectarà:
   ```cpp
   #include <TFT_eSPI.h>          // ⚠️ Falta!
   #include <Adafruit_NeoPixel.h> // ⚠️ Falta!
   ```

3. Apareixerà un **missatge groc**:
   ```
   "TFT_eSPI.h: No such file or directory"
   ```

4. Click a **"Install missing libraries"** o similar

5. **Automàtic!** ✨

---

### Opció B: Instal·lació Manual (Recomanat per Control)

#### Llibreria 1: TFT_eSPI (Display ST7789)

```
Sketch → Include Library → Manage Libraries

Busca: TFT_eSPI
Autor: Bodmer
Versió: Última (2.5.x)

Click Install
```

**IMPORTANT:** Després cal configurar `User_Setup.h` (veure més avall)

---

#### Llibreria 2: Adafruit NeoPixel (LED RGB)

```
Sketch → Include Library → Manage Libraries

Busca: Adafruit NeoPixel
Autor: Adafruit
Versió: Última (1.12.x)

Click Install
```

**Això instal·larà també:**
- Adafruit BusIO (dependència automàtica)

---

#### Llibreria 3: WiFi, WebServer, ESPmDNS

**JA INCLOSES amb ESP32 Core!** ✅

No cal instal·lar res més. Aquestes llibreries vénen amb el core ESP32.

---

## ⚙️ Pas 4: Configurar TFT_eSPI

**IMPORTANT:** TFT_eSPI necessita configuració manual per ST7789.

### Trobar User_Setup.h

**Windows:**
```
C:\Users\[TeuUsuari]\Documents\Arduino\libraries\TFT_eSPI\User_Setup.h
```

**Mac:**
```
~/Documents/Arduino/libraries/TFT_eSPI/User_Setup.h
```

**Linux:**
```
~/Arduino/libraries/TFT_eSPI/User_Setup.h
```

### Editar User_Setup.h

1. **Obre** `User_Setup.h` amb un editor de text

2. **Comenta totes les línies** de driver:
   ```cpp
   // #define ILI9341_DRIVER
   // #define ILI9163_DRIVER
   // etc...
   ```

3. **Descomenta ST7789:**
   ```cpp
   #define ST7789_DRIVER      // ← Això ha d'estar descoment
   ```

4. **Configura resolució:**
   ```cpp
   #define TFT_WIDTH  240
   #define TFT_HEIGHT 240
   ```

5. **Descomenta pins** (si no estan):
   ```cpp
   #define TFT_MISO -1  // No usat
   #define TFT_MOSI 13
   #define TFT_SCLK 14
   #define TFT_CS   10
   #define TFT_DC   11
   #define TFT_RST  12
   ```

6. **Guarda** el fitxer

**Fitxer complet** disponible a: `TFT_eSPI_Config.txt` al projecte

---

## 🎯 Pas 5: Verificar Instal·lació

### Test Ràpid

1. **Arduino IDE**
2. **File → Examples → ESP32 → ChipID → GetChipID**
3. **Tools → Board → ESP32S3 Dev Module**
4. **Tools → Port → [El teu port]**
5. **Upload** (amb Mode BOOT si cal)

Si compila i puja → **Tot OK!** ✅

---

## 📋 Llista Completa de Dependències

### Core
- ✅ **ESP32 Core** (Espressif) - v3.0.0+

### Llibreries Requerides
- ✅ **TFT_eSPI** - v2.5.x
- ✅ **Adafruit NeoPixel** - v1.12.x (opcional, per LED RGB)

### Llibreries Incloses (No cal instal·lar)
- ✅ **WiFi** - Inclòs amb ESP32 core
- ✅ **WebServer** - Inclòs amb ESP32 core
- ✅ **ESPmDNS** - Inclòs amb ESP32 core
- ✅ **SPIFFS** - Inclòs amb ESP32 core
- ✅ **Wire** - Inclòs amb ESP32 core

---

## 🔧 Configuració Final Arduino IDE

### Settings Recomanades

**File → Preferences:**

```
☐ Compile output: Verbose
☐ Upload output: Verbose
☐ Show line numbers: ✓
☐ Aggressive warnings: ✓
☐ Update check: Daily
```

### Board Settings

**Tools →**

```
Board: "ESP32S3 Dev Module"
USB CDC On Boot: "Enabled" ⭐
USB Mode: "Hardware CDC and JTAG"
CPU Frequency: "240MHz (WiFi)"
Core Debug Level: "None"
Erase All Flash: "Disabled"
Events Run On: "Core 1"
Flash Mode: "QIO 80MHz"
Flash Size: "4MB (32Mb)"
JTAG Adapter: "Disabled"
Arduino Runs On: "Core 1"
Partition Scheme: "Default 4MB with spiffs (1.2MB APP/1.5MB SPIFFS)"
PSRAM: "OPI PSRAM" ⭐
Upload Mode: "UART0 / Hardware CDC"
Upload Speed: "921600"
USB Firmware MSC On Boot: "Disabled"
USB DFU On Boot: "Disabled"
```

---

## 📦 Instal·lació amb un Click (Script Automàtic)

### Per a Usuaris Avançats

Si prefereixes automatitzar tot:

**Arduino CLI (Command Line Interface)**

```bash
# Instal·lar Arduino CLI
curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh | sh

# Configurar
arduino-cli config init
arduino-cli config add board_manager.additional_urls https://espressif.github.io/arduino-esp32/package_esp32_index.json

# Instal·lar core
arduino-cli core update-index
arduino-cli core install esp32:esp32

# Instal·lar llibreries
arduino-cli lib install "TFT_eSPI"
arduino-cli lib install "Adafruit NeoPixel"

# Compilar
arduino-cli compile --fqbn esp32:esp32:esp32s3 DrumMachine_ESP32S3.ino

# Pujar
arduino-cli upload -p [PORT] --fqbn esp32:esp32:esp32s3 DrumMachine_ESP32S3.ino
```

---

## 🐛 Troubleshooting Instal·lació

### Error: "Board esp32:esp32:esp32s3 is unknown"

**Solució:**
```
Tools → Board → Boards Manager
Busca: esp32
Reinstal·la "esp32 by Espressif Systems"
```

### Error: "TFT_eSPI.h: No such file or directory"

**Solució:**
```
Sketch → Include Library → Manage Libraries
Busca: TFT_eSPI
Instal·la
```

### Error: "A fatal error occurred: Could not open port"

**Solució:**
```
1. Mode BOOT
2. Verifica driver USB
3. Tanca Serial Monitor si està obert
```

### Error al compilar TFT_eSPI

**Solució:**
```
Verifica User_Setup.h:
- ST7789_DRIVER descomentada
- Pins correctes
- TFT_WIDTH i TFT_HEIGHT configurats
```

### Error: "SPIFFS upload failed"

**Solució:**
```
1. Tanca Serial Monitor
2. Tools → Partition Scheme → "Default 4MB with spiffs"
3. Reconnecta ESP32-S3
4. Retry upload
```

---

## 📱 Instal·lació Alternativa: PlatformIO

Si Arduino IDE et dona problemes, PlatformIO és una alternativa excel·lent:

### Avantatges PlatformIO
- ✅ Gestió automàtica de dependències
- ✅ Millor control de versions
- ✅ Més estable per projectes grans
- ✅ Integrat amb VS Code

### Instal·lació Ràpida

1. **Instal·la VS Code**
   https://code.visualstudio.com/

2. **Extensions → PlatformIO IDE**

3. **Obre** el projecte (carpeta amb `platformio.ini`)

4. **Click** "Build" (icona ✓)

5. **Click** "Upload" (icona →)

**Tot automàtic!** ✨

El projecte ja inclou `platformio.ini` configurat.

---

## 🎬 Vídeo Tutorial (Recomanat)

**Instal·lació ESP32 Arduino IDE:**
https://randomnerdtutorials.com/installing-esp32-arduino-ide-2-0/

**Configurar TFT_eSPI:**
https://www.youtube.com/results?search_query=TFT_eSPI+configuration

---

## ✅ Checklist Final d'Instal·lació

```
Instal·lació Base:
☐ Arduino IDE 2.x descarregat i instal·lat
☐ ESP32 core instal·lat (Board Manager)
☐ Board "ESP32S3 Dev Module" disponible

Llibreries:
☐ TFT_eSPI instal·lada
☐ TFT_eSPI User_Setup.h configurat per ST7789
☐ Adafruit NeoPixel instal·lada (opcional)

Configuració:
☐ USB CDC On Boot = Enabled
☐ PSRAM = OPI PSRAM
☐ Partition = Default 4MB with spiffs
☐ Upload Speed = 921600

Test:
☐ Codi compila sense errors
☐ Upload funciona (amb Mode BOOT primer cop)
☐ Serial Monitor mostra output
```

---

## 🚀 Primer Upload - Pas a Pas

### Una Vegada Tot Instal·lat

```
1. Obre DrumMachine_ESP32S3.ino

2. Verifica configuració:
   Tools → Board → ESP32S3 Dev Module
   Tools → USB CDC On Boot → Enabled
   Tools → PSRAM → OPI PSRAM

3. Mode BOOT:
   Mantén BOOT + Connecta USB + Deixa anar

4. Selecciona port:
   Tools → Port → COMx (o /dev/tty...)

5. Upload:
   Sketch → Upload
   
6. Espera "Done uploading"

7. Prem RESET o reconnecta

8. Success! 🎉
```

---

## 💡 Consells Finals

### Optimització

```
Per compilació més ràpida:
File → Preferences → Compiler warnings → "None"
```

### Backup

```
Guarda configuració:
File → Preferences → Export Settings
```

### Actualitzacions

```
Mantén actualitzat:
- Arduino IDE (Help → Check for Updates)
- ESP32 core (Boards Manager)
- Llibreries (Library Manager)
```

---

## 📞 Ajuda Addicional

**Problemes amb instal·lació?**

1. **Fòrum Arduino:**
   https://forum.arduino.cc/

2. **ESP32 GitHub:**
   https://github.com/espressif/arduino-esp32/issues

3. **TFT_eSPI GitHub:**
   https://github.com/Bodmer/TFT_eSPI/discussions

---

Amb aquesta guia, Arduino IDE instal·larà **tot el necessari automàticament**! 🎉

**Temps total:** ~15-20 minuts (inclou descàrregues)

**Dificultat:** Fàcil 😊
