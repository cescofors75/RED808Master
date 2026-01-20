# 🔌 ESP32-S3 No Detectat - Solucions

## ❗ Problema Comú

L'ESP32-S3 té **DOS ports USB**:
1. **USB Native (USB-OTG)** - Pins USB del xip ESP32-S3
2. **USB-to-Serial (UART)** - Via xip USB-UART (CP2102, CH340, etc.)

Moltes plaques DevKit **només** tenen USB Native, que requereix configuració especial!

---

## 🎯 Solució Ràpida (Prova Primer)

### Mètode 1: Mode Boot (FUNCIONA SEMPRE)

1. **Desconnecta** USB-C
2. **Mantén premut** el botó **BOOT** (o IO0)
3. **Mentre mantens BOOT**, connecta USB-C
4. **Espera 2 segons**
5. **Deixa anar** BOOT
6. Ara hauria d'aparèixer el port COM

**El port apareixerà com:**
- Windows: `COMx` (COM3, COM4, etc.)
- Mac: `/dev/cu.usbmodem...`
- Linux: `/dev/ttyACM0`

---

## 🔍 Identificar el Teu Tipus de Placa

### Opció A: Placa amb USB-UART (CP2102/CH340)

```
┌─────────────────────┐
│  ESP32-S3          │
│                    │
│  [Xip USB-UART] ←──┼── USB-C
│                    │
└─────────────────────┘
```

**Identificació:**
- Té un xip petit al costat del USB-C
- Text: "CP2102", "CH340", "CH9102", etc.
- Port apareix automàticament

**Si tens això**: No necessites mode BOOT

---

### Opció B: Placa amb USB Native (Només ESP32-S3)

```
┌─────────────────────┐
│  ESP32-S3          │
│                    │
│  USB-OTG Pins ←────┼── USB-C
│                    │
└─────────────────────┘
```

**Identificació:**
- NO té xip USB-UART
- USB connectat directe a ESP32-S3
- Port NO apareix automàticament

**Si tens això**: Necessites mode BOOT

---

## 💻 Solucions per Sistema Operatiu

### Windows 10/11

#### Pas 1: Verificar Device Manager

1. Windows + X → **Device Manager**
2. Mira a:
   - **Ports (COM & LPT)** → Hauria d'haver-hi COMx
   - **Universal Serial Bus devices** → "USB Serial Device"
   - **Other devices** → "Unknown device" (mala senyal)

#### Pas 2: Instal·lar Drivers

**Si tens CP2102/CH340:**

```
Driver CP2102:
https://www.silabs.com/developers/usb-to-uart-bridge-vcp-drivers

Driver CH340:
https://sparks.gogo.co.nz/ch340.html
```

**Si tens USB Native:**
No cal driver! Però usa mode BOOT.

#### Pas 3: Mode BOOT i Upload

```
Arduino IDE:
1. Tools → Board → ESP32S3 Dev Module
2. Tools → USB Mode → Hardware CDC and JTAG
3. Tools → USB CDC On Boot → Enabled
4. Mantén BOOT + Connecta USB
5. Tools → Port → Selecciona COMx
6. Upload
```

---

### macOS

#### Pas 1: Verificar Port

```bash
# Terminal
ls /dev/cu.*

# Hauries de veure:
/dev/cu.usbmodem14201  (o similar)
# O si tens CH340:
/dev/cu.usbserial-14201
```

#### Pas 2: Driver CH340 (si cal)

```bash
# Descarrega i instal·la:
https://github.com/adrianmihalko/ch340g-ch34g-ch34x-mac-os-x-driver

# Verifica:
ls /dev/cu.*
```

#### Pas 3: Permisos

```bash
# Si tens problemes de permisos:
sudo chmod 666 /dev/cu.usbmodem*
```

---

### Linux (Ubuntu/Debian)

#### Pas 1: Verificar Port

