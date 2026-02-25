# RED808 — Daisy Seed: Guía de implementación del Slave Audio
> Documento para el equipo de desarrollo del slave Daisy Seed
> Generado: 25/02/2026 — Verificado contra firmware ESP32-S3 v1.0

---

## ÍNDICE

1. [Resumen del proyecto](#1-resumen-del-proyecto)
2. [Hardware — Daisy Seed](#2-hardware--daisy-seed)
3. [Protocolo SPI — Formato de paquete](#3-protocolo-spi--formato-de-paquete)
4. [Transacción SPI — Flujo CS](#4-transacción-spi--flujo-cs)
5. [CRC16-Modbus](#5-crc16-modbus)
6. [Tabla completa de comandos](#6-tabla-completa-de-comandos)
7. [Payloads — Definición exacta de cada struct](#7-payloads--definición-exacta-de-cada-struct)
8. [Audio engine — Arquitectura requerida](#8-audio-engine--arquitectura-requerida)
9. [Implementación de referencia — red808_daisy.cpp](#9-implementación-de-referencia--red808_daisycpp)
10. [SD Card — Carga de kits desde la Daisy](#10-sd-card--carga-de-kits-desde-la-daisy)
11. [Per-Track FX Sends + Mixer](#11-per-track-fx-sends--mixer)
12. [Mapa DaisySP — Qué módulo usar para cada CMD](#12-mapa-daisysp--qué-módulo-usar-para-cada-cmd)
13. [Prioridades de implementación](#13-prioridades-de-implementación)
14. [Criterio de éxito](#14-criterio-de-éxito)
15. [protocol.h — Referencia completa de constantes](#15-protocolh--referencia-completa-de-constantes)

---

## 1. Resumen del proyecto

**RED808** es una drum machine basada en dos MCUs:

| Rol | MCU | Función |
|-----|-----|---------|
| **Master** | ESP32-S3 N16R8 | Web UI, secuenciador, MIDI, WiFi, control |
| **Slave** | **Daisy Seed** (STM32H750 + WM8731) | Motor de audio, DSP, efectos, samples |

La comunicación es **SPI**: el ESP32 (master) envía comandos, la Daisy (slave) reproduce audio.

**Ventajas de la Daisy Seed para este proyecto:**
- **64 MB SDRAM** — cabe todo el kit de samples en RAM (los samples son ~500 KB total)
- **WM8731 codec integrado** — audio estéreo 44.1/48 kHz sin hardware externo
- **480 MHz Cortex-M7** — DSP de sobra para 16+ voces + FX chain
- **DaisySP** — librería DSP con filtros, delay, reverb, compressor, chorus, etc.
- **libDaisy** — SPI slave DMA, AudioCallback, FatFS para SD card

---

## 2. Hardware — Daisy Seed

### 2.1 Pinout SPI — Daisy Seed ↔ ESP32-S3

| Señal    | Daisy Seed pin | Pin físico STM32H750 | ESP32-S3 GPIO | Notas |
|----------|----------------|----------------------|---------------|-------|
| SCK      | D10            | PC10 (SPI3_SCK)      | GPIO 12       | |
| MOSI     | D9             | PC11 (SPI3_MISO)⚠    | GPIO 11       | Daisy recibe aquí |
| MISO     | D8             | PC12 (SPI3_MOSI)⚠    | GPIO 13       | Daisy transmite aquí |
| NSS / CS | D7             | PA15 (SPI3_NSS)      | GPIO 10       | **Hardware NSS obligatorio** |
| GND      | GND            | GND                  | GND           | |

> ⚠ Verificar en tu Daisy que SPI3 usa PC11=MISO y PC12=MOSI. Alternativa: **SPI1** en D25/D26/D27/D28 (PA5/PA6/PA7/PA4).

- **SPI Mode 0** (CPOL=0, CPHA=0), MSB first, 8-bit
- **Clock:** 2 MHz durante bring-up → se subirá a **20 MHz** una vez estable
- **NSS mode:** Hardware — el flanco rising dispara la ISR del slave

### 2.2 Pinout SD Card (opcional pero recomendado)

La Daisy puede conectar una micro-SD via SDMMC (4-bit, mucho más rápido que SPI):

| Daisy pin | SD Card | Función |
|-----------|---------|---------|
| D18       | CLK     | SDMMC_CK |
| D19       | CMD     | SDMMC_CMD |
| D20       | DAT0    | SDMMC_D0 |
| D21       | DAT1    | SDMMC_D1 |
| D22       | DAT2    | SDMMC_D2 |
| D23       | DAT3    | SDMMC_D3 |
| 3V3       | VCC     | |
| GND       | GND     | |

### 2.3 Audio — WM8731 integrado

- Usar `seed.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_44KHZ)` — los samples del ESP32 son **44100 Hz, mono, 16-bit signed**
- `seed.SetAudioBlockSize(128)` — block size recomendado
- Salida estéreo: el AudioCallback escribe en `out[0]` (L) y `out[1]` (R)

---

## 3. Protocolo SPI — Formato de paquete

### Header (8 bytes, packed, little-endian)

```c
typedef struct __attribute__((packed)) {
    uint8_t  magic;       // 0xA5 = comando del Master, 0x5A = respuesta del Slave
    uint8_t  cmd;         // Código de comando (ver tabla Section 6)
    uint16_t length;      // Bytes de payload que siguen al header
    uint16_t sequence;    // Número de secuencia (el slave DEBE ecoarlo en respuesta)
    uint16_t checksum;    // CRC16-Modbus del payload (0 si length==0)
} SPIPacketHeader;        // 8 bytes total
```

### Reglas fundamentales

1. **Todo paquete empieza con el header de 8 bytes** + payload de `length` bytes
2. **Master envía magic=0xA5**, slave responde con magic=**0x5A**
3. **El campo `sequence` de la respuesta DEBE ser igual al del comando recibido**
4. **Checksum = CRC16-Modbus** del payload (no del header). Si `length==0`, checksum=0
5. **La mayoría de comandos son fire-and-forget** — el master envía, no espera respuesta
6. Solo PING, GET_PEAKS, GET_STATUS, GET_CPU_LOAD, GET_VOICES y los SD queries esperan respuesta

---

## 4. Transacción SPI — Flujo CS

### Comando sin respuesta (triggers, volumen, FX, samples)

```
ESP32 (master)                          Daisy (slave)
    │                                       │
    ├── CS LOW ──[ 8B header + NB payload ]── CS HIGH
    │                                       │ ← NSS rising ISR
    │                                       │   parsear + ejecutar
    │                                       │
    ├── (siguiente comando, ~100µs después)
```

### Comando con respuesta (PING, GET_PEAKS, SD queries)

```
ESP32 (master)                          Daisy (slave)
    │                                       │
    ├── CS LOW ── Txn1: [ 8B header + payload ] ── CS HIGH
    │                                       │ ← ISR: parsear, preparar txBuf
    │         [ ~500 µs pausa ]             │
    ├── CS LOW ── Txn2: [ master clockea, lee ] ── CS HIGH
    │                                       │ ← slave envía 0x5A + respuesta por MISO
```

**CRÍTICO:** La respuesta DEBE estar lista en txBuf **antes** de que llegue Txn2. Con 500µs a 480MHz hay tiempo de sobra.

**CRÍTICO:** NO llamar `DmaTransmit` desde dentro del callback DMA (ISR). Usar un flag y transmitir desde el **main loop**. Si no, el SPI queda en estado HAL_BUSY → corrupción → Hard Fault → reset.

---

## 5. CRC16-Modbus

```c
static uint16_t crc16(const uint8_t* data, uint16_t len) {
    uint16_t crc = 0xFFFF;
    for (uint16_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 1) crc = (crc >> 1) ^ 0xA001;
            else         crc >>= 1;
        }
    }
    return crc;
}
```

- Se calcula **solo sobre el payload** (sin incluir el header)
- Si `length == 0` → `checksum = 0`

---

## 6. Tabla completa de comandos

> El master ESP32 envía **70+ comandos**. Aquí está la lista completa con su valor hex, payload, y si requiere respuesta.

### 6.1 TRIGGERS (0x01–0x05) — sin respuesta

| CMD | Valor | Payload | Bytes | Descripción |
|-----|-------|---------|-------|-------------|
| TRIGGER_SEQ      | 0x01 | `TriggerSeqPayload`      | 8  | Trigger desde secuenciador |
| TRIGGER_LIVE     | 0x02 | `TriggerLivePayload`     | 2  | Trigger desde pad live |
| TRIGGER_STOP     | 0x03 | `uint8_t padIndex`       | 1  | Parar sample de un pad |
| TRIGGER_STOP_ALL | 0x04 | —                        | 0  | Parar todos los samples |
| TRIGGER_SIDECHAIN| 0x05 | `{pad, intensity}`       | 2  | Trigger sidechain envelope |

### 6.2 VOLUMEN (0x10–0x14) — sin respuesta

| CMD | Valor | Payload | Bytes | Descripción |
|-----|-------|---------|-------|-------------|
| MASTER_VOLUME | 0x10 | `uint8_t volume`  | 1 | Volumen master 0-100 |
| SEQ_VOLUME    | 0x11 | `uint8_t volume`  | 1 | Volumen secuenciador 0-150 |
| LIVE_VOLUME   | 0x12 | `uint8_t volume`  | 1 | Volumen live pads 0-180 |
| TRACK_VOLUME  | 0x13 | `{track, volume}` | 2 | Volumen por track 0-150 |
| LIVE_PITCH    | 0x14 | `float pitch`     | 4 | Pitch shift para live (0.25-3.0) |

### 6.3 FILTRO GLOBAL (0x20–0x26) — sin respuesta

| CMD | Valor | Payload | Bytes | Descripción |
|-----|-------|---------|-------|-------------|
| FILTER_SET       | 0x20 | `GlobalFilterPayload` | 24 | Configurar filtro completo |
| FILTER_CUTOFF    | 0x21 | `float Hz`            | 4  | Solo cutoff (20-20000 Hz) |
| FILTER_RESONANCE | 0x22 | `float Q`             | 4  | Solo resonancia (0.1-30.0) |
| FILTER_BITDEPTH  | 0x23 | `uint8_t bits`        | 1  | Bit crush (1-16 bits) |
| FILTER_DISTORTION| 0x24 | `float amount`        | 4  | Distorsión global (0-100) |
| FILTER_DIST_MODE | 0x25 | `uint8_t mode`        | 1  | Modo distorsión (0=soft,1=hard,2=tube,3=fuzz) |
| FILTER_SR_REDUCE | 0x26 | `uint32_t Hz`         | 4  | Sample rate reduction |

### 6.4 MASTER FX — DELAY (0x30–0x33) — sin respuesta

| CMD | Valor | Payload | Bytes | Descripción |
|-----|-------|---------|-------|-------------|
| DELAY_ACTIVE   | 0x30 | `uint8_t on/off`  | 1 | Activar delay |
| DELAY_TIME     | 0x31 | `uint16_t ms`     | 2 | Tiempo delay en ms |
| DELAY_FEEDBACK | 0x32 | `uint8_t 0-100`   | 1 | Feedback % |
| DELAY_MIX      | 0x33 | `uint8_t 0-100`   | 1 | Dry/wet mix % |

### 6.5 MASTER FX — PHASER (0x34–0x37) — sin respuesta

| CMD | Valor | Payload | Bytes | Descripción |
|-----|-------|---------|-------|-------------|
| PHASER_ACTIVE   | 0x34 | `uint8_t on/off` | 1 | Activar phaser |
| PHASER_RATE     | 0x35 | `uint8_t`        | 1 | LFO rate (Hz × 10) |
| PHASER_DEPTH    | 0x36 | `uint8_t 0-100`  | 1 | LFO depth % |
| PHASER_FEEDBACK | 0x37 | `uint8_t 0-100`  | 1 | Feedback % |

### 6.6 MASTER FX — FLANGER (0x38–0x3C) — sin respuesta

| CMD | Valor | Payload | Bytes | Descripción |
|-----|-------|---------|-------|-------------|
| FLANGER_ACTIVE   | 0x38 | `uint8_t on/off` | 1 | Activar flanger |
| FLANGER_RATE     | 0x39 | `uint8_t`        | 1 | LFO rate |
| FLANGER_DEPTH    | 0x3A | `uint8_t 0-100`  | 1 | Depth % |
| FLANGER_FEEDBACK | 0x3B | `uint8_t 0-100`  | 1 | Feedback % |
| FLANGER_MIX      | 0x3C | `uint8_t 0-100`  | 1 | Dry/wet % |

### 6.7 MASTER FX — COMPRESSOR (0x3D–0x42) — sin respuesta

| CMD | Valor | Payload | Bytes | Descripción |
|-----|-------|---------|-------|-------------|
| COMP_ACTIVE    | 0x3D | `uint8_t on/off`  | 1 | Activar compresor |
| COMP_THRESHOLD | 0x3E | `uint8_t dB`      | 1 | Threshold (valor × -1 = dB negativo) |
| COMP_RATIO     | 0x3F | `uint8_t ratio`   | 1 | Ratio (1-20) |
| COMP_ATTACK    | 0x40 | `uint8_t ms`      | 1 | Attack en ms |
| COMP_RELEASE   | 0x41 | `uint8_t ms`      | 1 | Release en ms |
| COMP_MAKEUP    | 0x42 | `uint8_t gain×10` | 1 | Makeup gain (× 0.1) |

### 6.8 MASTER FX — REVERB (0x43–0x46) — sin respuesta

| CMD | Valor | Payload | Bytes | Descripción |
|-----|-------|---------|-------|-------------|
| REVERB_ACTIVE   | 0x43 | `uint8_t` o `ReverbPayload` | 1 o 16 | On/off, o configuración completa |
| REVERB_FEEDBACK | 0x44 | `uint8_t 0-100`             | 1      | Room size / decay % |
| REVERB_LPFREQ   | 0x45 | `uint16_t Hz`               | 2      | LP filter (200-12000 Hz) |
| REVERB_MIX      | 0x46 | `uint8_t 0-100`             | 1      | Dry/wet % |

> **Nota REVERB_ACTIVE:** Si `length==1` → simple on/off. Si `length==16` → `ReverbPayload` completo con feedback, lpFreq and mix.

### 6.9 MASTER FX — CHORUS (0x47–0x4A) — sin respuesta

| CMD | Valor | Payload | Bytes | Descripción |
|-----|-------|---------|-------|-------------|
| CHORUS_ACTIVE | 0x47 | `uint8_t` o `ChorusPayload` | 1 o 16 | On/off o config completa |
| CHORUS_RATE   | 0x48 | `uint8_t rate×10`           | 1      | LFO rate Hz (div 10) |
| CHORUS_DEPTH  | 0x49 | `uint8_t 0-100`             | 1      | Depth % |
| CHORUS_MIX    | 0x4A | `uint8_t 0-100`             | 1      | Dry/wet % |

### 6.10 MASTER FX — TREMOLO (0x4B–0x4D) — sin respuesta

| CMD | Valor | Payload | Bytes | Descripción |
|-----|-------|---------|-------|-------------|
| TREMOLO_ACTIVE | 0x4B | `uint8_t` o `TremoloPayload` | 1 o 12 | On/off o config completa |
| TREMOLO_RATE   | 0x4C | `uint8_t rate×10`            | 1      | Rate Hz (div 10) |
| TREMOLO_DEPTH  | 0x4D | `uint8_t 0-100`              | 1      | Depth % |

### 6.11 MASTER FX — WAVEFOLDER + LIMITER (0x4E–0x4F) — sin respuesta

| CMD | Valor | Payload | Bytes | Descripción |
|-----|-------|---------|-------|-------------|
| WAVEFOLDER_GAIN | 0x4E | `uint8_t gain×10` | 1 | Gain (div 10), 1.0=off, >1.0=fold |
| LIMITER_ACTIVE  | 0x4F | `uint8_t on/off`  | 1 | Brick-wall limiter en 0dBFS |

### 6.12 PER-TRACK FX (0x50–0x58) — sin respuesta

| CMD | Valor | Payload | Bytes | Descripción |
|-----|-------|---------|-------|-------------|
| TRACK_FILTER      | 0x50 | `TrackFilterPayload`     | 20 | Filtro por track |
| TRACK_CLEAR_FILTER| 0x51 | `{track}`                | 1  | Reset filtro track |
| TRACK_DISTORTION  | 0x52 | `{track, mode, amount}`  | 8  | Distorsión por track |
| TRACK_BITCRUSH    | 0x53 | `{track, bits}`          | 2  | Bitcrush por track |
| TRACK_ECHO        | 0x54 | `TrackEchoPayload`       | 16 | Echo por track |
| TRACK_FLANGER_FX  | 0x55 | `TrackFlangerPayload`    | 16 | Flanger por track |
| TRACK_COMPRESSOR  | 0x56 | `TrackCompressorPayload` | 12 | Compresor por track |
| TRACK_CLEAR_LIVE  | 0x57 | `{track}`                | 1  | Clear live FX de track |
| TRACK_CLEAR_FX    | 0x58 | `{track}`                | 1  | Clear ALL FX de track |

### 6.13 PER-TRACK FX SENDS + MIXER (0x59–0x65) — sin respuesta

| CMD | Valor | Payload | Bytes | Descripción |
|-----|-------|---------|-------|-------------|
| TRACK_REVERB_SEND | 0x59 | `TrackSendPayload`     | 2 | Reverb send 0-100% |
| TRACK_DELAY_SEND  | 0x5A | `TrackSendPayload`     | 2 | Delay send 0-100% |
| TRACK_CHORUS_SEND | 0x5B | `TrackSendPayload`     | 2 | Chorus send 0-100% |
| TRACK_PAN         | 0x5C | `TrackPanPayload`      | 2 | Pan -100(L)..0..+100(R) |
| TRACK_MUTE        | 0x5D | `TrackMuteSoloPayload` | 2 | Mute on/off |
| TRACK_SOLO        | 0x5E | `TrackMuteSoloPayload` | 2 | Solo on/off |
| TRACK_PHASER      | 0x5F | `TrackFlangerPayload`  | 16| Phaser por track |
| TRACK_TREMOLO     | 0x60 | `{track, active, rate, depth}` | 4 | Tremolo por track |
| TRACK_PITCH       | 0x61 | `{track, int16_t cents}`| 3 | Pitch shift ±1200 cents |
| TRACK_GATE        | 0x62 | `TrackGatePayload`     | 16| Noise gate por track |
| TRACK_EQ_LOW      | 0x63 | `{track, int8_t dB}`   | 2 | EQ low shelf -12..+12 dB |
| TRACK_EQ_MID      | 0x64 | `{track, int8_t dB}`   | 2 | EQ mid peak -12..+12 dB |
| TRACK_EQ_HIGH     | 0x65 | `{track, int8_t dB}`   | 2 | EQ high shelf -12..+12 dB |

### 6.14 PER-PAD FX (0x70–0x7A) — sin respuesta

| CMD | Valor | Payload | Bytes | Descripción |
|-----|-------|---------|-------|-------------|
| PAD_FILTER       | 0x70 | `PadFilterPayload`       | 20 | Filtro por pad |
| PAD_CLEAR_FILTER | 0x71 | `{pad}`                  | 1  | Reset filtro pad |
| PAD_DISTORTION   | 0x72 | `PadDistortionPayload`   | 8  | Distorsión por pad |
| PAD_BITCRUSH     | 0x73 | `PadBitCrushPayload`     | 2  | Bitcrush por pad |
| PAD_LOOP         | 0x74 | `PadLoopPayload`         | 2  | Loop continuo on/off |
| PAD_REVERSE      | 0x75 | `PadReversePayload`      | 2  | Reverse on/off |
| PAD_PITCH        | 0x76 | `PadPitchPayload`        | 8  | Pitch shift (0.25-3.0) |
| PAD_STUTTER      | 0x77 | `PadStutterPayload`      | 4  | Stutter (retrigger cada N ms) |
| PAD_SCRATCH      | 0x78 | `PadScratchPayload`      | 20 | Vinyl scratch |
| PAD_TURNTABLISM  | 0x79 | `PadTurntablismPayload`  | 20 | DJ turntablism |
| PAD_CLEAR_FX     | 0x7A | `{pad}`                  | 1  | Reset ALL FX del pad |

### 6.15 SIDECHAIN (0x90–0x91) — sin respuesta

| CMD | Valor | Payload | Bytes | Descripción |
|-----|-------|---------|-------|-------------|
| SIDECHAIN_SET   | 0x90 | `SidechainPayload` | 20 | Config sidechain completa |
| SIDECHAIN_CLEAR | 0x91 | —                  | 0  | Desactivar sidechain |

### 6.16 SAMPLE TRANSFER (0xA0–0xA4) — sin respuesta

| CMD | Valor | Payload | Bytes | Descripción |
|-----|-------|---------|-------|-------------|
| SAMPLE_BEGIN     | 0xA0 | `SampleBeginPayload` | 12  | Inicio transferencia (info del sample) |
| SAMPLE_DATA      | 0xA1 | `SampleDataHeader` + PCM | 8+512 | Chunk de datos PCM (max 512B) |
| SAMPLE_END       | 0xA2 | `SampleEndPayload`   | 8   | Fin transferencia |
| SAMPLE_UNLOAD    | 0xA3 | `{padIndex}`         | 1   | Descargar sample de un pad |
| SAMPLE_UNLOAD_ALL| 0xA4 | —                    | 0   | Descargar todos los samples |

> Los samples llegan del ESP32 al arrancar. Son **PCM 16-bit signed, mono, 44100 Hz**. Se envían en chunks de max 512 bytes. La Daisy debe almacenarlos en SDRAM indexados por `padIndex`.

### 6.17 SD CARD — DAISY (0xB0–0xB9) — CON respuesta

| CMD | Valor | Payload enviado | Respuesta esperada | Descripción |
|-----|-------|-----------------|--------------------|-------------|
| SD_LIST_FOLDERS | 0xB0 | —                     | `SdFolderListResponse` (516B) | Listar carpetas de kits |
| SD_LIST_FILES   | 0xB1 | `SdListFilesPayload`  | `SdFileListResponse` (676B)   | Listar WAVs de una carpeta |
| SD_FILE_INFO    | 0xB2 | `SdFileInfoPayload`   | `SdFileInfoResponse` (44B)    | Info de un WAV |
| SD_LOAD_SAMPLE  | 0xB3 | `SdLoadSamplePayload` | — (fire-and-forget)           | Cargar WAV → pad slot |
| SD_LOAD_KIT     | 0xB4 | `SdLoadKitPayload`    | — (fire-and-forget)           | Cargar kit completo |
| SD_KIT_LIST     | 0xB5 | —                     | `SdKitListResponse` (516B)    | Lista de kits disponibles |
| SD_STATUS       | 0xB6 | —                     | `SdStatusResponse` (44B)      | Estado SD (presente, espacio) |
| SD_UNLOAD_KIT   | 0xB7 | —                     | — (fire-and-forget)           | Descargar kit de SDRAM |
| SD_GET_LOADED   | 0xB8 | —                     | `SdStatusResponse` (44B)      | Kit actualmente cargado |
| SD_ABORT        | 0xB9 | —                     | — (fire-and-forget)           | Cancelar carga SD |

> **IMPORTANTE:** Las respuestas SD grandes (516B, 676B) exceden el actual `TX_BUF_SIZE` de 76 bytes. Opciones:
> 1. Ampliar `TX_BUF_SIZE` a 768
> 2. Fragmentar respuestas grandes en múltiples transacciones

### 6.18 STATUS / QUERY (0xE0–0xEF) — CON respuesta

| CMD | Valor | Payload enviado | Respuesta esperada | Descripción |
|-----|-------|-----------------|--------------------|-------------|
| GET_STATUS  | 0xE0 | —              | `StatusResponse` (20B)  | Estado general |
| GET_PEAKS   | 0xE1 | —              | `PeaksResponse` (68B)   | VU meters 16 tracks + master |
| GET_CPU_LOAD| 0xE2 | —              | `CpuLoadResponse` (8B)  | CPU % + uptime |
| GET_VOICES  | 0xE3 | —              | `VoicesResponse` (4B)   | Voces activas |
| PING        | 0xEE | `PingPayload`  | `PongResponse` (8B)     | Echo timestamp + uptime |
| RESET       | 0xEF | —              | — (fire-and-forget)     | Reset completo DSP |

### 6.19 BULK (0xF0–0xF1) — sin respuesta

| CMD | Valor | Payload | Descripción |
|-----|-------|---------|-------------|
| BULK_TRIGGERS | 0xF0 | `BulkTriggersPayload` | Múltiples triggers en un paquete |
| BULK_FX       | 0xF1 | array de sub-commands  | Múltiples cambios FX en un paquete |

---

## 7. Payloads — Definición exacta de cada struct

> Todas son `__attribute__((packed))`, little-endian. Copiar directamente a vuestro código.

### 7.1 Triggers

```c
typedef struct __attribute__((packed)) {
    uint8_t  padIndex;       // 0-15 (sequencer track)
    uint8_t  velocity;       // 1-127
    uint8_t  trackVolume;    // 0-150
    uint8_t  reserved;       // siempre 0x00
    uint32_t maxSamples;     // 0 = sample completo, >0 = cortar después de N muestras
} TriggerSeqPayload;         // 8 bytes

typedef struct __attribute__((packed)) {
    uint8_t  padIndex;       // 0-23 (16 seq + 8 XTRA)
    uint8_t  velocity;       // 1-127
} TriggerLivePayload;        // 2 bytes

typedef struct __attribute__((packed)) {
    uint8_t  count;          // 1-16 triggers
    uint8_t  reserved;
    TriggerSeqPayload triggers[16];
} BulkTriggersPayload;
```

### 7.2 Volume

```c
// CMD_MASTER_VOLUME (0x10): 1 byte, uint8_t 0-100
// CMD_SEQ_VOLUME    (0x11): 1 byte, uint8_t 0-150
// CMD_LIVE_VOLUME   (0x12): 1 byte, uint8_t 0-180
// CMD_TRACK_VOLUME  (0x13): 2 bytes:
typedef struct __attribute__((packed)) {
    uint8_t  track;          // 0-15
    uint8_t  volume;         // 0-150
} TrackVolumePayload;

// CMD_LIVE_PITCH (0x14): 4 bytes, float 0.25-3.0
```

### 7.3 Global Filter

```c
typedef struct __attribute__((packed)) {
    uint8_t  filterType;     // 0=off, 1=LP, 2=HP, 3=BP, 4=Notch...
    uint8_t  distMode;       // 0=soft, 1=hard, 2=tube, 3=fuzz
    uint8_t  bitDepth;       // 1-16
    uint8_t  reserved;
    float    cutoff;         // Hz (20.0-20000.0)
    float    resonance;      // Q (0.1-30.0)
    float    distortion;     // 0.0-100.0
    uint32_t sampleRateReduce; // Hz
} GlobalFilterPayload;       // 24 bytes
```

### 7.4 Master FX (Reverb, Chorus, Tremolo)

```c
typedef struct __attribute__((packed)) {
    uint8_t  active;
    uint8_t  reserved[3];
    float    feedback;       // 0.0-1.0 (room size / decay)
    float    lpFreq;         // Hz (200-12000, damping)
    float    mix;            // 0.0-1.0 (dry/wet)
} ReverbPayload;             // 16 bytes

typedef struct __attribute__((packed)) {
    uint8_t  active;
    uint8_t  reserved[3];
    float    rate;           // Hz (0.1-10.0)
    float    depth;          // 0.0-1.0
    float    mix;            // 0.0-1.0
} ChorusPayload;             // 16 bytes

typedef struct __attribute__((packed)) {
    uint8_t  active;
    uint8_t  reserved[3];
    float    rate;           // Hz (0.1-20.0)
    float    depth;          // 0.0-1.0
} TremoloPayload;            // 12 bytes
```

### 7.5 Per-Track FX

```c
typedef struct __attribute__((packed)) {
    uint8_t  track;          // 0-15
    uint8_t  filterType;
    uint8_t  reserved[2];
    float    cutoff;
    float    resonance;
    float    gain;           // dB (peaking/shelf)
} TrackFilterPayload;        // 20 bytes

typedef struct __attribute__((packed)) {
    uint8_t  track;
    uint8_t  active;
    uint8_t  reserved[2];
    float    time;           // ms (10-200)
    float    feedback;       // 0-90%
    float    mix;            // 0-100%
} TrackEchoPayload;          // 16 bytes

typedef struct __attribute__((packed)) {
    uint8_t  track;
    uint8_t  active;
    uint8_t  reserved[2];
    float    rate;           // 0-100%
    float    depth;          // 0-100%
    float    feedback;       // 0-100%
} TrackFlangerPayload;       // 16 bytes

typedef struct __attribute__((packed)) {
    uint8_t  track;
    uint8_t  active;
    uint8_t  reserved[2];
    float    threshold;      // dB
    float    ratio;          // 1.0-20.0
} TrackCompressorPayload;    // 12 bytes
```

### 7.6 Per-Track Sends + Mixer + EQ

```c
typedef struct __attribute__((packed)) {
    uint8_t  track;          // 0-15
    uint8_t  sendLevel;      // 0-100 (percentage)
} TrackSendPayload;          // 2 bytes (para 0x59, 0x5A, 0x5B)

typedef struct __attribute__((packed)) {
    uint8_t  track;          // 0-15
    int8_t   pan;            // -100 (full L) .. 0 (center) .. +100 (full R)
} TrackPanPayload;           // 2 bytes

typedef struct __attribute__((packed)) {
    uint8_t  track;          // 0-15
    uint8_t  enabled;        // 0/1
} TrackMuteSoloPayload;      // 2 bytes (para 0x5D, 0x5E)

typedef struct __attribute__((packed)) {
    uint8_t  track;          // 0-15
    int8_t   gainLow;        // -12..+12 dB
    int8_t   gainMid;        // -12..+12 dB
    int8_t   gainHigh;       // -12..+12 dB
} TrackEqPayload;            // 4 bytes

typedef struct __attribute__((packed)) {
    uint8_t  track;          // 0-15
    uint8_t  active;         // 0/1
    uint8_t  reserved[2];
    float    threshold;      // dB (-60..-6)
    float    attackMs;       // 0.1-50
    float    releaseMs;      // 10-500
} TrackGatePayload;          // 16 bytes
```

### 7.7 Per-Pad FX

```c
typedef struct __attribute__((packed)) {
    uint8_t  pad;
    uint8_t  enabled;        // 0/1
} PadLoopPayload;            // 2 bytes

typedef struct __attribute__((packed)) {
    uint8_t  pad;
    uint8_t  reverse;        // 0/1
} PadReversePayload;         // 2 bytes

typedef struct __attribute__((packed)) {
    uint8_t  pad;
    uint8_t  reserved;
    uint16_t reserved2;
    float    pitch;          // 0.25-3.0
} PadPitchPayload;           // 8 bytes

typedef struct __attribute__((packed)) {
    uint8_t  pad;
    uint8_t  active;
    uint16_t intervalMs;
} PadStutterPayload;         // 4 bytes

typedef struct __attribute__((packed)) {
    uint8_t  pad;
    uint8_t  active;
    uint8_t  reserved[2];
    float    rate;           // Hz (0.5-20.0)
    float    depth;          // 0.0-1.0
    float    filterCutoff;   // Hz (500-12000)
    float    crackle;        // 0.0-1.0
} PadScratchPayload;         // 20 bytes

typedef struct __attribute__((packed)) {
    uint8_t  pad;
    uint8_t  active;
    uint8_t  autoMode;
    int8_t   mode;           // -1=auto, 0-3=manual
    uint16_t brakeMs;
    uint16_t backspinMs;
    float    transformRate;
    float    vinylNoise;
} PadTurntablismPayload;     // 20 bytes
```

### 7.8 Sidechain

```c
typedef struct __attribute__((packed)) {
    uint8_t  active;
    uint8_t  sourceTrack;    // 0-15
    uint16_t destMask;       // Bitmask tracks afectados
    float    amount;         // 0.0-1.0
    float    attackMs;       // 0.1-80.0
    float    releaseMs;      // 10.0-1200.0
    float    knee;           // 0.0-1.0
} SidechainPayload;          // 20 bytes
```

### 7.9 Sample Transfer

```c
typedef struct __attribute__((packed)) {
    uint8_t  padIndex;       // 0-23
    uint8_t  bitsPerSample;  // siempre 16
    uint16_t sampleRate;     // siempre 44100
    uint32_t totalBytes;     // tamaño total PCM en bytes
    uint32_t totalSamples;   // total muestras int16_t (= totalBytes / 2)
} SampleBeginPayload;        // 12 bytes

typedef struct __attribute__((packed)) {
    uint8_t  padIndex;
    uint8_t  reserved;
    uint16_t chunkSize;      // bytes PCM en este chunk (max 512)
    uint32_t offset;         // offset en bytes desde el inicio
    // int16_t data[] sigue aquí (hasta 256 muestras = 512 bytes)
} SampleDataHeader;          // 8 bytes + hasta 512 bytes audio

typedef struct __attribute__((packed)) {
    uint8_t  padIndex;
    uint8_t  status;         // 0=OK
    uint16_t reserved;
    uint32_t checksum;       // CRC del total de datos PCM
} SampleEndPayload;          // 8 bytes
```

### 7.10 SD Card

```c
typedef struct __attribute__((packed)) {
    char     folderName[32]; // null-terminated
} SdListFilesPayload;       // 32 bytes

typedef struct __attribute__((packed)) {
    uint8_t  padIndex;
    uint8_t  reserved[3];
    char     folderName[32];
    char     fileName[32];
} SdFileInfoPayload;         // 68 bytes

typedef struct __attribute__((packed)) {
    uint8_t  padIndex;       // 0-23 destino
    uint8_t  reserved[3];
    char     folderName[32];
    char     fileName[32];
} SdLoadSamplePayload;       // 68 bytes

typedef struct __attribute__((packed)) {
    char     kitName[32];
    uint8_t  startPad;       // primer slot (normalmente 0)
    uint8_t  maxPads;        // máx pads a cargar (16 o 24)
    uint8_t  reserved[2];
} SdLoadKitPayload;          // 36 bytes
```

### 7.11 SD Card — Respuestas (Daisy → ESP32)

```c
typedef struct __attribute__((packed)) {
    uint8_t  count;          // número de carpetas (max 16)
    uint8_t  reserved[3];
    char     names[16][32];  // nombres null-terminated
} SdFolderListResponse;      // 516 bytes

typedef struct __attribute__((packed)) {
    uint8_t  count;          // número de archivos (max 24)
    uint8_t  reserved[3];
    struct __attribute__((packed)) {
        char     name[24];
        uint32_t sizeBytes;
    } files[24];
} SdFileListResponse;        // 676 bytes

typedef struct __attribute__((packed)) {
    uint32_t totalSamples;
    uint16_t sampleRate;
    uint8_t  bitsPerSample;
    uint8_t  channels;       // 1=mono, 2=stereo
    uint32_t durationMs;
    char     name[32];
} SdFileInfoResponse;        // 44 bytes

typedef struct __attribute__((packed)) {
    uint8_t  present;        // 0=no SD, 1=present
    uint8_t  reserved[3];
    uint32_t totalMB;
    uint32_t freeMB;
    uint32_t samplesLoaded;  // bitmask de pads cargados
    char     currentKit[32];
} SdStatusResponse;          // 44 bytes

typedef struct __attribute__((packed)) {
    uint8_t  count;
    uint8_t  reserved[3];
    char     kits[16][32];   // hasta 16 nombres de kits
} SdKitListResponse;         // 516 bytes
```

### 7.12 Status Responses

```c
typedef struct __attribute__((packed)) {
    float trackPeaks[16];    // 0.0-1.0 por track
    float masterPeak;        // 0.0-1.0
} PeaksResponse;             // 68 bytes

typedef struct __attribute__((packed)) {
    uint8_t  activeVoices;
    uint8_t  cpuLoadPercent;
    uint16_t freeSRAM;       // KB libres
    uint32_t samplesLoaded;  // bitmask de pads cargados
    uint32_t uptime;         // segundos desde boot
    uint16_t spiErrors;
    uint16_t bufferUnderruns;
} StatusResponse;            // 20 bytes

typedef struct __attribute__((packed)) {
    uint32_t timestamp;      // micros() del ESP32
} PingPayload;               // 4 bytes

typedef struct __attribute__((packed)) {
    uint32_t echoTimestamp;  // mismo valor recibido
    uint32_t stm32Uptime;   // HAL_GetTick() o seed.system.GetNow()
} PongResponse;              // 8 bytes
```

---

## 8. Audio engine — Arquitectura requerida

### 8.1 Samples en SDRAM

```cpp
#define MAX_PADS          24
#define MAX_SAMPLE_BYTES  (96000 * 2)   // ~2s a 48kHz, 16-bit

DSY_SDRAM_BSS static int16_t sampleStorage[MAX_PADS][MAX_SAMPLE_BYTES / 2];
static uint32_t sampleLength[MAX_PADS];  // en muestras int16_t
static bool     sampleLoaded[MAX_PADS];
```

Los samples del ESP32 son **PCM mono signed 16-bit 44100Hz**. Caben perfectamente en los 64MB de SDRAM.

### 8.2 Voces polifónicas

```cpp
#define MAX_VOICES  10  // mínimo 10, se puede subir a 16-24

struct Voice {
    bool     active;
    uint8_t  pad;       // qué sample reproduce
    float    pos;       // posición fractional (para pitch/interpolación)
    float    speed;     // 1.0 = normal, >1 = rápido, <1 = lento
    float    gainL;     // ganancia final 0.0-1.5
};
```

### 8.3 Cadena de señal

```
Trigger (0x01/0x02) → Asignar voz → play desde sampleStorage[]

Voz activa → leer sample con interpolación lineal
           → aplicar pad FX (filter, distortion, bitcrush, loop, reverse, pitch)
           → aplicar track FX (filter, echo, compressor, EQ 3-band)
           → multiplicar por trackGain[pad] × velocity
           │
           ├──► Mix directo → Pan L/R → Master bus
           ├──► Reverb send × trackReverbSend[pad] → Reverb bus
           ├──► Delay send × trackDelaySend[pad]  → Delay bus
           └──► Chorus send × trackChorusSend[pad] → Chorus bus

Master bus + FX buses → Master FX chain:
  → Global Filter (Biquad + Overdrive + Decimator)
  → Delay
  → Compressor
  → WaveFolder
  → Tremolo
  → Phaser
  → Chorus
  → Reverb (stereo)
  → Limiter (soft clip / brick-wall)
  → masterGain
  → out[0] (L), out[1] (R)
```

### 8.4 Sample rate

**Configurar Daisy a 44100 Hz** (los samples del ESP32 son 44100):

```cpp
seed.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_44KHZ);
```

---

## 9. Implementación de referencia — red808_daisy.cpp

> Este archivo único contiene una implementación funcional del slave. Úsalo como punto de partida. Tiene implementados: **triggers, volumen, samples, ping, peaks, delay, compressor, reverb, chorus, tremolo, wavefolder, limiter, loop, reverse, pitch**.

```cpp
#include "daisy_seed.h"
#include "daisysp.h"
#include <string.h>
#include <math.h>

using namespace daisy;
using namespace daisysp;

// ─── Hardware ────────────────────────────────────────────────────────────────
DaisySeed seed;
SpiHandle spi_slave;

// ─── Protocolo RED808 ─────────────────────────────────────────────────────────
#define SPI_MAGIC_CMD        0xA5
#define SPI_MAGIC_RESP       0x5A
#define CMD_TRIGGER_SEQ      0x01
#define CMD_TRIGGER_LIVE     0x02
#define CMD_TRIGGER_STOP     0x03
#define CMD_TRIGGER_STOP_ALL 0x04
#define CMD_TRIGGER_SIDECHAIN 0x05
#define CMD_MASTER_VOLUME    0x10
#define CMD_TRACK_VOLUME     0x13
#define CMD_DELAY_ACTIVE     0x30
#define CMD_DELAY_TIME       0x31
#define CMD_DELAY_FEEDBACK   0x32
#define CMD_DELAY_MIX        0x33
#define CMD_COMP_ACTIVE      0x3D
#define CMD_COMP_THRESHOLD   0x3E
#define CMD_COMP_RATIO       0x3F
#define CMD_COMP_ATTACK      0x40
#define CMD_COMP_RELEASE     0x41
#define CMD_COMP_MAKEUP      0x42
#define CMD_REVERB_ACTIVE    0x43
#define CMD_REVERB_FEEDBACK  0x44
#define CMD_REVERB_LPFREQ    0x45
#define CMD_REVERB_MIX       0x46
#define CMD_CHORUS_ACTIVE    0x47
#define CMD_CHORUS_RATE      0x48
#define CMD_CHORUS_DEPTH     0x49
#define CMD_CHORUS_MIX       0x4A
#define CMD_TREMOLO_ACTIVE   0x4B
#define CMD_TREMOLO_RATE     0x4C
#define CMD_TREMOLO_DEPTH    0x4D
#define CMD_WAVEFOLDER_GAIN  0x4E
#define CMD_LIMITER_ACTIVE   0x4F
#define CMD_PAD_LOOP         0x74
#define CMD_PAD_REVERSE      0x75
#define CMD_PAD_PITCH        0x76
#define CMD_SAMPLE_BEGIN     0xA0
#define CMD_SAMPLE_DATA      0xA1
#define CMD_SAMPLE_END       0xA2
#define CMD_SAMPLE_UNLOAD    0xA3
#define CMD_GET_STATUS       0xE0
#define CMD_GET_PEAKS        0xE1
#define CMD_GET_CPU_LOAD     0xE2
#define CMD_GET_VOICES       0xE3
#define CMD_PING             0xEE
#define CMD_RESET            0xEF
#define CMD_BULK_TRIGGERS    0xF0

struct __attribute__((packed)) SPIPacketHeader {
    uint8_t  magic;
    uint8_t  cmd;
    uint16_t length;
    uint16_t sequence;
    uint16_t checksum;
};

#define RX_BUF_SIZE  536
#define TX_BUF_SIZE  76
static uint8_t rxBuf[RX_BUF_SIZE];
static uint8_t txBuf[TX_BUF_SIZE];
static volatile bool  waitingPayload     = false;
static volatile bool  pendingResponse    = false;
static uint16_t       pendingTxLen       = 0;

// ─── Samples en SDRAM (64 MB) ────────────────────────────────────────────────
#define MAX_PADS          24
#define MAX_SAMPLE_BYTES  (96000 * 2)

DSY_SDRAM_BSS static int16_t sampleStorage[MAX_PADS][MAX_SAMPLE_BYTES / 2];
static uint32_t sampleLength[MAX_PADS];
static uint32_t sampleTotalSamples[MAX_PADS];
static bool     sampleLoaded[MAX_PADS];

// ─── Voces polifónicas ────────────────────────────────────────────────────────
#define MAX_VOICES  10
struct Voice {
    bool     active;
    uint8_t  pad;
    float    pos;
    float    speed;
    float    gainL;
};
static Voice voices[MAX_VOICES];

// ─── Volúmenes ────────────────────────────────────────────────────────────────
static float masterGain   = 1.0f;
static float trackGain[MAX_PADS];

// ─── Peaks ────────────────────────────────────────────────────────────────────
static volatile float trackPeak[MAX_PADS];
static volatile float masterPeak = 0.0f;

// ─── DaisySP Master FX ───────────────────────────────────────────────────────
#define MAX_DELAY_SAMPLES  88200
static DelayLine<float, MAX_DELAY_SAMPLES> DSY_SDRAM_BSS masterDelay;
static ReverbSc   masterReverb;
static Chorus     masterChorus;
static Tremolo    masterTremolo;
static Compressor masterComp;
static Fold       masterFold;

static bool  delayActive   = false;
static float delayTime     = 250.0f;
static float delayFeedback = 0.3f;
static float delayMix      = 0.3f;

static bool  reverbActive   = false;
static float reverbFeedback = 0.85f;
static float reverbLpFreq   = 8000.0f;
static float reverbMix      = 0.3f;

static bool  chorusActive = false;
static float chorusMix    = 0.4f;

static bool  tremoloActive = false;
static bool  compActive    = false;

static float waveFolderGain = 1.0f;
static bool  limiterActive  = false;

static bool  padLoop[MAX_PADS]    = {false};
static bool  padReverse[MAX_PADS] = {false};
static float padPitch[MAX_PADS];

// ─── CRC16 Modbus ─────────────────────────────────────────────────────────────
static uint16_t crc16(const uint8_t* d, uint16_t len) {
    uint16_t crc = 0xFFFF;
    for (uint16_t i = 0; i < len; i++) {
        crc ^= d[i];
        for (uint8_t j = 0; j < 8; j++)
            crc = (crc & 1) ? (crc >> 1) ^ 0xA001 : (crc >> 1);
    }
    return crc;
}

// ─── Audio Callback ──────────────────────────────────────────────────────────
void AudioCallback(AudioHandle::InputBuffer  /*in*/,
                   AudioHandle::OutputBuffer  out,
                   size_t                     size)
{
    for (size_t i = 0; i < size; i++) out[0][i] = out[1][i] = 0.0f;

    float mixPeak = 0.0f;

    for (int v = 0; v < MAX_VOICES; v++) {
        Voice& vx = voices[v];
        if (!vx.active) continue;

        for (size_t i = 0; i < size; i++) {
            uint32_t idx = (uint32_t)vx.pos;

            if (padReverse[vx.pad]) {
                if (vx.pos < 0.0f) {
                    if (padLoop[vx.pad]) vx.pos = (float)(sampleLength[vx.pad] - 1);
                    else { vx.active = false; break; }
                }
            } else {
                if (idx >= sampleLength[vx.pad]) {
                    if (padLoop[vx.pad]) { vx.pos = 0.0f; idx = 0; }
                    else { vx.active = false; break; }
                }
            }
            idx = (uint32_t)fabsf(vx.pos);
            if (idx >= sampleLength[vx.pad]) { vx.active = false; break; }

            float frac = fabsf(vx.pos) - idx;
            float s0   = sampleStorage[vx.pad][idx];
            float s1   = (idx + 1 < sampleLength[vx.pad])
                         ? sampleStorage[vx.pad][idx + 1] : 0.0f;
            float s    = (s0 + frac * (s1 - s0)) / 32768.0f;
            float fout = s * vx.gainL;

            out[0][i] += fout;
            out[1][i] += fout;

            float spd = vx.speed * padPitch[vx.pad];
            vx.pos += padReverse[vx.pad] ? -spd : spd;

            float absv = fabsf(fout);
            if (absv > trackPeak[vx.pad]) trackPeak[vx.pad] = absv;
            if (absv > mixPeak)           mixPeak = absv;
        }
    }

    // Master FX chain
    for (size_t i = 0; i < size; i++) {
        float L = out[0][i] * masterGain;
        float R = L;

        if (delayActive) {
            float wet = masterDelay.Read();
            masterDelay.Write(L + wet * delayFeedback);
            L = L * (1.0f - delayMix) + wet * delayMix;
        }

        if (compActive) L = masterComp.Process(L);

        if (waveFolderGain > 1.01f) {
            masterFold.SetIncrement(waveFolderGain);
            L = masterFold.Process(L);
        }

        if (tremoloActive) L = masterTremolo.Process(L);

        if (chorusActive) {
            float wet = masterChorus.Process(L);
            L = L * (1.0f - chorusMix) + wet * chorusMix;
        }

        float revL = 0.0f, revR = 0.0f;
        if (reverbActive) {
            masterReverb.Process(L, L, &revL, &revR);
            L = L * (1.0f - reverbMix) + revL * reverbMix;
            R = L * (1.0f - reverbMix) + revR * reverbMix;
        } else {
            R = L;
        }

        if (limiterActive) {
            L = fclamp(L, -1.0f, 1.0f);
            R = fclamp(R, -1.0f, 1.0f);
        } else {
            L = tanhf(L);
            R = tanhf(R);
        }

        out[0][i] = L;
        out[1][i] = R;
    }
    masterPeak = mixPeak * masterGain;
}

// ─── Trigger ─────────────────────────────────────────────────────────────────
static void TriggerPad(uint8_t pad, uint8_t velocity,
                       uint8_t trkVol = 100, uint32_t maxSamples = 0)
{
    if (pad >= MAX_PADS || !sampleLoaded[pad]) return;
    int slot = -1;
    for (int i = 0; i < MAX_VOICES; i++)
        if (!voices[i].active) { slot = i; break; }
    if (slot < 0) slot = 0;   // voice stealing

    voices[slot].active = true;
    voices[slot].pad    = pad;
    voices[slot].pos    = padReverse[pad] ? (float)(sampleLength[pad] - 1) : 0.0f;
    voices[slot].speed  = 1.0f;
    voices[slot].gainL  = (velocity / 127.0f) * (trkVol / 100.0f);
}

// ─── Build Response ──────────────────────────────────────────────────────────
static void BuildResponse(uint8_t cmd, uint16_t seq,
                          const uint8_t* payload, uint16_t payloadLen) {
    SPIPacketHeader* r = (SPIPacketHeader*)txBuf;
    r->magic    = SPI_MAGIC_RESP;
    r->cmd      = cmd;
    r->length   = payloadLen;
    r->sequence = seq;
    r->checksum = payloadLen ? crc16(payload, payloadLen) : 0;
    if (payloadLen && payload) memcpy(txBuf + 8, payload, payloadLen);
    pendingTxLen    = 8 + payloadLen;
    pendingResponse = true;
    // NUNCA transmitir desde ISR — se hace en main loop
}

// ─── Process Command ─────────────────────────────────────────────────────────
static void ProcessCommand() {
    SPIPacketHeader* hdr = (SPIPacketHeader*)rxBuf;
    uint8_t* p = rxBuf + 8;

    switch (hdr->cmd) {

        // ── PING ────────────────────────────────────────────────────────
        case CMD_PING: {
            uint32_t echo = 0, uptime = seed.system.GetNow();
            if (hdr->length >= 4) memcpy(&echo, p, 4);
            uint8_t pong[8];
            memcpy(pong,     &echo,   4);
            memcpy(pong + 4, &uptime, 4);
            BuildResponse(CMD_PING, hdr->sequence, pong, 8);
            return;
        }

        // ── GET_PEAKS ───────────────────────────────────────────────────
        case CMD_GET_PEAKS: {
            float buf[17];
            for (int i = 0; i < 16; i++) { buf[i] = trackPeak[i]; trackPeak[i] = 0.0f; }
            buf[16] = masterPeak;
            BuildResponse(CMD_GET_PEAKS, hdr->sequence, (uint8_t*)buf, 68);
            return;
        }

        // ── TRIGGERS ────────────────────────────────────────────────────
        case CMD_TRIGGER_LIVE:
            if (hdr->length >= 2) TriggerPad(p[0], p[1]);
            break;

        case CMD_TRIGGER_SEQ:
            if (hdr->length >= 8) {
                uint32_t maxS = 0; memcpy(&maxS, p + 4, 4);
                TriggerPad(p[0], p[1], p[2], maxS);
            }
            break;

        case CMD_TRIGGER_STOP:
            if (hdr->length >= 1)
                for (int v = 0; v < MAX_VOICES; v++)
                    if (voices[v].pad == p[0]) voices[v].active = false;
            break;

        case CMD_TRIGGER_STOP_ALL:
            for (int v = 0; v < MAX_VOICES; v++) voices[v].active = false;
            break;

        // ── VOLUME ──────────────────────────────────────────────────────
        case CMD_MASTER_VOLUME:
            if (hdr->length >= 1) masterGain = p[0] / 100.0f;
            break;

        case CMD_TRACK_VOLUME:
            if (hdr->length >= 2 && p[0] < MAX_PADS) trackGain[p[0]] = p[1] / 100.0f;
            break;

        // ── SAMPLE TRANSFER ─────────────────────────────────────────────
        case CMD_SAMPLE_BEGIN:
            if (hdr->length >= 12) {
                uint8_t pad = p[0];
                uint32_t ts = 0; memcpy(&ts, p + 8, 4);
                sampleTotalSamples[pad] = ts;
                sampleLength[pad] = 0;
                sampleLoaded[pad] = false;
            }
            break;

        case CMD_SAMPLE_DATA:
            if (hdr->length >= 8) {
                uint8_t pad = p[0];
                uint16_t chunkSize = 0; uint32_t offset = 0;
                memcpy(&chunkSize, p + 2, 2);
                memcpy(&offset,    p + 4, 4);
                uint32_t startSample = offset / 2;
                uint16_t numSamples  = chunkSize / 2;
                if (pad < MAX_PADS && startSample + numSamples <= MAX_SAMPLE_BYTES / 2)
                    memcpy(&sampleStorage[pad][startSample], p + 8, chunkSize);
            }
            break;

        case CMD_SAMPLE_END:
            if (hdr->length >= 1) {
                uint8_t pad = p[0];
                if (pad < MAX_PADS) {
                    sampleLength[pad] = sampleTotalSamples[pad];
                    sampleLoaded[pad] = true;
                }
            }
            break;

        case CMD_SAMPLE_UNLOAD:
            if (hdr->length >= 1 && p[0] < MAX_PADS) {
                sampleLoaded[p[0]] = false;
                sampleLength[p[0]] = 0;
            }
            break;

        // ── RESET ───────────────────────────────────────────────────────
        case CMD_RESET:
            for (int i = 0; i < MAX_VOICES; i++) voices[i].active = false;
            for (int i = 0; i < MAX_PADS; i++) { sampleLoaded[i] = false; sampleLength[i] = 0; }
            break;

        // ── DELAY ───────────────────────────────────────────────────────
        case CMD_DELAY_ACTIVE:
            if (hdr->length >= 1) delayActive = (p[0] != 0);
            break;
        case CMD_DELAY_TIME:
            if (hdr->length >= 2) {
                uint16_t ms = 0; memcpy(&ms, p, 2);
                delayTime = (float)ms;
                masterDelay.SetDelay(delayTime / 1000.0f * 44100.0f);
            }
            break;
        case CMD_DELAY_FEEDBACK:
            if (hdr->length >= 1) delayFeedback = p[0] / 100.0f;
            break;
        case CMD_DELAY_MIX:
            if (hdr->length >= 1) delayMix = p[0] / 100.0f;
            break;

        // ── COMPRESSOR ──────────────────────────────────────────────────
        case CMD_COMP_ACTIVE:
            if (hdr->length >= 1) compActive = (p[0] != 0);
            break;
        case CMD_COMP_THRESHOLD:
            if (hdr->length >= 1) masterComp.SetThreshold(-((float)p[0]));
            break;
        case CMD_COMP_RATIO:
            if (hdr->length >= 1) masterComp.SetRatio((float)p[0]);
            break;
        case CMD_COMP_ATTACK:
            if (hdr->length >= 1) masterComp.SetAttack((float)p[0] / 1000.0f);
            break;
        case CMD_COMP_RELEASE:
            if (hdr->length >= 1) masterComp.SetRelease((float)p[0] / 1000.0f);
            break;
        case CMD_COMP_MAKEUP:
            if (hdr->length >= 1) masterComp.SetMakeup((float)p[0] / 10.0f);
            break;

        // ── REVERB ──────────────────────────────────────────────────────
        case CMD_REVERB_ACTIVE:
            if (hdr->length >= 1) reverbActive = (p[0] != 0);
            break;
        case CMD_REVERB_FEEDBACK:
            if (hdr->length >= 1) { reverbFeedback = p[0] / 100.0f; masterReverb.SetFeedback(reverbFeedback); }
            break;
        case CMD_REVERB_LPFREQ:
            if (hdr->length >= 2) { uint16_t f = 0; memcpy(&f, p, 2); reverbLpFreq = (float)f; masterReverb.SetLpFreq(reverbLpFreq); }
            break;
        case CMD_REVERB_MIX:
            if (hdr->length >= 1) reverbMix = p[0] / 100.0f;
            break;

        // ── CHORUS ──────────────────────────────────────────────────────
        case CMD_CHORUS_ACTIVE:
            if (hdr->length >= 1) chorusActive = (p[0] != 0);
            break;
        case CMD_CHORUS_RATE:
            if (hdr->length >= 1) masterChorus.SetLfoFreq(p[0] / 10.0f);
            break;
        case CMD_CHORUS_DEPTH:
            if (hdr->length >= 1) masterChorus.SetLfoDepth(p[0] / 100.0f);
            break;
        case CMD_CHORUS_MIX:
            if (hdr->length >= 1) chorusMix = p[0] / 100.0f;
            break;

        // ── TREMOLO ─────────────────────────────────────────────────────
        case CMD_TREMOLO_ACTIVE:
            if (hdr->length >= 1) tremoloActive = (p[0] != 0);
            break;
        case CMD_TREMOLO_RATE:
            if (hdr->length >= 1) masterTremolo.SetFreq(p[0] / 10.0f);
            break;
        case CMD_TREMOLO_DEPTH:
            if (hdr->length >= 1) masterTremolo.SetDepth(p[0] / 100.0f);
            break;

        // ── WAVEFOLDER + LIMITER ────────────────────────────────────────
        case CMD_WAVEFOLDER_GAIN:
            if (hdr->length >= 1) waveFolderGain = p[0] / 10.0f;
            break;
        case CMD_LIMITER_ACTIVE:
            if (hdr->length >= 1) limiterActive = (p[0] != 0);
            break;

        // ── PAD LOOP / REVERSE / PITCH ──────────────────────────────────
        case CMD_PAD_LOOP:
            if (hdr->length >= 2 && p[0] < MAX_PADS) padLoop[p[0]] = (p[1] != 0);
            break;
        case CMD_PAD_REVERSE:
            if (hdr->length >= 2 && p[0] < MAX_PADS) padReverse[p[0]] = (p[1] != 0);
            break;
        case CMD_PAD_PITCH:
            if (hdr->length >= 3 && p[0] < MAX_PADS) {
                int16_t cents = 0; memcpy(&cents, p + 1, 2);
                padPitch[p[0]] = powf(2.0f, cents / 1200.0f);
            }
            break;

        // ── STATUS QUERIES ──────────────────────────────────────────────
        case CMD_GET_STATUS: {
            uint8_t resp[20]; memset(resp, 0, sizeof(resp));
            uint8_t cnt = 0;
            for (int v = 0; v < MAX_VOICES; v++) if (voices[v].active) cnt++;
            resp[0] = cnt;
            resp[1] = (uint8_t)(seed.system.GetCpuLoad() * 100.0f);
            BuildResponse(CMD_GET_STATUS, hdr->sequence, resp, 20);
            return;
        }
        case CMD_GET_CPU_LOAD: {
            float load = seed.system.GetCpuLoad();
            uint8_t pct = (uint8_t)(load * 100.0f);
            BuildResponse(CMD_GET_CPU_LOAD, hdr->sequence, &pct, 1);
            return;
        }
        case CMD_GET_VOICES: {
            uint8_t cnt = 0;
            for (int v = 0; v < MAX_VOICES; v++) if (voices[v].active) cnt++;
            BuildResponse(CMD_GET_VOICES, hdr->sequence, &cnt, 1);
            return;
        }

        // ── BULK TRIGGERS ───────────────────────────────────────────────
        case CMD_BULK_TRIGGERS:
            if (hdr->length >= 2) {
                uint8_t count = p[0];
                for (uint8_t i = 0; i < count && (1 + i * 2 + 1) < hdr->length; i++)
                    TriggerPad(p[1 + i * 2], p[1 + i * 2 + 1]);
            }
            break;

        default: break;
    }
}

// ─── SPI DMA Callback ────────────────────────────────────────────────────────
static void SpiRxCallback(void* context, SpiHandle::Result result) {
    if (result != SpiHandle::Result::OK) {
        spi_slave.DmaReceive(rxBuf, 8, nullptr, SpiRxCallback, nullptr);
        return;
    }
    SPIPacketHeader* hdr = (SPIPacketHeader*)rxBuf;

    if (!waitingPayload) {
        if (hdr->magic != SPI_MAGIC_CMD) {
            spi_slave.DmaReceive(rxBuf, 8, nullptr, SpiRxCallback, nullptr);
            return;
        }
        if (hdr->length > 0 && hdr->length <= (RX_BUF_SIZE - 8)) {
            waitingPayload = true;
            spi_slave.DmaReceive(rxBuf + 8, hdr->length, nullptr, SpiRxCallback, nullptr);
            return;
        }
    }
    waitingPayload = false;
    ProcessCommand();
    if (!pendingResponse)
        spi_slave.DmaReceive(rxBuf, 8, nullptr, SpiRxCallback, nullptr);
}

// ─── main ─────────────────────────────────────────────────────────────────────
int main() {
    seed.Init();
    seed.SetAudioBlockSize(128);
    seed.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_44KHZ);

    // Init arrays
    for (int i = 0; i < MAX_PADS; i++) {
        sampleLoaded[i] = false; sampleLength[i] = 0;
        sampleTotalSamples[i] = 0;
        trackGain[i]  = 1.0f; trackPeak[i] = 0.0f;
        padLoop[i]    = false;
        padReverse[i] = false;
        padPitch[i]   = 1.0f;
    }
    for (int i = 0; i < MAX_VOICES; i++) voices[i].active = false;

    // Init FX
    float sr = 44100.0f;
    masterDelay.Init();
    masterDelay.SetDelay(sr * 0.25f);

    masterReverb.Init(sr);
    masterReverb.SetFeedback(0.6f);
    masterReverb.SetLpFreq(8000.0f);

    masterChorus.Init(sr);
    masterChorus.SetLfoFreq(0.3f);
    masterChorus.SetLfoDepth(0.4f);
    masterChorus.SetDelay(0.75f);

    masterTremolo.Init(sr);
    masterTremolo.SetFreq(4.0f);
    masterTremolo.SetDepth(0.5f);
    masterTremolo.SetWaveform(Oscillator::WAVE_SIN);

    masterComp.Init(sr);
    masterComp.SetThreshold(-20.0f);
    masterComp.SetRatio(4.0f);
    masterComp.SetAttack(0.01f);
    masterComp.SetRelease(0.1f);
    masterComp.SetMakeup(1.0f);
    masterComp.AutoMakeup(true);

    masterFold.Init();
    masterFold.SetIncrement(1.0f);

    // SPI Slave
    SpiHandle::Config spi_config;
    spi_config.periph         = SpiHandle::Config::Peripheral::SPI_3;
    spi_config.mode           = SpiHandle::Config::Mode::SLAVE;
    spi_config.direction      = SpiHandle::Config::Direction::TWO_LINES;
    spi_config.datasize       = 8;
    spi_config.clock_polarity = SpiHandle::Config::ClockPolarity::LOW;
    spi_config.clock_phase    = SpiHandle::Config::ClockPhase::ONE_EDGE;
    spi_config.nss            = SpiHandle::Config::NSS::HARD_INPUT;
    spi_config.baud_prescaler = SpiHandle::Config::BaudPrescaler::PS_128;
    spi_config.pin_config.sclk = seed.GetPin(10);
    spi_config.pin_config.miso = seed.GetPin(8);
    spi_config.pin_config.mosi = seed.GetPin(9);
    spi_config.pin_config.nss  = seed.GetPin(7);
    spi_slave.Init(spi_config);

    spi_slave.DmaReceive(rxBuf, 8, nullptr, SpiRxCallback, nullptr);

    // Start Audio
    seed.StartAudio(AudioCallback);

    // Main loop
    while (1) {
        if (pendingResponse) {
            pendingResponse = false;
            spi_slave.DmaTransmit(txBuf, pendingTxLen, nullptr, nullptr, nullptr);
            System::Delay(1);
            spi_slave.DmaReceive(rxBuf, 8, nullptr, SpiRxCallback, nullptr);
        }
    }
}
```

---

## 10. SD Card — Carga de kits desde la Daisy

### 10.1 Estructura de carpetas en la SD

```
/RED808/
  ├── 808 Classic/
  │   ├── BD.wav
  │   ├── SD.wav
  │   ├── CH.wav
  │   └── ...
  ├── 808 Karz/
  │   ├── BD.wav
  │   └── ...
  └── My Custom Kit/
      └── ...
```

### 10.2 Flujo de operación

```
ESP32 (master)                    Daisy (slave con SD)
  │                                   │
  ├─ CMD_SD_KIT_LIST ────────────────►│  Escanear /RED808/
  │◄─ SdKitListResponse ─────────────┤  ["808 Classic", "808 Karz", ...]
  │                                   │
  │  (Usuario selecciona kit en web)  │
  │                                   │
  ├─ CMD_SD_LOAD_KIT ────────────────►│  Cargar WAVs → SDRAM
  │  kitName="808 Classic"            │  (SD→SDRAM directo, ~2 MB/s)
  │                                   │
  ├─ CMD_SD_STATUS ──────────────────►│  ¿Cargado?
  │◄─ SdStatusResponse ──────────────┤  loaded=0xFFFF, kit="808 Classic"
  │                                   │
  ├─ CMD_TRIGGER_LIVE ───────────────►│  ¡Suena!
```

### 10.3 Velocidad comparativa

| Método | 16 samples (~500KB) | 
|--------|---------------------|
| ESP32→SPI→Daisy @ 2MHz  | ~12 segundos |
| ESP32→SPI→Daisy @ 20MHz | ~1.5 segundos |
| **Daisy SD→SDRAM**      | **~0.25 segundos** |

### 10.4 Código de ejemplo — SD filesystem

```cpp
#include "fatfs.h"

SdmmcHandler   sd;
FatFSInterface fsi;
static bool    sdPresent = false;
static char    currentKitName[32] = "";

static bool InitSD() {
    SdmmcHandler::Config sd_config;
    sd_config.Defaults();
    sd_config.speed = SdmmcHandler::Speed::FAST;
    sd.Init(sd_config);
    fsi.Init(FatFSInterface::Config::MEDIA_SD);
    FRESULT fr = f_mount(&fsi.GetSDFileSystem(), "/", 1);
    sdPresent = (fr == FR_OK);
    return sdPresent;
}

// Añadir al switch de ProcessCommand():

case CMD_SD_KIT_LIST: {
    SdKitListResponse resp = {};
    DIR dir; FILINFO fno;
    if (f_opendir(&dir, "/RED808") == FR_OK) {
        while (f_readdir(&dir, &fno) == FR_OK && fno.fname[0] != 0) {
            if ((fno.fattrib & AM_DIR) && resp.count < 16) {
                strncpy(resp.kits[resp.count], fno.fname, 31);
                resp.count++;
            }
        }
        f_closedir(&dir);
    }
    BuildResponse(CMD_SD_KIT_LIST, hdr->sequence, (uint8_t*)&resp, sizeof(resp));
    return;
}

case CMD_SD_LOAD_KIT: {
    if (hdr->length >= sizeof(SdLoadKitPayload)) {
        SdLoadKitPayload lk;
        memcpy(&lk, p, sizeof(lk));
        char path[64];
        snprintf(path, sizeof(path), "/RED808/%s", lk.kitName);
        DIR dir; FILINFO fno;
        uint8_t padIdx = lk.startPad;
        if (f_opendir(&dir, path) == FR_OK) {
            while (f_readdir(&dir, &fno) == FR_OK && fno.fname[0] != 0
                   && padIdx < lk.startPad + lk.maxPads) {
                char* ext = strrchr(fno.fname, '.');
                if (!ext || strcasecmp(ext, ".wav") != 0) continue;
                char fpath[96];
                snprintf(fpath, sizeof(fpath), "%s/%s", path, fno.fname);
                FIL fil;
                if (f_open(&fil, fpath, FA_READ) == FR_OK) {
                    f_lseek(&fil, 44);  // skip WAV header
                    UINT br;
                    uint32_t totalBytes = f_size(&fil) - 44;
                    uint32_t totalSamples = totalBytes / 2;
                    if (totalSamples > MAX_SAMPLE_BYTES / 2)
                        totalSamples = MAX_SAMPLE_BYTES / 2;
                    f_read(&fil, sampleStorage[padIdx], totalSamples * 2, &br);
                    f_close(&fil);
                    sampleLength[padIdx] = totalSamples;
                    sampleLoaded[padIdx] = true;
                    padIdx++;
                }
            }
            f_closedir(&dir);
            strncpy(currentKitName, lk.kitName, 31);
        }
    }
    break;
}

case CMD_SD_STATUS: {
    SdStatusResponse resp = {};
    resp.present = sdPresent ? 1 : 0;
    for (int i = 0; i < MAX_PADS; i++)
        if (sampleLoaded[i]) resp.samplesLoaded |= (1 << i);
    strncpy(resp.currentKit, currentKitName, 31);
    BuildResponse(CMD_SD_STATUS, hdr->sequence, (uint8_t*)&resp, sizeof(resp));
    return;
}
```

---

## 11. Per-Track FX Sends + Mixer

### 11.1 Diagrama de señal

```
Track N audio → [EQ 3-band] → [Track Filter] → [Gate] → [Track Comp]
       │                                                       │
       │            ┌─ [Pan L/R] → Master Mix L/R             │
       │            │                                          │
       └── gain ────┤─ [Reverb Send] → Master Reverb Bus      │
                    │─ [Delay Send]  → Master Delay Bus        │
                    └─ [Chorus Send] → Master Chorus Bus       │
                          │                                    │
                          ▼                                    │
                    Master FX Chain → Out ◄────────────────────┘
```

### 11.2 Variables globales

```cpp
static float trackReverbSend[MAX_PADS] = {0};   // 0.0-1.0
static float trackDelaySend[MAX_PADS]  = {0};   // 0.0-1.0
static float trackChorusSend[MAX_PADS] = {0};   // 0.0-1.0
static float trackPanF[MAX_PADS];               // -1.0..+1.0 (0.0 = center)
static bool  trackMute[MAX_PADS]  = {false};
static bool  trackSolo[MAX_PADS]  = {false};
```

### 11.3 En AudioCallback (después del voice mixing, antes de Master FX)

```cpp
float reverbBusL = 0.0f, delayBusL = 0.0f, chorusBusL = 0.0f;

// En el bucle de voces, después de calcular fout:
reverbBusL += fout * trackReverbSend[vx.pad];
delayBusL  += fout * trackDelaySend[vx.pad];
chorusBusL += fout * trackChorusSend[vx.pad];

// Pan:
float panL = (1.0f - trackPanF[vx.pad]) * 0.5f;
float panR = (1.0f + trackPanF[vx.pad]) * 0.5f;
out[0][i] += fout * panL;
out[1][i] += fout * panR;
```

### 11.4 Command handlers

```cpp
case CMD_TRACK_REVERB_SEND:
    if (hdr->length >= 2 && p[0] < MAX_PADS) trackReverbSend[p[0]] = p[1] / 100.0f;
    break;
case CMD_TRACK_DELAY_SEND:
    if (hdr->length >= 2 && p[0] < MAX_PADS) trackDelaySend[p[0]] = p[1] / 100.0f;
    break;
case CMD_TRACK_CHORUS_SEND:
    if (hdr->length >= 2 && p[0] < MAX_PADS) trackChorusSend[p[0]] = p[1] / 100.0f;
    break;
case CMD_TRACK_PAN:
    if (hdr->length >= 2 && p[0] < MAX_PADS) trackPanF[p[0]] = (int8_t)p[1] / 100.0f;
    break;
case CMD_TRACK_MUTE:
    if (hdr->length >= 2 && p[0] < MAX_PADS) trackMute[p[0]] = (p[1] != 0);
    break;
case CMD_TRACK_SOLO:
    if (hdr->length >= 2 && p[0] < MAX_PADS) trackSolo[p[0]] = (p[1] != 0);
    break;
case CMD_TRACK_EQ_LOW:
    if (hdr->length >= 2 && p[0] < MAX_PADS) { /* Biquad lowshelf: (int8_t)p[1] dB */ }
    break;
case CMD_TRACK_EQ_MID:
    if (hdr->length >= 2 && p[0] < MAX_PADS) { /* Biquad peaking: (int8_t)p[1] dB */ }
    break;
case CMD_TRACK_EQ_HIGH:
    if (hdr->length >= 2 && p[0] < MAX_PADS) { /* Biquad highshelf: (int8_t)p[1] dB */ }
    break;
```

---

## 12. Mapa DaisySP — Qué módulo usar para cada CMD

| Grupo de comandos | Módulo DaisySP | Notas |
|-------------------|----------------|-------|
| Delay (0x30-0x33) | `DelayLine<float, 88200>` | En SDRAM. `SetDelay()`, `Read()`, `Write()` |
| Phaser (0x34-0x37) | `Phaser` | `SetLfoFreq()`, `SetLfoDepth()`, `SetFeedback()` |
| Flanger (0x38-0x3C) | `DelayLine` + `Oscillator` | No hay clase Flanger, construir manualmente |
| Compressor (0x3D-0x42) | `Compressor` | `SetThreshold()`, `SetRatio()`, `SetAttack()`, `SetRelease()` |
| Reverb (0x43-0x46) | `ReverbSc` | Estéreo. `SetFeedback()`, `SetLpFreq()` |
| Chorus (0x47-0x4A) | `Chorus` | `SetLfoFreq()`, `SetLfoDepth()`, `SetDelay()` |
| Tremolo (0x4B-0x4D) | `Tremolo` | `SetFreq()`, `SetDepth()`, `SetWaveform()` |
| WaveFolder (0x4E) | `Fold` | `SetIncrement()` = gain, `Process()` |
| Limiter (0x4F) | `fclamp()` / `tanhf()` | Soft clip o brick-wall |
| Global Filter (0x20-0x26) | `Biquad` + `Overdrive` + `Decimator` | 3 módulos combinados |
| Per-Track Filter (0x50) | `Biquad trackFilter[16]` | 16 instancias |
| Per-Track Echo (0x54) | `DelayLine<float, 8820> trackDelay[16]` | 16 delays cortos en SDRAM |
| Per-Track Compressor (0x56) | `Compressor trackComp[16]` | 16 instancias |
| Per-Track EQ (0x63-0x65) | `Biquad` × 3 × 16 | Low shelf + peaking + high shelf |
| Pad Filter (0x70) | `Biquad padFilter[24]` | 24 instancias |
| Pad Distortion (0x72) | `Overdrive padDrive[24]` | 24 instancias |
| Pad Bitcrush (0x73) | `Decimator padDecim[24]` | `SetBitcrushFactor()` |
| Sidechain (0x90) | `Adsr` | Envelope follower + ducking |

> **Recursos en Daisy:** 16 Biquad + 16 Decimator + 16 Compressor + 48 EQ Biquad + 16 DelayLine(8820) ≈ **~600 KB SDRAM** para delays. La Daisy tiene 64MB → sin problema.

---

## 13. Prioridades de implementación

| Prioridad | Grupo | CMDs | Impacto |
|-----------|-------|------|---------|
| **1 — CRÍTICO** | Triggers + Volume + Samples + Ping + Reset | 0x01-0x04, 0x10, 0x13, 0xA0-0xA2, 0xEE, 0xEF | ✅ Ya en red808_daisy.cpp referencia |
| **2 — ALTO** | Reverb + Delay + Compressor | 0x30-0x33, 0x3D-0x42, 0x43-0x46 | Master FX esenciales |
| **3 — ALTO** | Loop + Reverse + Pitch | 0x74-0x76 | ✅ Ya en referencia |
| **4 — ALTO** | SD Card kits | 0xB0-0xB9 | Carga rápida de kits |
| **5 — MEDIO** | Chorus + Tremolo + WaveFolder + Limiter | 0x47-0x4F | Master FX extendidos |
| **6 — MEDIO** | Global Filter | 0x20-0x26 | Biquad + Overdrive + Bitcrush |
| **7 — MEDIO** | Per-Track Sends + Pan + Mute/Solo + EQ | 0x59-0x65 | Mixer avanzado |
| **8 — MEDIO** | Per-Track FX | 0x50-0x58 | Filter, echo, compressor por track |
| **9 — BAJO** | Sidechain | 0x90-0x91 | Ducking |
| **10 — BAJO** | Pad FX avanzados | 0x77-0x79 | Stutter, scratch, turntablism |
| **11 — BAJO** | Status queries + Peaks + Bulk | 0xE0-0xE3, 0xF0-0xF1 | Monitorización |

---

## 14. Criterio de éxito

### Fase 1 — PING OK

Monitor serial del ESP32 muestra:
```
[SPI RX] #000 PING      OK len=8
[SPI] STM32 connected! RTT: 300 us
```

### Fase 2 — Samples cargados

```
[SPI] Sample 1 transfer complete: 68 chunks, 34560 bytes
[SPI] Sample 2 transfer complete: 32 chunks, 16128 bytes
...
✓ Samples loaded: 16/16
```

### Fase 3 — Audio funciona

Al golpear pad en la web:
```
[PAD] trigger pad=3 vel=127
[SPI TX] TRIG_LIVE cmd=0x02 len=2  data: 03 7F
```
→ La Daisy reproduce el sample del pad 3 con calidad perfecta a 44100 Hz estéreo.

### Fase 4 — FX maestros funcionan

- Delay: eco audible al activar 0x30
- Reverb: cola de reverb al activar 0x43
- Compressor: pumping audible al activar 0x3D

### Fase 5 — SD Card

```
[SD] Kit list: 808 Classic, 808 Karz, Lo-Fi Kit
[SD] Loaded "808 Classic" → 16 pads, 0.25s
```

---

## 15. protocol.h — Referencia completa de constantes

> Este es el archivo real del firmware ESP32. Todos los `#define` y `typedef` son la fuente de verdad.

```c
// ═══════════════════════════════════════════════════════
// MAGIC BYTES
// ═══════════════════════════════════════════════════════
#define SPI_MAGIC_CMD      0xA5   // Master → Slave
#define SPI_MAGIC_RESP     0x5A   // Slave → Master

// ═══════════════════════════════════════════════════════
// ALL COMMAND CODES
// ═══════════════════════════════════════════════════════

// Triggers (0x01-0x05)
#define CMD_TRIGGER_SEQ       0x01
#define CMD_TRIGGER_LIVE      0x02
#define CMD_TRIGGER_STOP      0x03
#define CMD_TRIGGER_STOP_ALL  0x04
#define CMD_TRIGGER_SIDECHAIN 0x05

// Volume (0x10-0x14)
#define CMD_MASTER_VOLUME     0x10
#define CMD_SEQ_VOLUME        0x11
#define CMD_LIVE_VOLUME       0x12
#define CMD_TRACK_VOLUME      0x13
#define CMD_LIVE_PITCH        0x14

// Global Filter (0x20-0x26)
#define CMD_FILTER_SET        0x20
#define CMD_FILTER_CUTOFF     0x21
#define CMD_FILTER_RESONANCE  0x22
#define CMD_FILTER_BITDEPTH   0x23
#define CMD_FILTER_DISTORTION 0x24
#define CMD_FILTER_DIST_MODE  0x25
#define CMD_FILTER_SR_REDUCE  0x26

// Master FX (0x30-0x4F)
#define CMD_DELAY_ACTIVE      0x30
#define CMD_DELAY_TIME        0x31
#define CMD_DELAY_FEEDBACK    0x32
#define CMD_DELAY_MIX         0x33
#define CMD_PHASER_ACTIVE     0x34
#define CMD_PHASER_RATE       0x35
#define CMD_PHASER_DEPTH      0x36
#define CMD_PHASER_FEEDBACK   0x37
#define CMD_FLANGER_ACTIVE    0x38
#define CMD_FLANGER_RATE      0x39
#define CMD_FLANGER_DEPTH     0x3A
#define CMD_FLANGER_FEEDBACK  0x3B
#define CMD_FLANGER_MIX       0x3C
#define CMD_COMP_ACTIVE       0x3D
#define CMD_COMP_THRESHOLD    0x3E
#define CMD_COMP_RATIO        0x3F
#define CMD_COMP_ATTACK       0x40
#define CMD_COMP_RELEASE      0x41
#define CMD_COMP_MAKEUP       0x42
#define CMD_REVERB_ACTIVE     0x43
#define CMD_REVERB_FEEDBACK   0x44
#define CMD_REVERB_LPFREQ     0x45
#define CMD_REVERB_MIX        0x46
#define CMD_CHORUS_ACTIVE     0x47
#define CMD_CHORUS_RATE       0x48
#define CMD_CHORUS_DEPTH      0x49
#define CMD_CHORUS_MIX        0x4A
#define CMD_TREMOLO_ACTIVE    0x4B
#define CMD_TREMOLO_RATE      0x4C
#define CMD_TREMOLO_DEPTH     0x4D
#define CMD_WAVEFOLDER_GAIN   0x4E
#define CMD_LIMITER_ACTIVE    0x4F

// Per-Track FX (0x50-0x65)
#define CMD_TRACK_FILTER      0x50
#define CMD_TRACK_CLEAR_FILTER 0x51
#define CMD_TRACK_DISTORTION  0x52
#define CMD_TRACK_BITCRUSH    0x53
#define CMD_TRACK_ECHO        0x54
#define CMD_TRACK_FLANGER_FX  0x55
#define CMD_TRACK_COMPRESSOR  0x56
#define CMD_TRACK_CLEAR_LIVE  0x57
#define CMD_TRACK_CLEAR_FX    0x58
#define CMD_TRACK_REVERB_SEND 0x59
#define CMD_TRACK_DELAY_SEND  0x5A
#define CMD_TRACK_CHORUS_SEND 0x5B
#define CMD_TRACK_PAN         0x5C
#define CMD_TRACK_MUTE        0x5D
#define CMD_TRACK_SOLO        0x5E
#define CMD_TRACK_PHASER      0x5F
#define CMD_TRACK_TREMOLO     0x60
#define CMD_TRACK_PITCH       0x61
#define CMD_TRACK_GATE        0x62
#define CMD_TRACK_EQ_LOW      0x63
#define CMD_TRACK_EQ_MID      0x64
#define CMD_TRACK_EQ_HIGH     0x65

// Per-Pad FX (0x70-0x7A)
#define CMD_PAD_FILTER        0x70
#define CMD_PAD_CLEAR_FILTER  0x71
#define CMD_PAD_DISTORTION    0x72
#define CMD_PAD_BITCRUSH      0x73
#define CMD_PAD_LOOP          0x74
#define CMD_PAD_REVERSE       0x75
#define CMD_PAD_PITCH         0x76
#define CMD_PAD_STUTTER       0x77
#define CMD_PAD_SCRATCH       0x78
#define CMD_PAD_TURNTABLISM   0x79
#define CMD_PAD_CLEAR_FX      0x7A

// Sidechain (0x90-0x91)
#define CMD_SIDECHAIN_SET     0x90
#define CMD_SIDECHAIN_CLEAR   0x91

// Sample Transfer (0xA0-0xA4)
#define CMD_SAMPLE_BEGIN      0xA0
#define CMD_SAMPLE_DATA       0xA1
#define CMD_SAMPLE_END        0xA2
#define CMD_SAMPLE_UNLOAD     0xA3
#define CMD_SAMPLE_UNLOAD_ALL 0xA4

// SD Card (0xB0-0xB9)
#define CMD_SD_LIST_FOLDERS   0xB0
#define CMD_SD_LIST_FILES     0xB1
#define CMD_SD_FILE_INFO      0xB2
#define CMD_SD_LOAD_SAMPLE    0xB3
#define CMD_SD_LOAD_KIT       0xB4
#define CMD_SD_KIT_LIST       0xB5
#define CMD_SD_STATUS         0xB6
#define CMD_SD_UNLOAD_KIT     0xB7
#define CMD_SD_GET_LOADED     0xB8
#define CMD_SD_ABORT          0xB9

// Status / Query (0xE0-0xEF)
#define CMD_GET_STATUS        0xE0
#define CMD_GET_PEAKS         0xE1
#define CMD_GET_CPU_LOAD      0xE2
#define CMD_GET_VOICES        0xE3
#define CMD_PING              0xEE
#define CMD_RESET             0xEF

// Bulk (0xF0-0xF1)
#define CMD_BULK_TRIGGERS     0xF0
#define CMD_BULK_FX           0xF1

// Filter types
#define FTYPE_NONE         0
#define FTYPE_LOWPASS      1
#define FTYPE_HIGHPASS     2
#define FTYPE_BANDPASS     3
#define FTYPE_NOTCH        4
#define FTYPE_ALLPASS      5
#define FTYPE_PEAKING      6
#define FTYPE_LOWSHELF     7
#define FTYPE_HIGHSHELF    8

// Distortion modes
#define DMODE_SOFT   0
#define DMODE_HARD   1
#define DMODE_TUBE   2
#define DMODE_FUZZ   3
```

---

## NOTAS IMPORTANTES

1. **NSS debe ser Hardware NSS** — sin esto el flanco rising no dispara el callback DMA
2. **NUNCA transmitir SPI desde ISR/callback** — usar flag + main loop (ver el patrón `pendingResponse`)
3. **Los samples son 44100 Hz mono 16-bit signed** — configurar la Daisy a 44100 Hz
4. **padIndex en triggers = el mismo padIndex del SMPL_BEG** — no secuencial
5. **No todos los pads tienen sample** — si llega un trigger para un pad vacío → ignorar
6. **TX_BUF_SIZE = 76 es suficiente** para la mayoría de respuestas, pero las SD responses (0xB0, 0xB1, 0xB5) son más grandes → ampliar a 768 o fragmentar
7. **La transferencia SPI de samples es el método legacy** — la carga desde SD card (0xB4) es **~50x más rápida**

---

> **Contacto:** Si algo del protocolo no queda claro, preguntad. El firmware ESP32 está compilado y funcionando — lo que envía es exactamente lo que describe este documento.
