# Compilar y Subir al ESP32-S3

## Configuración Actual

- **Placa**: ESP32-S3 DevKit C-1
- **Samples**: En carpetas `/01`, `/02`, `/03` con archivos `001.WAV` a `008.WAV`
- **Kits**: 3 kits automáticamente detectados

## Pasos para Compilar y Subir

### 1. Instalar PlatformIO (si no está instalado)

Desde VS Code:
- Instala la extensión "PlatformIO IDE"
- O desde terminal: `pip install platformio`

### 2. Subir el Filesystem (SPIFFS) con los Samples

Primero sube los archivos WAV a la memoria SPIFFS:

```bash
# Desde el terminal en VS Code
platformio run --target uploadfs
```

O desde la interfaz de PlatformIO:
- Click en el icono de PlatformIO en la barra lateral
- Project Tasks → esp32-s3-devkitc-1 → Platform → Upload Filesystem Image

**IMPORTANTE**: Los archivos en la carpeta `data/` serán subidos a SPIFFS:
- `/01/001.WAV` - `/01/008.WAV` → Kit 1
- `/02/001.WAV` - `/02/008.WAV` → Kit 2
- `/03/001.WAV` - `/03/008.WAV` → Kit 3

### 3. Compilar el Código

```bash
platformio run
```

O desde PlatformIO UI:
- Project Tasks → esp32-s3-devkitc-1 → General → Build

### 4. Subir el Código al ESP32-S3

```bash
platformio run --target upload
```

O desde PlatformIO UI:
- Project Tasks → esp32-s3-devkitc-1 → General → Upload

### 5. Monitor Serial

Para ver los mensajes del ESP32-S3:

```bash
platformio device monitor
```

O desde PlatformIO UI:
- Project Tasks → esp32-s3-devkitc-1 → General → Monitor

## Desde Arduino IDE (Alternativa)

Si prefieres usar Arduino IDE:

1. **Configurar Arduino IDE para ESP32-S3**:
   - File → Preferences → Additional Board Manager URLs
   - Añadir: `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`
   - Tools → Board → Boards Manager → Instalar "esp32" by Espressif

2. **Configuración de la Placa**:
   - Board: "ESP32S3 Dev Module"
   - USB CDC On Boot: "Enabled"
   - PSRAM: "OPI PSRAM"
   - Flash Mode: "QIO 80MHz"
   - Flash Size: "16MB (128Mb)"
   - Partition Scheme: "Default 4MB with spiffs"
   - Upload Speed: "921600"

3. **Subir SPIFFS**:
   - Instalar plugin: ESP32 Sketch Data Upload
   - Tools → ESP32 Sketch Data Upload
   - Esperar a que termine (puede tardar varios minutos)

4. **Compilar y Subir**:
   - Abrir `DrumMachine_ESP32S3.ino`
   - Sketch → Upload

## Configuración WiFi

Antes de subir, edita en `DrumMachine_ESP32S3.cpp` o `.ino`:

```cpp
#define WIFI_SSID "TU_WIFI_AQUI"
#define WIFI_PASSWORD "TU_PASSWORD_AQUI"
```

## Troubleshooting

### No se detecta el puerto USB
- Presiona el botón BOOT mientras conectas el USB
- O usa: `platformio run --target upload --upload-port /dev/ttyUSB0`

### Error de PSRAM
- Verifica que tu ESP32-S3 tenga PSRAM
- Revisa que `board_build.arduino.memory_type = opi_opi` esté en platformio.ini

### Samples no suenan
- Verifica que subiste el filesystem (paso 2)
- Revisa el monitor serial para ver si los archivos se cargan
- Comprueba que los archivos sean WAV válidos (16-bit, mono/stereo, 44.1kHz)

## Comandos Rápidos

```bash
# Todo en uno: Upload filesystem + compile + upload + monitor
platformio run --target uploadfs && platformio run --target upload && platformio device monitor

# Solo compilar para ver errores
platformio run

# Limpiar y recompilar
platformio run --target clean && platformio run
```