```bash
# Terminal
ls /dev/ttyACM* /dev/ttyUSB*

# Hauries de veure:
/dev/ttyACM0  (USB Native)
# o
/dev/ttyUSB0  (CH340/CP2102)
```

#### Pas 2: Afegir Usuari a Grup dialout

```bash
# Això permet accés al port sense sudo
sudo usermod -a -G dialout $USER
sudo usermod -a -G plugdev $USER

# IMPORTANT: Reinicia o fes logout/login
```

#### Pas 3: Regles udev (si cal)

```bash
# Crea fitxer de regles
sudo nano /etc/udev/rules.d/99-esp32.rules

# Afegeix:
SUBSYSTEMS=="usb", ATTRS{idVendor}=="303a", ATTRS{idProduct}=="1001", MODE="0666"
SUBSYSTEMS=="usb", ATTRS{idVendor}=="1a86", ATTRS{idProduct}=="7523", MODE="0666"

# Recarrega regles
sudo udevadm control --reload-rules
sudo udevadm trigger
```

---

## 🔧 Configuració Arduino IDE

### Configuració Completa ESP32-S3

```
Tools →
  Board: "ESP32S3 Dev Module"
  USB Mode: "Hardware CDC and JTAG"
  USB CDC On Boot: "Enabled"
  USB Firmware MSC On Boot: "Disabled"
  USB DFU On Boot: "Disabled"
  Upload Mode: "UART0 / Hardware CDC"
  CPU Frequency: "240MHz (WiFi)"
  Flash Mode: "QIO 80MHz"
  Flash Size: "4MB (32Mb)"
  Partition Scheme: "Default 4MB with spiffs"
  Core Debug Level: "None"
  PSRAM: "OPI PSRAM"
  Arduino Runs On: "Core 1"
  Events Run On: "Core 1"
  Upload Speed: "921600"
  Port: [El teu COMx o /dev/tty...]
```

---

## 🚨 Procediment d'Upload amb Mode BOOT

### Pas a Pas Detallat

```
1. ABANS d'Upload:
   ☐ Mantén premut BOOT
   ☐ Prem i deixa anar RESET (o desconnecta/reconnecta USB)
   ☐ Deixa anar BOOT després de 1 segon

2. Arduino IDE:
   ☐ Sketch → Upload
   ☐ Espera "Connecting..."
   
3. Veuràs:
   "Connecting........_____....._____....."
   
4. Si connecta:
   "Writing at 0x00010000... (10%)"
   ✓ Success!

5. DESPRÉS del upload:
   ☐ Prem RESET per executar el programa
   O
   ☐ Desconnecta i reconnecta USB
```

---

## 🎯 Solucions a Problemes Específics

### Problema 1: "Port Gris" (No Seleccionable)

```
Causa: Port no detectat

Solució:
1. Mode BOOT + Reconnecta USB
2. Verifica drivers
3. Prova altre cable USB-C
4. Prova altre port USB del PC
```

### Problema 2: "Serial port not found"

```
Causa: Port desaparegut durant upload

Solució:
1. Tools → USB CDC On Boot → Enabled
2. Primer upload amb BOOT
3. Següents uploads no caldrà BOOT
```

### Problema 3: "Timed out waiting for packet header"

```
Causa: No està en mode boot

Solució:
1. Mantén BOOT + Prem RESET
2. Retry upload immediatament
```

### Problema 4: "A fatal error occurred: Failed to connect"

```
Causa: Velocitat de upload massa alta

Solució:
Tools → Upload Speed → "115200"
(Més lent però més fiable)
```

### Problema 5: Port apareix i desapareix

```
Causa: Cable USB dolent o contacte fluix

Solució:
1. Prova altre cable USB-C (amb dades!)
2. Neteja port USB-C de l'ESP32
3. Prova port USB 2.0 (no 3.0)
```

---

## 🔍 Test de Diagnòstic

### Script de Test Python

```python
# test_esp32_ports.py
import serial.tools.list_ports

print("Ports USB disponibles:")
ports = serial.tools.list_ports.comports()

for port in ports:
    print(f"\n  Port: {port.device}")
    print(f"  Desc: {port.description}")
    print(f"  VID:PID: {port.vid}:{port.pid}")
    
    # ESP32-S3 USB Native
    if port.vid == 0x303a and port.pid == 0x1001:
        print("  → ESP32-S3 USB Native detectat!")
    
    # CH340
    elif port.vid == 0x1a86:
        print("  → CH340 USB-UART detectat!")
    
    # CP2102
    elif port.vid == 0x10c4:
        print("  → CP2102 USB-UART detectat!")

if not ports:
    print("  Cap port detectat!")
    print("\n  Prova:")
    print("  1. Mode BOOT + Connecta USB")
    print("  2. Verifica cable USB")
    print("  3. Instal·la drivers")
```

---

## 📋 Checklist Complet

```
Hardware:
☐ Cable USB-C amb dades (no només càrrega)
☐ Port USB del PC funciona (prova altre dispositiu)
☐ LED power ESP32-S3 encès
☐ Botons BOOT i RESET accessibles

Drivers:
☐ CP2102/CH340 driver instal·lat (si cal)
☐ Device Manager mostra port COM (Windows)
☐ Usuari en grup dialout (Linux)

Arduino IDE:
☐ Board: ESP32S3 Dev Module
☐ USB CDC On Boot: Enabled
☐ PSRAM: OPI PSRAM
☐ Port seleccionat

Procediment:
☐ Mode BOOT (mantenir + connectar)
☐ Upload
☐ Reset després d'upload
```

---

## 🎓 Explicació Tècnica

### Per què passa això?

L'ESP32-S3 té **USB native** integrat, però:

1. **Al primer boot**: El bootloader espera per USB-UART tradicional
2. **Després d'upload**: El firmware usa USB native (CDC)
3. **Solució**: Forçar mode boot perquè bootloader agafi USB

**USB CDC On Boot = Enabled** fa que el firmware configuri USB native immediatament en arrencar, així en següents uploads no cal BOOT.

---

## 💡 Consells Finals

### Millor Pràctica

```
Primer upload del dia:
  → Mode BOOT obligatori

Següents uploads:
  → Automàtics (si USB CDC On Boot = Enabled)

Si canvies de PC o cable:
  → Mode BOOT de nou
```

### Cable USB Recomanat

- ✅ Cable curt (< 1 metre)
- ✅ Amb xip de dades
- ✅ USB-C a USB-A o USB-C
- ❌ Evita cables només de càrrega
- ❌ Evita hubs USB sense alimentació

### Ports USB Recomanats

- ✅ USB 2.0 del PC (més compatible)
- ❌ USB 3.0 (pot donar problemes)
- ❌ Hubs USB sense alimentació
- ✅ Port USB posterior del PC (millor alimentació)

---

## 🆘 Si Res Funciona

### Reset Factory del ESP32-S3

```
1. Mantén BOOT
2. Prem i deixa RESET
3. Deixa BOOT
4. Esborra flash:
   
   esptool.py --chip esp32s3 erase_flash
   
5. Torna a provar upload
```

### Verifica el Hardware

```
1. LED Power encès? → Alimentació OK
2. Botons BOOT/RESET funcionen? → Prova amb multímetre
3. USB-C ben connectat? → Prova resseguir
4. Placa original? → Clons poden tenir problemes
```

---

## 📞 Informació Extra

**Documentació Espressif:**
https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/

**Fòrum Arduino ESP32:**
https://github.com/espressif/arduino-esp32/issues

**Si continues amb problemes:**
Envia foto de:
- Device Manager (Windows)
- La placa (per identificar xip USB)
- Missatge d'error complet

---

Amb aquesta guia hauries de poder connectar l'ESP32-S3! 🚀

La clau està en el **Mode BOOT** per primer upload! 🔑
