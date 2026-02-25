# RED808 — STM32 SPI Slave: Guía completa de implementación
> Actualizado 25/02/2026 — sincronizado con cambios del equipo slave Daisy

---

## 1. Estado actual del sistema

### Lo que ya funciona en el ESP32 (master):
- 16 samples cargados correctamente desde Flash → PSRAM (7.5 MB libres)
- Todos los samples transferidos al slave via SPI (SMPL_BEG / SMPL_DATA / SMPL_END)
- WebSocket conectado — los pads envían triggers correctamente
- `TRIG_LIVE` cmd=0x02 enviado por SPI cada vez que se golpea un pad

### Log real del monitor (extracto):
```
[SPI] Sample 13 transfer complete: 45 chunks, 23040 bytes
[SPI] Sample 1 transfer complete: 68 chunks, 34560 bytes
✓ Samples loaded: 16/16

[WS BIN] len=3 data: 90 03 7F
[PAD] trigger pad=3 vel=127
[SPI TX] #1765 TRIG_LIVE cmd=0x02 len=2 crc=0xA040
         data: 03 7F
```

**Todo el lado ESP32 funciona.** El STM32 recibe los bytes pero aún no responde ni reproduce audio.

---

## 2. Conexiones físicas

| Señal        | STM32 pin | ESP32-S3 pin | Notas                        |
|--------------|-----------|--------------|------------------------------|
| NSS / CS     | PA4       | GPIO 10      | **Hardware NSS obligatorio** |
| SCK          | PA5       | GPIO 12      |                              |
| MOSI (RX)    | PA7       | GPIO 11      | STM32 recibe aquí            |
| MISO (TX)    | PA6       | GPIO 13      | STM32 transmite aquí         |
| GND          | GND       | GND          |                              |

- **SPI Mode 0** (CPOL=0, CPHA=0), MSB first
- **Clock actual: 2 MHz** (se subirá a 20 MHz una vez estable)
- NSS en modo **Hardware NSS** — el flanco rising dispara la ISR del slave

---

## 3. Protocolo de transacción — DOS CS SEPARADAS (CRÍTICO)

El ESP32 ya fue modificado para hacer **dos transacciones CS separadas** en todos los comandos que requieren respuesta:

```
Txn 1:  CS LOW ──[ master envía 8 bytes header + N bytes payload ]── CS HIGH
                                                                         ↑
                                          flanco NSS rising → ISR del STM32
                                          El STM32 tiene aquí 500 µs para:
                                            1. parsear el header recibido
                                            2. preparar txBuf con la respuesta
                                            3. armar HAL_SPI_Transmit_IT/DMA
        [ 500 µs pausa en el ESP32 ]

Txn 2:  CS LOW ──[ master clokea 8 bytes header + M bytes respuesta ]── CS HIGH
                         ↑
                         STM32 ya tiene txBuf cargado → MISO sale 0x5A 0xEE ...
```

Para comandos **sin respuesta** (triggers, volumen, FX, sample transfer):
- Solo existe Txn 1
- El STM32 solo recibe, no necesita transmitir nada

---

## 4. Estructura del header (8 bytes, packed, little-endian)

```c
typedef struct __attribute__((packed)) {
    uint8_t  magic;      // Master→Slave: 0xA5  |  Slave→Master: 0x5A
    uint8_t  cmd;        // Código de comando
    uint16_t length;     // Bytes de payload que siguen al header
    uint16_t sequence;   // Número de secuencia (el slave debe ecoarlo en la respuesta)
    uint16_t checksum;   // CRC16-Modbus del payload (0 si length==0)
} SPIPacketHeader;       // Total: 8 bytes
```

---

## 5. Comandos y payloads reales (verificados con logs)

### 5.1 TRIG_LIVE — cmd=0x02 (sin respuesta)

Log real:
```
[SPI TX] TRIG_LIVE  cmd=0x02  len=2  crc=0xA040
         data: 03 7F
```

| Byte | Campo    | Ejemplo | Descripción  |
|------|----------|---------|--------------|
| 0    | padIndex | 0x03    | Pad 3        |
| 1    | velocity | 0x7F    | Velocity 127 |

```c
typedef struct __attribute__((packed)) {
    uint8_t padIndex;   // 0-23
    uint8_t velocity;   // 1-127
} TriggerLivePayload;
```

---

### 5.2 TRIG_SEQ — cmd=0x01 (sin respuesta)

```c
typedef struct __attribute__((packed)) {
    uint8_t  padIndex;
    uint8_t  velocity;
    uint8_t  trackVolume;  // 0-150
    uint8_t  reserved;     // 0x00
    uint32_t maxSamples;   // 0=sample completo, >0=cortar a N muestras
} TriggerSeqPayload;       // 8 bytes
```

---

### 5.3 SMPL_BEG — cmd=0xA0 (sin respuesta)

Log real (pad 13, HC sample):
```
[SPI TX] SMPL_BEG  cmd=0xA0  len=12  crc=0x1B59
         data: 0D 10 44 AC 00 5A 00 00 00 2D 00 00
```

Decodificado byte a byte:

| Bytes  | Campo         | Hex        | Decimal  |
|--------|---------------|------------|----------|
| [0]    | padIndex      | 0x0D       | 13       |
| [1]    | bitsPerSample | 0x10       | 16       |
| [2-3]  | sampleRate    | 0xAC44     | 44100 Hz |
| [4-7]  | totalBytes    | 0x00005A00 | 23040 B  |
| [8-11] | totalSamples  | 0x00002D00 | 11520    |

```c
typedef struct __attribute__((packed)) {
    uint8_t  padIndex;
    uint8_t  bitsPerSample;  // siempre 16
    uint16_t sampleRate;     // siempre 44100
    uint32_t totalBytes;
    uint32_t totalSamples;
} SampleBeginPayload;        // 12 bytes
```

---

### 5.4 SMPL_DATA — cmd=0xA1 (sin respuesta, muchos chunks)

```c
typedef struct __attribute__((packed)) {
    uint8_t  padIndex;
    uint8_t  reserved;    // 0x00
    uint16_t chunkSize;   // bytes de PCM en este chunk (max 512)
    uint32_t offset;      // offset en bytes desde el inicio
    // int16_t PCM data[] sigue aquí (hasta 512 bytes = 256 muestras)
} SampleDataHeader;       // 8 bytes de cabecera + hasta 512 bytes de audio
```

Número real de chunks por sample (de los logs):

| Sample          | Bytes  | Chunks |
|-----------------|--------|--------|
| 808 RS.wav      | 4608   | 9      |
| 808 OH / MA.wav | 13824  | 27     |
| 808 HC 1.wav    | 23040  | 45     |
| 808 SD 1-5.wav  | 34560  | 68     |
| 808 HT 3.wav    | 36864  | 72     |
| 808 MC 3.wav    | 25344  | 50     |
| LC00.WAV        | 44102  | 87     |
| 808 CY 3-1.wav  | 78336  | 153    |
| LT10 / MT00.WAV | 88202  | 173    |

---

### 5.5 SMPL_END — cmd=0xA2 (sin respuesta)

Log real (pad 13):
```
[SPI TX] SMPL_END  cmd=0xA2  len=8  crc=0xA8F4
         data: 0D 00 00 00 BB 61 00 00
```

| Bytes | Campo    | Hex        | Descripción           |
|-------|----------|------------|-----------------------|
| [0]   | padIndex | 0x0D       | Pad 13                |
| [1]   | status   | 0x00       | 0=OK                  |
| [2-3] | reserved | 0x0000     |                       |
| [4-7] | checksum | 0x000061BB | CRC16 de todos los datos PCM |

```c
typedef struct __attribute__((packed)) {
    uint8_t  padIndex;
    uint8_t  status;    // 0=OK
    uint16_t reserved;
    uint32_t checksum;  // CRC16 del total de datos (en campo uint32)
} SampleEndPayload;     // 8 bytes
```

---

### 5.6 PING — cmd=0xEE (CON respuesta: Txn 1 + Txn 2)

```
Master envía   (Txn 1): header(8) + PingPayload(4)  = 12 bytes total
Slave responde (Txn 2): header(8) + PongResponse(8) = 16 bytes total
```

Header de respuesta que debe armar el slave:
```
magic=0x5A, cmd=0xEE, length=8, sequence=<eco del recibido>, checksum=crc16(pongData,8)
```

```c
typedef struct __attribute__((packed)) {
    uint32_t timestamp;      // micros() del ESP32
} PingPayload;

typedef struct __attribute__((packed)) {
    uint32_t echoTimestamp;  // mismo valor recibido
    uint32_t stm32Uptime;    // HAL_GetTick()
} PongResponse;
```

---

### 5.7 GET_PEAKS — cmd=0xE1 (CON respuesta)

```
Master envía   (Txn 1): header(8) + 0 bytes payload
Slave responde (Txn 2): header(8) + PeaksResponse(68) = 76 bytes
```

```c
typedef struct __attribute__((packed)) {
    float trackPeaks[16];  // 0.0-1.0 por pista (64 bytes)
    float masterPeak;      // 0.0-1.0 master    (4 bytes)
} PeaksResponse;           // 68 bytes total
```

---

### 5.8 Otros comandos sin respuesta

| CMD                 | Valor | Payload                              |
|---------------------|-------|--------------------------------------|
| CMD_TRIGGER_STOP    | 0x03  | `uint8_t padIndex`                  |
| CMD_TRIGGER_STOP_ALL| 0x04  | ninguno (length=0)                   |
| CMD_MASTER_VOLUME   | 0x10  | `uint8_t volume` (0-100)            |
| CMD_TRACK_VOLUME    | 0x13  | `uint8_t track, uint8_t volume`     |
| CMD_RESET           | 0xEF  | ninguno                              |

---

## 6. CRC16-Modbus

```c
uint16_t crc16(const uint8_t* data, uint16_t len) {
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

---

## 7. Mapa de samples ya transferidos al slave

El ESP32 transfiere los 16 samples automáticamente al arrancar. El STM32 los recibe via SMPL_BEG/DATA/END y debe almacenarlos indexados por `padIndex`:

| padIndex | Archivo WAV       | Samples | Bytes  | Formato          |
|----------|-------------------|---------|--------|------------------|
| 1        | 808 SD 1-5.wav    | 17280   | 34560  | PCM 16b 44100 Hz |
| 2        | 808 HH.wav (CH)   | 8064    | 16128  | PCM 16b 44100 Hz |
| 3        | 808 OH 1.wav      | 6912    | 13824  | PCM 16b 44100 Hz |
| 4        | 808 CY 3-1.wav    | 39168   | 78336  | PCM 16b 44100 Hz |
| 6        | 808 RS.wav        | 2304    | 4608   | PCM 16b 44100 Hz |
| 8        | LT10.WAV          | 44101   | 88202  | PCM 16b 44100 Hz |
| 9        | MT00.WAV          | 44101   | 88202  | PCM 16b 44100 Hz |
| 10       | 808 HT 3.wav      | 18432   | 36864  | PCM 16b 44100 Hz |
| 11       | 808 MA.wav        | 6912    | 13824  | PCM 16b 44100 Hz |
| 13       | 808 HC 1.wav      | 11520   | 23040  | PCM 16b 44100 Hz |
| 14       | 808 MC 3.wav      | 12672   | 25344  | PCM 16b 44100 Hz |
| 15       | LC00.WAV          | 22051   | 44102  | PCM 16b 44100 Hz |

Los samples PCM son **16-bit signed, mono, 44100 Hz** — listos para reproducir con DAC/I2S.

---

## 8. Implementación STM32 completa (HAL + DMA)

### red808_spi_slave.h

```c
#ifndef RED808_SPI_SLAVE_H
#define RED808_SPI_SLAVE_H

#include "main.h"
#include <stdint.h>

void SPI_Slave_Init(void);

/* ── Implementar en tu capa de audio ── */
void Audio_TriggerPad(uint8_t padIndex, uint8_t velocity);
void Audio_TriggerPadSeq(uint8_t padIndex, uint8_t velocity,
                          uint8_t trackVolume, uint32_t maxSamples);
void Audio_StopPad(uint8_t padIndex);
void Audio_StopAll(void);
void Audio_SetMasterVolume(uint8_t volume);
void Audio_SetTrackVolume(uint8_t track, uint8_t volume);
void Audio_GetPeaks(float* trackPeaks16, float* masterPeak);
void Audio_SampleBegin(uint8_t pad, uint32_t totalBytes, uint32_t totalSamples);
void Audio_SampleData(uint8_t pad, uint32_t offset,
                       const uint8_t* pcmData, uint16_t len);
void Audio_SampleEnd(uint8_t pad);
void Audio_Reset(void);

#endif
```

### red808_spi_slave.c

```c
#include "red808_spi_slave.h"
#include <string.h>

#define SPI_MAGIC_CMD        0xA5
#define SPI_MAGIC_RESP       0x5A

#define CMD_TRIGGER_SEQ      0x01
#define CMD_TRIGGER_LIVE     0x02
#define CMD_TRIGGER_STOP     0x03
#define CMD_TRIGGER_STOP_ALL 0x04
#define CMD_MASTER_VOLUME    0x10
#define CMD_TRACK_VOLUME     0x13
#define CMD_SAMPLE_BEGIN     0xA0
#define CMD_SAMPLE_DATA      0xA1
#define CMD_SAMPLE_END       0xA2
#define CMD_GET_PEAKS        0xE1
#define CMD_PING             0xEE
#define CMD_RESET            0xEF

#define RX_BUF_SIZE  536
#define TX_BUF_SIZE  76

typedef struct __attribute__((packed)) {
    uint8_t  magic;
    uint8_t  cmd;
    uint16_t length;
    uint16_t sequence;
    uint16_t checksum;
} SPIPacketHeader;

static uint8_t rxBuf[RX_BUF_SIZE];
static uint8_t txBuf[TX_BUF_SIZE];
static volatile uint8_t waitingPayload = 0;

extern SPI_HandleTypeDef hspi1;


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

static void buildResponse(uint8_t cmd, uint16_t seq,
                           const uint8_t* payload, uint16_t payloadLen) {
    SPIPacketHeader* resp = (SPIPacketHeader*)txBuf;
    resp->magic    = SPI_MAGIC_RESP;
    resp->cmd      = cmd;
    resp->length   = payloadLen;
    resp->sequence = seq;
    resp->checksum = (payloadLen > 0) ? crc16(payload, payloadLen) : 0;
    if (payloadLen > 0 && payload != NULL)
        memcpy(txBuf + sizeof(SPIPacketHeader), payload, payloadLen);
    /* Cargar TX: el master leerá ~500 µs después en Txn 2 */
    HAL_SPI_Transmit_IT(&hspi1, txBuf, sizeof(SPIPacketHeader) + payloadLen);
}

void SPI_Slave_Init(void) {
    memset(rxBuf, 0, sizeof(rxBuf));
    memset(txBuf, 0, sizeof(txBuf));
    waitingPayload = 0;
    HAL_SPI_Receive_DMA(&hspi1, rxBuf, 8);
}

void HAL_SPI_RxCpltCallback(SPI_HandleTypeDef *hspi) {
    if (hspi->Instance != SPI1) return;

    SPIPacketHeader* hdr = (SPIPacketHeader*)rxBuf;

    /* Paso 1: ¿recién recibimos el header y viene payload? */
    if (!waitingPayload) {
        if (hdr->magic != SPI_MAGIC_CMD) {
            HAL_SPI_Receive_DMA(&hspi1, rxBuf, 8);
            return;
        }
        if (hdr->length > 0 && hdr->length <= (RX_BUF_SIZE - 8)) {
            waitingPayload = 1;
            HAL_SPI_Receive_DMA(&hspi1, rxBuf + 8, hdr->length);
            return;
        }
    }

    /* Paso 2: procesar header + payload completo */
    waitingPayload = 0;
    uint8_t* p = rxBuf + sizeof(SPIPacketHeader);  /* puntero al payload */

    switch (hdr->cmd) {

        case CMD_PING: {
            uint32_t echoTs = 0;
            if (hdr->length >= 4) memcpy(&echoTs, p, 4);
            uint8_t pong[8];
            uint32_t uptime = HAL_GetTick();
            memcpy(pong,     &echoTs,  4);
            memcpy(pong + 4, &uptime,  4);
            buildResponse(CMD_PING, hdr->sequence, pong, 8);
            return;  /* RX se rearma en TxCpltCallback */
        }

        case CMD_GET_PEAKS: {
            float peaks[16] = {0.0f};
            float master = 0.0f;
            Audio_GetPeaks(peaks, &master);
            uint8_t buf[68];
            memcpy(buf,      peaks,   64);
            memcpy(buf + 64, &master,  4);
            buildResponse(CMD_GET_PEAKS, hdr->sequence, buf, 68);
            return;
        }

        case CMD_TRIGGER_LIVE:
            if (hdr->length >= 2) Audio_TriggerPad(p[0], p[1]);
            break;

        case CMD_TRIGGER_SEQ:
            if (hdr->length >= 8) {
                uint32_t maxS = 0;
                memcpy(&maxS, p + 4, 4);
                Audio_TriggerPadSeq(p[0], p[1], p[2], maxS);
            }
            break;

        case CMD_TRIGGER_STOP:
            if (hdr->length >= 1) Audio_StopPad(p[0]);
            break;

        case CMD_TRIGGER_STOP_ALL:
            Audio_StopAll();
            break;

        case CMD_MASTER_VOLUME:
            if (hdr->length >= 1) Audio_SetMasterVolume(p[0]);
            break;

        case CMD_TRACK_VOLUME:
            if (hdr->length >= 2) Audio_SetTrackVolume(p[0], p[1]);
            break;

        case CMD_SAMPLE_BEGIN:
            if (hdr->length >= 12) {
                uint32_t tb = 0, ts = 0;
                memcpy(&tb, p + 4, 4);
                memcpy(&ts, p + 8, 4);
                Audio_SampleBegin(p[0], tb, ts);
            }
            break;

        case CMD_SAMPLE_DATA:
            if (hdr->length >= 8) {
                uint16_t chunkSize = 0;
                uint32_t offset    = 0;
                memcpy(&chunkSize, p + 2, 2);
                memcpy(&offset,    p + 4, 4);
                Audio_SampleData(p[0], offset, p + 8, chunkSize);
            }
            break;

        case CMD_SAMPLE_END:
            if (hdr->length >= 1) Audio_SampleEnd(p[0]);
            break;

        case CMD_RESET:
            Audio_Reset();
            break;

        default:
            break;
    }

    HAL_SPI_Receive_DMA(&hspi1, rxBuf, 8);
}

void HAL_SPI_TxCpltCallback(SPI_HandleTypeDef *hspi) {
    if (hspi->Instance != SPI1) return;
    HAL_SPI_Receive_DMA(&hspi1, rxBuf, 8);
}
```

---

## 9. Llamada desde main.c

```c
int main(void) {
    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();
    MX_DMA_Init();     /* DMA debe inicializarse ANTES que SPI */
    MX_SPI1_Init();

    SPI_Slave_Init();  /* ← aquí */

    /* ... tu init de audio / I2S / DAC ... */

    while (1) {
        /* El procesado SPI es 100% por interrupción/DMA */
        /* Aquí va tu loop de audio */
    }
}
```

---

## 10. Criterio de éxito

### Fase 1 — PING OK

Monitor ESP32 debe mostrar:
```
[SPI RX] #000 PING      OK len=8
[SPI] STM32 connected! RTT: 600 us
```
En lugar del actual:
```
[SPI RX] #000 PING      FAIL magic=0xFF cmd=0xFF len=65535
         raw: FF FF FF FF FF FF FF FF
```

### Fase 2 — Audio OK

Al golpear pad desde la web:
```
[PAD] trigger pad=3 vel=127
[SPI TX] TRIG_LIVE cmd=0x02 len=2
         data: 03 7F
```
→ STM32 reproduce el sample del pad 3 (808 OH 1.wav, 6912 muestras, 13824 bytes)

---

## 11. Proceso de bring-up

1. Añadir `red808_spi_slave.c/.h` al proyecto CubeIDE
2. SPI1 en CubeMX: Slave, Hardware NSS, Mode 0, MSB first, 8 bits, DMA RX+TX habilitado
3. `MX_DMA_Init()` antes de `MX_SPI1_Init()` en main.c
4. Llamar `SPI_Slave_Init()` tras `MX_SPI1_Init()`
5. Stub mínimo: `Audio_TriggerPad()` → toggle LED → verificar que el trigger llega
6. Verificar en monitor ESP32: `PING OK` con RTT < 2000 µs
7. Implementar `Audio_SampleBegin/Data/End` → los samples ya se envían al arrancar
8. Implementar `Audio_TriggerPad()` real con reproducción PCM via I2S/DAC
9. Una vez estable: avisar al equipo ESP32 para subir clock a **20 MHz**

---

## 12. Notas importantes

- NSS debe ser **Hardware NSS** en CubeMX — sin esto el flanco rising no dispara el callback
- DMA debe inicializarse **antes** que SPI (`MX_DMA_Init()` → `MX_SPI1_Init()`)
- El TX buffer debe estar listo antes del segundo CS LOW — 500 µs con STM32 a ≥48 MHz es más que suficiente
- Los comandos trigger/volumen/FX/sample son **fire-and-forget** — no se responde nada, no hay Txn 2
- Si el ESP32 muestra `magic=0x00` en lugar de `0xFF` → NSS llega pero el buffer está vacío → `HAL_SPI_Transmit_IT` no se está llamando a tiempo

---

## 13. BUGS CONOCIDOS — Diagnóstico y correcciones (post primera prueba)

> Síntomas reportados tras primera integración: audio suena muy mal (tipo caja de zapatos), volumen/efectos no hacen nada, la placa se reinicia sola al rato.

---

### BUG 1 — Sonido distorsionado / pitch incorrecto ("tipo caja de zapatos")

**Causa raíz:** Los samples PCM son **mono 16-bit**. Si el I2S está configurado como **estéreo**, escribir los bytes mono directamente al FIFO produce que los bytes alternos van a L y R:

```
Sample mono real:   [sampleA_L] [sampleA_H] [sampleB_L] [sampleB_H] ...
I2S estéreo ve:     [   L_word = sampleA   ] [   R_word = sampleB   ] ...
  → toca a MITAD de velocidad + pitch una octava abajo + completamente desalineado en fase
```

**Fix (opción A — recomendada):** Duplicar cada muestra en L y R al escribir. En tu `Audio_TriggerPad()` / ISR de I2S:

```c
// En el callback o DMA half/complete del I2S TX:
// pcmBuffer es int16_t*, numSamples es el total de muestras (no bytes)

uint8_t i2sBuf[DMA_CHUNK * 4];  // 2 canales × 2 bytes/muestra
for (uint32_t i = 0; i < DMA_CHUNK; i++) {
    int16_t s = pcmBuffer[playPos + i];
    // Aplicar volumen master y track
    int32_t sv = ((int32_t)s * masterGain * trackGain) >> 14;  // Q14 gain
    if (sv >  32767) sv =  32767;
    if (sv < -32768) sv = -32768;
    int16_t out = (int16_t)sv;
    // Canal L
    i2sBuf[i*4 + 0] = (uint8_t)(out & 0xFF);
    i2sBuf[i*4 + 1] = (uint8_t)(out >> 8);
    // Canal R (idéntico)
    i2sBuf[i*4 + 2] = (uint8_t)(out & 0xFF);
    i2sBuf[i*4 + 3] = (uint8_t)(out >> 8);
}
HAL_I2S_Transmit_DMA(&hi2s1, (uint16_t*)i2sBuf, DMA_CHUNK * 2);
```

**Fix (opción B):** Configurar el I2S en CubeMX como **Full-Duplex Master, Mono** con `hdataformat = I2S_DATAFORMAT_16B` y `Audio Frequency = 44100`. Asegúrate de que `Standard = I2S_STANDARD_PHILIPS`.

---

### BUG 2 — Volumen y efectos no hacen nada

**Causa raíz:** Las funciones stub están vacías. El ESP32 envía los comandos correctamente (llegan al STM32), pero las funciones no aplican nada.

**Fix mínimo — volumen master + track:**

```c
// En tu módulo de audio, variables globales:
static uint8_t masterVolume = 100;  // 0-100
static uint8_t trackVolume[16]  = {[0 ... 15] = 100};  // 0-150

// Implementar estos stubs:
void Audio_SetMasterVolume(uint8_t volume) {
    masterVolume = volume;
}
void Audio_SetTrackVolume(uint8_t track, uint8_t volume) {
    if (track < 16) trackVolume[track] = volume;
}

// En el mezclador, calcular gain final (Q14 fixed point):
// gain = masterVolume * trackVolume[pad] / (100 * 100)  → rango 0.0-1.5
// En entero Q14: gain_q14 = (masterVolume * trackVolume[pad] * 16384) / 10000
int32_t gain_q14 = ((int32_t)masterVolume * trackVolume[padIndex] * 16384) / 10000;
int32_t out = ((int32_t)sample * gain_q14) >> 14;
```

**Nota sobre efectos (filter, delay, flanger, etc.):**
El ESP32 envía los comandos por SPI (hay >30 comandos de FX definidos). Todos son `fire-and-forget`. Implementarlos en orden de prioridad:
1. `CMD_MASTER_VOLUME` (0x10) ← ya arriba
2. `CMD_TRACK_VOLUME` (0x13) ← ya arriba
3. `CMD_TRIGGER_STOP` (0x03) y `CMD_TRIGGER_STOP_ALL` (0x04) ← parar sample inmediatamente
4. FX en orden de dificultad: delay → filtro biquad → compressor

---

### BUG 3 — Placa se reinicia sola al rato

**Causa raíz más probable: `HAL_SPI_Transmit_IT()` llamado desde dentro de `HAL_SPI_RxCpltCallback()`** (contexto de interrupción DMA), mientras el SPI puede estar todavía ocupado con transacciones anteriores → `HAL_BUSY` → estado SPI corrupto → Hard Fault → reset.

**Fix — separar TX de la ISR usando un flag:**

```c
// Variables globales:
static volatile uint8_t pendingResponseReady = 0;
static uint8_t pendingTxBuf[TX_BUF_SIZE];
static uint16_t pendingTxLen = 0;

// En buildResponse(), en lugar de llamar HAL_SPI_Transmit_IT directamente:
static void buildResponse(uint8_t cmd, uint16_t seq,
                           const uint8_t* payload, uint16_t payloadLen) {
    SPIPacketHeader* resp = (SPIPacketHeader*)pendingTxBuf;
    resp->magic    = SPI_MAGIC_RESP;
    resp->cmd      = cmd;
    resp->length   = payloadLen;
    resp->sequence = seq;
    resp->checksum = (payloadLen > 0) ? crc16(payload, payloadLen) : 0;
    if (payloadLen > 0 && payload != NULL)
        memcpy(pendingTxBuf + sizeof(SPIPacketHeader), payload, payloadLen);
    pendingTxLen = sizeof(SPIPacketHeader) + payloadLen;
    pendingResponseReady = 1;  // ← flag, no transmitir aquí
}

// En el main loop (while(1)):
void SPI_Slave_Process(void) {
    if (pendingResponseReady) {
        pendingResponseReady = 0;
        HAL_StatusTypeDef st = HAL_SPI_Transmit_IT(&hspi1, pendingTxBuf, pendingTxLen);
        if (st != HAL_OK) {
            // Reintentar en el siguiente ciclo
            pendingResponseReady = 1;
        }
    }
}

// Añadir en while(1) del main:
//   SPI_Slave_Process();
```

**Segunda causa probable: Watchdog (IWDG)** activado en CubeMX sin `HAL_IWDG_Refresh()` en el loop. Si el loop de audio lleva >1s bloqueado procesando samples grandes → timeout → reset. Fix: llamar `HAL_IWDG_Refresh(&hiwdg)` frecuentemente en el loop, o deshabilitar IWDG en fase de debug.

**Tercera causa: Stack overflow** si las variables de audio son grandes y están en el stack en lugar de ser `static` o en heap/CCRAM. Asegúrate de que los buffers de samples (que pueden ser de hasta 88 KB cada uno) estén en SRAM no en el stack:
```c
// MAL (stack overflow si es buffer grande):
void Audio_SampleBegin(...) {
    uint8_t tempBuf[88202];  // ← NUNCA en el stack
}

// BIEN:
static uint8_t sampleStorage[24][MAX_SAMPLE_BYTES];  // en BSS / CCRAM
// O con malloc si tienes heap suficiente
```

---

### Resumen de prioridades de fix

| # | Problema                  | Fix                                        | Impacto |
|---|---------------------------|--------------------------------------------|---------|
| 1 | Sonido distorsionado      | Duplicar mono→estéreo en write I2S         | CRÍTICO |
| 2 | Reset aleatorio           | Mover HAL_SPI_Transmit_IT fuera de la ISR  | CRÍTICO |
| 3 | Volumen no funciona       | Implementar masterVolume/trackVolume scale  | ALTO    |
| 4 | Stop no funciona          | Implementar Audio_StopPad() y StopAll()    | ALTO    |
| 5 | Efectos no funcionan      | Delay → Filter → Compresor (en ese orden)  | MEDIO   |

---

## 14. CORRECCIONES AL DOCUMENTO DEL EQUIPO SLAVE

> El equipo STM32 ha compartido su interpretación del protocolo. Hay **5 errores que deben corregirse antes de implementar**.

---

### ❌ ERROR 1 — CRÍTICO: "Los samples se cargan desde SD al arrancar el STM32"

**Esto es INCORRECTO. No hay tarjeta SD en el STM32.**

Los samples vienen **del ESP32 via SPI** al arrancar, usando los comandos `SMPL_BEG` / `SMPL_DATA` / `SMPL_END`. El STM32 los recibe y los almacena en su SRAM/PSRAM interna.

```
Boot sequence real:
  ESP32 arranca → carga WAVs desde su Flash/LittleFS → PSRAM
  ESP32 envía cada sample al STM32 via SPI:
    CMD_SAMPLE_BEGIN (0xA0) → avisa tamaño total
    CMD_SAMPLE_DATA  (0xA1) → chunks de 512 bytes PCM (muchas transacciones)
    CMD_SAMPLE_END   (0xA2) → confirma fin de transfer

  Solo después empieza a enviar triggers.
```

El STM32 debe:
1. Recibir y almacenar los samples en RAM interna (`Audio_SampleBegin/Data/End`)
2. Reproducirlos desde RAM cuando llegue el trigger
3. **Nunca acceder a ninguna SD** — no existe en este diseño

---

### ❌ ERROR 2 — CRÍTICO: Mapa de pads INCORRECTO

El equipo slave tiene este mapa (secuencial 0-15):
```
0=BD, 1=SD, 2=CH, 3=OH, 4=CY, 5=CP, 6=RS, 7=CB, 8=LT, ...
```

**Esto es incorrecto.** Los `padIndex` en los triggers corresponden al índice real del sample tal como fue enviado con `SMPL_BEG`. El mapa real, verificado con los logs de boot, es:

| padIndex en trigger | Sonido           | Archivo           |
|---------------------|------------------|-------------------|
| **1**               | SD — Snare       | 808 SD 1-5.wav    |
| **2**               | CH — Closed HiHat| 808 HH.wav        |
| **3**               | OH — Open HiHat  | 808 OH 1.wav      |
| **4**               | CY — Cymbal      | 808 CY 3-1.wav    |
| **6**               | RS — Rimshot     | 808 RS.wav        |
| **8**               | LT — Low Tom     | LT10.WAV          |
| **9**               | MT — Mid Tom     | MT00.WAV          |
| **10**              | HT — High Tom    | 808 HT 3.wav      |
| **11**              | MA — Maracas     | 808 MA.wav        |
| **13**              | HC — High Conga  | 808 HC 1.wav      |
| **14**              | MC — Mid Conga   | 808 MC 3.wav      |
| **15**              | LC — Low Conga   | LC00.WAV          |

**Notas importantes:**
- `padIndex 0` (BD/Bombo) **no está cargado** en este kit — no llegará `SMPL_BEG` para el pad 0
- Los índices **5, 7, 12** tampoco tienen sample en este kit
- El STM32 debe indexar los samples **exactamente por el `padIndex` recibido en `SMPL_BEG`**, no por posición secuencial
- Si llega un trigger para un pad sin sample cargado → ignorar silenciosamente

---

### ❌ ERROR 3 — CMD_PING valor incorrecto

El equipo slave indica `CMD_PING = 0x60`. **El valor real es `0xEE`.**

```c
#define CMD_PING  0xEE   // ← correcto
// NO:  0x60            // ← incorrecto
```

El ESP32 envía PING con `cmd=0xEE` y espera respuesta con `magic=0x5A, cmd=0xEE`.

---

### ❌ ERROR 4 — Rango MASTER_VOLUME incorrecto

El equipo slave indica rango `0-180`. **El rango real es `0-100`** (uint8_t, porcentaje).

```c
// CMD_MASTER_VOLUME (0x10): payload = 1 byte
uint8_t volume;  // 0-100  (no 0-180)

// CMD_TRACK_VOLUME (0x13): payload = 2 bytes
uint8_t track;   // 0-15
uint8_t volume;  // 0-150  (track volume sí puede superar 100 para boost)
```

---

### ❌ ERROR 5 — Payload TRIGGER_SEQ incompleto

El equipo slave indica `[pad, velocity, trackVol?] (2-3 bytes)`. **El payload real es 8 bytes:**

```c
typedef struct __attribute__((packed)) {
    uint8_t  padIndex;     // byte 0
    uint8_t  velocity;     // byte 1
    uint8_t  trackVolume;  // byte 2  (0-150)
    uint8_t  reserved;     // byte 3  (siempre 0x00)
    uint32_t maxSamples;   // bytes 4-7 (0=sample completo, >0=cortar)
} TriggerSeqPayload;       // 8 bytes total
```

---

### ✅ Lo que el equipo slave tiene CORRECTO

- Conexión SPI: SCK, MOSI, MISO, NSS — correcto
- NSS en Hardware Slave mode — correcto
- Formato header 8 bytes + payload — correcto
- CRC16 Modbus — correcto
- Fire-and-forget para triggers/volumen — correcto
- Timing ~500µs entre transacciones — correcto
- Velocidad SPI máx ~10MHz — aceptable (estamos en 2MHz en bring-up)

---

## 15. MIGRACIÓN A DAISY SEED — Guía completa

> La Daisy Seed (STM32H750 @ 480MHz + 64MB SDRAM + codec WM8731 integrado) es la opción óptima para este proyecto. Todo el protocolo SPI existente se mantiene igual, solo cambia el framework.

---

### 15.1 Por qué Daisy Seed es perfecta para este proyecto

| Característica      | STM32 genérico         | Daisy Seed                          |
|---------------------|------------------------|-------------------------------------|
| RAM para samples    | 512KB–1MB SRAM         | **64MB SDRAM** ← cabe todo el kit   |
| Audio codec         | I2S externo manual     | **WM8731 integrado** ← plug & play  |
| Framework audio     | HAL manual + DMA       | **libDaisy AudioHandle** ← 3 líneas |
| CPU                 | 72–168 MHz             | **480 MHz** ← voces polifónicas     |
| DSP                 | Manual                 | **DaisySP** ← filtros, efectos listos|
| SPI Slave           | HAL_SPI_Receive_DMA    | **SpiHandle** (misma HAL debajo)    |

---

### 15.2 CRÍTICO — Sample rate: 44100 Hz vs 48000 Hz

Los samples del ESP32 son **44100 Hz**. El codec WM8731 de Daisy por defecto trabaja a **48000 Hz**.

**Opciones (elige una):**

**Opción A — Configurar Daisy a 44100 Hz (recomendado):**
```cpp
// En main(), antes de seed.StartAudio():
AudioHandle::Config audio_config;
audio_config.blocksize  = 128;
audio_config.samplerate = SaiHandle::Config::SampleRate::SAI_44KHZ;  // ← 44100 Hz
audio_config.postgain   = 1.0f;
seed.audio_handle.Init(audio_config);
```

**Opción B — Interpolar 44100→48000 en Daisy (ratio fijo):**
```cpp
// En el mezclador, avanzar el puntero de reproducción a ratio:
// ratio = 44100.0f / 48000.0f = 0.91875f
// playPos acumulado en float, leer sample[floor(playPos)]
float ratio = 44100.0f / 48000.0f;
playPosF += ratio;  // avanzar fraccionariamente
int16_t s = sampleBuf[padIndex][(uint32_t)playPosF];
```

**Opción C — Que el ESP32 reenvíe los samples a 48000 Hz**: no recomendado, requiere cambios en `SampleManager.cpp`.

---

### 15.3 Pinout SPI — Daisy Seed ↔ ESP32-S3

| Señal    | Daisy Seed pin | Pin físico STM32H750 | ESP32-S3 GPIO |
|----------|----------------|---------------------|---------------|
| SCK      | D10            | PC10 (SPI3_SCK)     | GPIO 12       |
| MOSI     | D9             | PC11 (SPI3_MISO)⚠   | GPIO 11       |
| MISO     | D8             | PC12 (SPI3_MOSI)⚠   | GPIO 13       |
| NSS / CS | D7             | PA15 (SPI3_NSS)     | GPIO 10       |
| GND      | GND            | GND                 | GND           |

> ⚠ En libDaisy el SPI3 usa PC11=MISO y PC12=MOSI. Verifica en el schematic de tu Daisy que los pines coinciden. Alternativamente usa **SPI1** en los pines D25/D26/D27/D28 (PA5/PA6/PA7/PA4) que son más accesibles.

---

### 15.4 Implementación completa libDaisy

#### red808_daisy.cpp — archivo único, todo incluido

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
#define MAX_SAMPLE_BYTES  (96000 * 2)  // ~2s a 48kHz, 16-bit

DSY_SDRAM_BSS static int16_t sampleStorage[MAX_PADS][MAX_SAMPLE_BYTES / 2];
static uint32_t sampleLength[MAX_PADS];  // en muestras (int16_t)
static bool     sampleLoaded[MAX_PADS];

// ─── Voces polifónicas ────────────────────────────────────────────────────────
#define MAX_VOICES  10
struct Voice {
    bool     active;
    uint8_t  pad;
    float    pos;       // posición fractional (para pitch / interpolación SR)
    float    speed;     // 1.0 = normal, >1 = faster
    float    gainL;     // volumen final 0.0-1.5
};
static Voice voices[MAX_VOICES];

// ─── Volúmenes ────────────────────────────────────────────────────────────────
static float masterGain   = 1.0f;   // 0.0-1.0
static float trackGain[MAX_PADS];   // 0.0-1.5

// ─── Peaks para GET_PEAKS ─────────────────────────────────────────────────────
static volatile float trackPeak[MAX_PADS];
static volatile float masterPeak = 0.0f;

// ─── DaisySP Master FX ───────────────────────────────────────────────────────
#define MAX_DELAY_SAMPLES  88200  // 2s a 44100 Hz
static DelayLine<float, MAX_DELAY_SAMPLES> DSY_SDRAM_BSS masterDelay;
static ReverbSc   masterReverb;
static Chorus     masterChorus;
static Tremolo    masterTremolo;
static Compressor masterComp;
static Fold       masterFold;

// FX state
static bool  delayActive   = false;
static float delayTime     = 250.0f; // ms
static float delayFeedback = 0.3f;
static float delayMix      = 0.3f;

static bool  reverbActive   = false;
static float reverbFeedback = 0.85f;
static float reverbLpFreq   = 8000.0f;
static float reverbMix      = 0.3f;

static bool  chorusActive = false;
static float chorusMix    = 0.4f;

static bool  tremoloActive = false;

static bool  compActive = false;

static float waveFolderGain = 1.0f;  // 1.0 = off
static bool  limiterActive  = false;

// Per-pad state
static bool  padLoop[MAX_PADS]    = {false};
static bool  padReverse[MAX_PADS] = {false};
static float padPitch[MAX_PADS];   // init to 1.0f

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

// ─── Audio Callback (Core del mezclador) ─────────────────────────────────────
// Llamado por libDaisy cada 'blocksize' muestras — NO BLOQUEAR
void AudioCallback(AudioHandle::InputBuffer  /*in*/,
                   AudioHandle::OutputBuffer  out,
                   size_t                     size)
{
    // Limpiar salida
    for (size_t i = 0; i < size; i++) out[0][i] = out[1][i] = 0.0f;

    float mixPeak = 0.0f;

    for (int v = 0; v < MAX_VOICES; v++) {
        Voice& vx = voices[v];
        if (!vx.active) continue;

        for (size_t i = 0; i < size; i++) {
            uint32_t idx = (uint32_t)vx.pos;

            // Bounds check con loop/reverse
            if (padReverse[vx.pad]) {
                if (vx.pos < 0.0f) {
                    if (padLoop[vx.pad]) { vx.pos = (float)(sampleLength[vx.pad] - 1); }
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

            // Interpolación lineal entre muestras
            float frac = fabsf(vx.pos) - idx;
            float s0   = sampleStorage[vx.pad][idx];
            float s1   = (idx + 1 < sampleLength[vx.pad])
                         ? sampleStorage[vx.pad][idx + 1] : 0.0f;
            float s    = (s0 + frac * (s1 - s0)) / 32768.0f;  // normalizar -1..+1
            float gain = vx.gainL;
            float fout = s * gain;

            out[0][i] += fout;
            out[1][i] += fout;

            // Avanzar con pitch y dirección
            float spd = vx.speed * padPitch[vx.pad];
            vx.pos += padReverse[vx.pad] ? -spd : spd;

            // Peak tracking
            float absv = fabsf(fout);
            if (absv > trackPeak[vx.pad]) trackPeak[vx.pad] = absv;
            if (absv > mixPeak)           mixPeak = absv;
        }
    }

    // ── Master FX chain ──────────────────────────────────────────────────────
    for (size_t i = 0; i < size; i++) {
        float L = out[0][i] * masterGain;
        float R = L;  // mono → stereo

        // Delay
        if (delayActive) {
            float wet = masterDelay.Read();
            masterDelay.Write(L + wet * delayFeedback);
            L = L * (1.0f - delayMix) + wet * delayMix;
        }

        // Compressor
        if (compActive) {
            L = masterComp.Process(L);
        }

        // WaveFolder (gain > 1.0 = fold)
        if (waveFolderGain > 1.01f) {
            masterFold.SetIncrement(waveFolderGain);
            L = masterFold.Process(L);
        }

        // Tremolo (amplitude modulation)
        if (tremoloActive) {
            L = masterTremolo.Process(L);
        }

        // Chorus
        if (chorusActive) {
            float wet = masterChorus.Process(L);
            L = L * (1.0f - chorusMix) + wet * chorusMix;
        }

        // Reverb (stereo)
        float revL = 0.0f, revR = 0.0f;
        if (reverbActive) {
            masterReverb.Process(L, L, &revL, &revR);
            L = L * (1.0f - reverbMix) + revL * reverbMix;
            R = L * (1.0f - reverbMix) + revR * reverbMix;
        } else {
            R = L;
        }

        // Limiter (brick-wall soft clip)
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

// ─── Trigger un pad ──────────────────────────────────────────────────────────
static void TriggerPad(uint8_t pad, uint8_t velocity,
                       uint8_t trkVol = 100, uint32_t maxSamples = 0)
{
    if (pad >= MAX_PADS || !sampleLoaded[pad]) return;
    // Buscar voz libre (o robar la más antigua)
    int slot = -1;
    for (int i = 0; i < MAX_VOICES; i++) {
        if (!voices[i].active) { slot = i; break; }
    }
    if (slot < 0) slot = 0;  // robar voz 0

    float velGain  = velocity / 127.0f;
    float trkScale = trkVol   / 100.0f;

    voices[slot].active = true;
    voices[slot].pad    = pad;
    voices[slot].pos    = 0.0f;
    voices[slot].speed  = 1.0f;  // ajustar si SR != 44100
    voices[slot].gainL  = velGain * trkScale;

    // Limitar duración si maxSamples > 0
    if (maxSamples > 0 && maxSamples < sampleLength[pad])
        sampleLength[pad] = maxSamples;  // temporal — restaurar en SampleEnd
}

// ─── Recepción SPI: procesar comando ─────────────────────────────────────────
static void BuildResponse(uint8_t cmd, uint16_t seq,
                          const uint8_t* payload, uint16_t payloadLen) {
    SPIPacketHeader* r = (SPIPacketHeader*)txBuf;
    r->magic    = SPI_MAGIC_RESP;
    r->cmd      = cmd;
    r->length   = payloadLen;
    r->sequence = seq;
    r->checksum = payloadLen ? crc16(payload, payloadLen) : 0;
    if (payloadLen && payload) memcpy(txBuf + 8, payload, payloadLen);
    pendingTxLen   = 8 + payloadLen;
    pendingResponse = true;
    // La transmisión se lanza desde el main loop (evitar HAL_BUSY en ISR)
}

static void ProcessCommand() {
    SPIPacketHeader* hdr = (SPIPacketHeader*)rxBuf;
    uint8_t* p = rxBuf + 8;

    switch (hdr->cmd) {

        case CMD_PING: {
            uint32_t echo = 0, uptime = seed.system.GetNow();
            if (hdr->length >= 4) memcpy(&echo, p, 4);
            uint8_t pong[8];
            memcpy(pong,     &echo,   4);
            memcpy(pong + 4, &uptime, 4);
            BuildResponse(CMD_PING, hdr->sequence, pong, 8);
            return;
        }

        case CMD_GET_PEAKS: {
            float buf[17];
            for (int i = 0; i < 16; i++) { buf[i] = trackPeak[i]; trackPeak[i] = 0.0f; }
            buf[16] = masterPeak;
            BuildResponse(CMD_GET_PEAKS, hdr->sequence, (uint8_t*)buf, 68);
            return;
        }

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

        case CMD_MASTER_VOLUME:
            if (hdr->length >= 1) masterGain = p[0] / 100.0f;
            break;

        case CMD_TRACK_VOLUME:
            if (hdr->length >= 2 && p[0] < MAX_PADS) trackGain[p[0]] = p[1] / 100.0f;
            break;

        case CMD_SAMPLE_BEGIN:
            if (hdr->length >= 12) {
                uint8_t pad = p[0];
                uint32_t totalBytes = 0; memcpy(&totalBytes, p + 4, 4);
                sampleLength[pad] = 0;
                sampleLoaded[pad] = false;
                // totalBytes / 2 = num muestras int16_t
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
            if (hdr->length >= 8) {
                uint8_t pad = p[0];
                if (pad < MAX_PADS) {
                    // Calcular longitud real desde totalBytes recibido en SMPL_BEG
                    // Se puede guardar en SMPL_BEG y aplicar aquí
                    sampleLoaded[pad] = true;
                }
            }
            break;

        case CMD_RESET:
            for (int i = 0; i < MAX_VOICES; i++) voices[i].active = false;
            for (int i = 0; i < MAX_PADS; i++) { sampleLoaded[i] = false; sampleLength[i] = 0; }
            break;

        // ── Trigger Sidechain (0x05) ────────────────────────────────────────
        case CMD_TRIGGER_SIDECHAIN:
            if (hdr->length >= 2) {
                // p[0] = padIndex, p[1] = intensity — el compresor usa esto
                compSidechainEnv = p[1] / 127.0f;
            }
            break;

        // ── Bulk Triggers (0xF0) ────────────────────────────────────────────
        case CMD_BULK_TRIGGERS:
            if (hdr->length >= 2) {
                uint8_t count = p[0];
                for (uint8_t i = 0; i < count && (1 + i * 2 + 1) < hdr->length; i++)
                    TriggerPad(p[1 + i * 2], p[1 + i * 2 + 1]);
            }
            break;

        // ── Delay (0x30-0x33) ───────────────────────────────────────────────
        case CMD_DELAY_ACTIVE:
            if (hdr->length >= 1) delayActive = (p[0] != 0);
            break;
        case CMD_DELAY_TIME:
            if (hdr->length >= 2) {
                uint16_t ms = 0; memcpy(&ms, p, 2);
                delayTime = (float)ms / 1000.0f;
                masterDelay.SetDelay(delayTime * SAMPLE_RATE);
            }
            break;
        case CMD_DELAY_FEEDBACK:
            if (hdr->length >= 1) delayFeedback = p[0] / 100.0f;
            break;
        case CMD_DELAY_MIX:
            if (hdr->length >= 1) delayMix = p[0] / 100.0f;
            break;

        // ── Filter por track (0x34-0x37) ────────────────────────────────────
        case CMD_FILTER_TYPE:
            // TODO: per-track filter type
            break;
        case CMD_FILTER_FREQ:
            // TODO: per-track filter frequency
            break;
        case CMD_FILTER_RES:
            // TODO: per-track filter resonance
            break;
        case CMD_FILTER_ACTIVE:
            // TODO: per-track filter enable
            break;

        // ── Compressor (0x3D-0x42) ──────────────────────────────────────────
        case CMD_COMP_ACTIVE:
            if (hdr->length >= 1) compActive = (p[0] != 0);
            break;
        case CMD_COMP_THRESHOLD:
            if (hdr->length >= 1) masterComp.SetThreshold(-((float)p[0]));  // dB negativo
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

        // ── Reverb (0x43-0x46) ──────────────────────────────────────────────
        case CMD_REVERB_ACTIVE:
            if (hdr->length >= 1) {
                reverbActive = (p[0] != 0);
            } else if (hdr->length >= sizeof(ReverbPayload)) {
                // All-in-one payload
                ReverbPayload rp; memcpy(&rp, p, sizeof(rp));
                reverbActive   = true;
                reverbFeedback = rp.feedback / 100.0f;
                reverbLpFreq   = rp.lpFreq;
                reverbMix      = rp.mix / 100.0f;
                masterReverb.SetFeedback(reverbFeedback);
                masterReverb.SetLpFreq(reverbLpFreq);
            }
            break;
        case CMD_REVERB_FEEDBACK:
            if (hdr->length >= 1) {
                reverbFeedback = p[0] / 100.0f;
                masterReverb.SetFeedback(reverbFeedback);
            }
            break;
        case CMD_REVERB_LPFREQ: {
            if (hdr->length >= 2) {
                uint16_t f = 0; memcpy(&f, p, 2);
                reverbLpFreq = (float)f;
                masterReverb.SetLpFreq(reverbLpFreq);
            }
            break;
        }
        case CMD_REVERB_MIX:
            if (hdr->length >= 1) reverbMix = p[0] / 100.0f;
            break;

        // ── Chorus (0x47-0x4A) ──────────────────────────────────────────────
        case CMD_CHORUS_ACTIVE:
            if (hdr->length >= 1) {
                chorusActive = (p[0] != 0);
            } else if (hdr->length >= sizeof(ChorusPayload)) {
                ChorusPayload cp; memcpy(&cp, p, sizeof(cp));
                chorusActive = true;
                chorusMix    = cp.mix / 100.0f;
                masterChorus.SetLfoFreq(cp.rate / 10.0f);
                masterChorus.SetLfoDepth(cp.depth / 100.0f);
            }
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

        // ── Tremolo (0x4B-0x4D) ─────────────────────────────────────────────
        case CMD_TREMOLO_ACTIVE:
            if (hdr->length >= 1) {
                tremoloActive = (p[0] != 0);
            } else if (hdr->length >= sizeof(TremoloPayload)) {
                TremoloPayload tp; memcpy(&tp, p, sizeof(tp));
                tremoloActive = true;
                masterTremolo.SetFreq(tp.rate / 10.0f);
                masterTremolo.SetDepth(tp.depth / 100.0f);
            }
            break;
        case CMD_TREMOLO_RATE:
            if (hdr->length >= 1) masterTremolo.SetFreq(p[0] / 10.0f);
            break;
        case CMD_TREMOLO_DEPTH:
            if (hdr->length >= 1) masterTremolo.SetDepth(p[0] / 100.0f);
            break;

        // ── WaveFolder (0x4E) ───────────────────────────────────────────────
        case CMD_WAVEFOLDER_GAIN:
            if (hdr->length >= 1) waveFolderGain = p[0] / 10.0f;  // 0-255 → 0.0-25.5
            break;

        // ── Limiter (0x4F) ──────────────────────────────────────────────────
        case CMD_LIMITER_ACTIVE:
            if (hdr->length >= 1) limiterActive = (p[0] != 0);
            break;

        // ── Pad Loop / Reverse / Pitch (0x74-0x76) ─────────────────────────
        case CMD_PAD_LOOP:
            if (hdr->length >= 2 && p[0] < MAX_PADS) padLoop[p[0]] = (p[1] != 0);
            break;
        case CMD_PAD_REVERSE:
            if (hdr->length >= 2 && p[0] < MAX_PADS) padReverse[p[0]] = (p[1] != 0);
            break;
        case CMD_PAD_PITCH:
            if (hdr->length >= 3 && p[0] < MAX_PADS) {
                int16_t cents = 0; memcpy(&cents, p + 1, 2);      // -1200..+1200
                padPitch[p[0]] = powf(2.0f, cents / 1200.0f);     // ratio
            }
            break;

        // ── Unload sample (0xA3) ────────────────────────────────────────────
        case CMD_SAMPLE_UNLOAD:
            if (hdr->length >= 1 && p[0] < MAX_PADS) {
                sampleLoaded[p[0]] = false;
                sampleLength[p[0]] = 0;
            }
            break;

        // ── Status queries (0xE0, 0xE2, 0xE3) ──────────────────────────────
        case CMD_GET_STATUS: {
            // Enviar StatusResponse completo
            uint8_t resp[60];
            memset(resp, 0, sizeof(resp));
            // Fill firmware version, loop counter, etc.
            BuildResponse(CMD_GET_STATUS, hdr->sequence, resp, sizeof(resp));
            return;
        }
        case CMD_GET_CPU_LOAD: {
            float load = seed.system.GetCpuLoad();
            uint8_t pct = (uint8_t)(load * 100.0f);
            BuildResponse(CMD_GET_CPU_LOAD, hdr->sequence, &pct, 1);
            return;
        }
        case CMD_GET_ACTIVE_VOICES: {
            uint8_t cnt = 0;
            for (int v = 0; v < MAX_VOICES; v++) if (voices[v].active) cnt++;
            BuildResponse(CMD_GET_ACTIVE_VOICES, hdr->sequence, &cnt, 1);
            return;
        }

        default: break;
    }
}

// ─── SPI DMA callbacks ────────────────────────────────────────────────────────
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
    // Si la respuesta llama BuildResponse, se lanzará desde main loop
    if (!pendingResponse)
        spi_slave.DmaReceive(rxBuf, 8, nullptr, SpiRxCallback, nullptr);
}

// ─── main ─────────────────────────────────────────────────────────────────────
int main() {
    seed.Init();
    seed.SetAudioBlockSize(128);

    // ── Configurar sample rate 44100 Hz ──────────────────────────────────────
    seed.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_44KHZ);

    // ── Inicializar arrays ────────────────────────────────────────────────────
    for (int i = 0; i < MAX_PADS;   i++) {
        sampleLoaded[i] = false; sampleLength[i] = 0;
        trackGain[i] = 1.0f; trackPeak[i] = 0.0f;
        padLoop[i]    = false;
        padReverse[i] = false;
        padPitch[i]   = 1.0f;
    }
    for (int i = 0; i < MAX_VOICES; i++) voices[i].active = false;

    // ── Inicializar FX (DaisySP) ─────────────────────────────────────────────
    float sr = SAMPLE_RATE;

    masterDelay.Init();
    masterDelay.SetDelay(sr * 0.25f);       // 250 ms por defecto

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
    masterFold.SetIncrement(1.0f);  // sin fold por defecto

    // ── SPI Slave ─────────────────────────────────────────────────────────────
    SpiHandle::Config spi_config;
    spi_config.periph        = SpiHandle::Config::Peripheral::SPI_3;  // D7-D10
    spi_config.mode          = SpiHandle::Config::Mode::SLAVE;
    spi_config.direction     = SpiHandle::Config::Direction::TWO_LINES;
    spi_config.datasize      = 8;
    spi_config.clock_polarity= SpiHandle::Config::ClockPolarity::LOW;
    spi_config.clock_phase   = SpiHandle::Config::ClockPhase::ONE_EDGE;
    spi_config.nss           = SpiHandle::Config::NSS::HARD_INPUT;  // Hardware NSS
    spi_config.baud_prescaler= SpiHandle::Config::BaudPrescaler::PS_128;  // irrelevante en slave
    spi_config.pin_config.sclk = seed.GetPin(10);
    spi_config.pin_config.miso = seed.GetPin(8);
    spi_config.pin_config.mosi = seed.GetPin(9);
    spi_config.pin_config.nss  = seed.GetPin(7);
    spi_slave.Init(spi_config);

    // Arrancar recepción DMA
    spi_slave.DmaReceive(rxBuf, 8, nullptr, SpiRxCallback, nullptr);

    // ── Audio ─────────────────────────────────────────────────────────────────
    seed.StartAudio(AudioCallback);

    // ── Main loop ─────────────────────────────────────────────────────────────
    while (1) {
        // Lanzar respuesta SPI pendiente desde aquí (no desde ISR)
        if (pendingResponse) {
            pendingResponse = false;
            spi_slave.DmaTransmit(txBuf, pendingTxLen, nullptr, nullptr, nullptr);
            // Rearmar RX tras TX completado
            // Como DmaTransmit es async, usar callback o delay mínimo:
            System::Delay(1);  // 1ms > suficiente para 76 bytes a 2MHz
            spi_slave.DmaReceive(rxBuf, 8, nullptr, SpiRxCallback, nullptr);
        }
    }
}
```

---

### 15.5 Almacenar sampleLength correctamente

El campo `totalSamples` llega en `SMPL_BEG`. Hay que guardarlo:

```cpp
// Variable adicional para guardar totalSamples prometido:
static uint32_t sampleTotalSamples[MAX_PADS];

// En CMD_SAMPLE_BEGIN:
case CMD_SAMPLE_BEGIN:
    if (hdr->length >= 12) {
        uint8_t pad = p[0];
        uint32_t ts = 0; memcpy(&ts, p + 8, 4);
        sampleTotalSamples[pad] = ts;
        sampleLoaded[pad]       = false;
        sampleLength[pad]       = 0;
    }
    break;

// En CMD_SAMPLE_END:
case CMD_SAMPLE_END:
    if (hdr->length >= 1) {
        uint8_t pad = p[0];
        sampleLength[pad] = sampleTotalSamples[pad];
        sampleLoaded[pad] = true;
    }
    break;
```

---

### 15.6 Corrección ERROR 1 del documento del equipo slave

Con Daisy Seed el ERROR 1 ya no aplica de la misma forma. La Daisy **sí puede usar su SD card opcional** (conectada por SPI2) para cargar los samples — pero en este proyecto el diseño es que **los samples lleguen del ESP32 via SPI** (protocolo SMPL_BEG/DATA/END). Con 64MB SDRAM hay espacio de sobra para todo el kit (≈500KB total de samples).

**Opciones con Daisy:**
1. **Diseño actual** (recomendado): samples desde ESP32 → SDRAM Daisy al arrancar
2. **Alternativa**: Daisy carga WAVs desde su SD card propia → ESP32 solo envía triggers. Esto requeriría cambiar el protocolo en el ESP32 (eliminar SMPL_* commands)

Para máxima compatibilidad con el protocolo ya implementado en el ESP32, **mantener diseño actual**.

---

### 15.7 Tabla comparativa — Por qué Daisy resuelve los 3 bugs de golpe

| Bug anterior          | Causa                          | En Daisy                              |
|-----------------------|--------------------------------|---------------------------------------|
| Sonido distorsionado  | I2S mono/stereo mal configurado| AudioCallback siempre estéreo correcto|
| Reset sola            | HAL_BUSY en ISR                | DmaTransmit desde main loop (no ISR)  |
| Volumen no funciona   | Stubs vacíos                   | `masterGain` y `trackGain[]` en mixer |

Los 3 bugs desaparecen con la implementación de arriba.

---

### 15.8 Setup libDaisy — qué instalar

```bash
# Opción 1: PlatformIO (recomendado si ya usas PlatformIO en ESP32)
# platformio.ini:
[env:daisy_seed]
platform = ststm32
board = daisy_seed
framework = arduino   # o "daisy" con el core oficial

# Opción 2: Make con libDaisy + DaisySP (oficial)
git clone https://github.com/electro-smith/libDaisy
git clone https://github.com/electro-smith/DaisySP
# Seguir: https://electro-smith.github.io/libDaisy/
```

---

### 15.9 Criterio de éxito con Daisy

Monitor ESP32 al arrancar:
```
[SPI] STM32 connected! RTT: 300 us      ← Daisy es más rápida que STM32 básico
✓ Samples loaded: 16/16                  ← mismos comandos SMPL_BEG/DATA/END
[SPI TX] TRIG_LIVE cmd=0x02 len=2
         data: 03 7F                     ← trigger llega a Daisy
```
→ Daisy reproduce el sample con AudioCallback a 44100 Hz estéreo, calidad perfecta

---

### 15.10 SD Card en la Daisy — Lectura de kits desde el Master

> **Nuevo (25/02/2026):** El ESP32 ahora puede explorar y cargar kits directamente desde la SD card de la Daisy. La Daisy carga los WAVs directo a SDRAM — **mucho más rápido** que transferir PCM por SPI.

#### Conexión SD Card

La Daisy Seed puede conectar una micro-SD a **SPI2** (pines D18-D21) o mediante **SDMMC** (4-bit, mucho más rápido). Usar libDaisy `SdmmcHandler` o `FatFSInterface`:

```
Daisy Seed pin | SD Card    | Función
D18            | CLK        | SDMMC_CK
D19            | CMD        | SDMMC_CMD
D20            | DAT0       | SDMMC_D0
D21            | DAT1       | SDMMC_D1
D22            | DAT2       | SDMMC_D2
D23            | DAT3       | SDMMC_D3
3V3            | VCC        |
GND            | GND        |
```

#### Estructura de carpetas en la SD

```
/data/
  ├── RED 808 KARZ/          ← Kit por defecto (LIVE PADS 0-15)
  │   ├── 808 BD 3-1.wav       Mapeo automático por nombre: BD→pad0, SD→1, etc.
  │   ├── 808 SD 1-5.wav
  │   ├── 808 HH.wav
  │   └── ... (16 wavs)
  │
  ├── BD/                     ← Familias de instrumentos (por pad)
  │   ├── BD0000.WAV            25 variantes de bass drum
  │   └── BD7575.WAV
  ├── SD/                     ← 25 variantes snare
  ├── CH/  OH/  CY/  CP/     ← Más familias
  ├── RS/  CB/  LT/  MT/
  ├── HT/  MA/  CL/  HC/
  ├── MC/  LC/
  │
  └── xtra/                   ← XTRA PADS (pads 16-23)
      ├── Alesis-Fusion-Bass-C3.wav
      ├── dre-yeah.wav
      └── ...
```

> **Actualizado 25/02/2026:** Rutas cambiadas de `/RED808/` a `/data/`. `CMD_SD_KIT_LIST` filtra familias (2 chars) y `xtra`, solo devuelve kits completos.

#### Flujo de operación

```
ESP32                         Daisy (SD + SDRAM)
  │                              │
  ├─ CMD_SD_KIT_LIST ──────────►│  Escanear /data/ (solo kits completos)
  │◄─ SdKitListResponse ────────┤  ["RED 808 KARZ", ...]
  │                              │
  │  (Usuario selecciona kit     │
  │   desde la web interface)    │
  │                              │
  ├─ CMD_SD_LIST_FILES ─────────►│  Listar /data/RED 808 KARZ/*.wav
  │◄─ SdFileListResponse ───────┤  [{BD.wav, 26KB}, {SD.wav, 18KB}, ...]
  │                              │
  ├─ CMD_SD_LOAD_KIT ───────────►│  Cargar todos los WAVs → SDRAM
  │  kitName="808 Classic"       │  (lectura directa SD→SDRAM, ~2MB/s)
  │                              │
  │  (Daisy lee WAV headers,     │
  │   decodifica PCM mono 16-bit,│
  │   almacena en sampleStorage) │
  │                              │
  ├─ CMD_SD_STATUS ─────────────►│  ¿Cargado?
  │◄─ SdStatusResponse ─────────┤  loaded=0xFFFF, kit="808 Classic"
  │                              │
  ├─ CMD_TRIGGER_LIVE ──────────►│  ¡Suena!
```

#### Velocidad comparativa

| Método                          | 16 samples (~500KB) | Ventaja           |
|---------------------------------|---------------------|-------------------|
| ESP32→SPI→Daisy (actual)        | ~12 segundos a 2MHz | Compatible legacy |
| ESP32→SPI→Daisy a 20MHz         | ~1.5 segundos       | Más rápido        |
| **Daisy SD→SDRAM (nuevo)**      | **~0.25 segundos**  | **~50x más rápido** |

#### Comandos nuevos (0xB0-0xB9)

| CMD   | Nombre              | Payload ESP32→Daisy              | Respuesta Daisy→ESP32          |
|-------|---------------------|----------------------------------|--------------------------------|
| 0xB0  | SD_LIST_FOLDERS     | —                                | `SdFolderListResponse` (516B)  |
| 0xB1  | SD_LIST_FILES       | `SdListFilesPayload` (32B)       | `SdFileListResponse` (676B)    |
| 0xB2  | SD_FILE_INFO        | `SdFileInfoPayload` (68B)        | `SdFileInfoResponse` (16B)     |
| 0xB3  | SD_LOAD_SAMPLE      | `SdLoadSamplePayload` (65B)      | — (emits EVT_SD_SAMPLE_LOADED) |
| 0xB4  | SD_LOAD_KIT         | `SdLoadKitPayload` (36B)         | —                              |
| 0xB5  | SD_KIT_LIST         | —                                | `SdKitListResponse` (516B)     |
| 0xB6  | SD_STATUS           | —                                | `SdStatusResponse` (44B)       |
| 0xB7  | SD_UNLOAD_KIT       | —                                | —                              |
| 0xB8  | SD_GET_LOADED       | —                                | `SdStatusResponse` (44B)       |
| 0xB9  | SD_ABORT            | —                                | —                              |

> **Nota:** Las respuestas grandes (SdFolderListResponse=516B, SdFileListResponse=676B) exceden el TX buffer actual de 76 bytes. Para estas respuestas hay que ampliar `TX_BUF_SIZE` a 768 o usar fragmentación (enviar en múltiples transacciones con un índice de offset).

#### Código Daisy — SD file system (añadir a red808_daisy.cpp)

```cpp
#include "fatfs.h"  // libDaisy FatFS wrapper

// ─── SD Card ─────────────────────────────────────────────────────────────────
SdmmcHandler   sd;
FatFSInterface fsi;
static bool    sdPresent = false;
static char    currentKitName[32] = "";

static bool InitSD() {
    SdmmcHandler::Config sd_config;
    sd_config.Defaults();
    sd_config.speed = SdmmcHandler::Speed::FAST;   // 25 MHz
    sd.Init(sd_config);
    fsi.Init(FatFSInterface::Config::MEDIA_SD);
    FRESULT fr = f_mount(&fsi.GetSDFileSystem(), "/", 1);
    sdPresent = (fr == FR_OK);
    return sdPresent;
}

// En ProcessCommand(), case handlers para SD:
case CMD_SD_KIT_LIST: {
    SdKitListResponse resp = {};
    DIR dir;
    FILINFO fno;
    if (f_opendir(&dir, "/data") == FR_OK) {
        while (f_readdir(&dir, &fno) == FR_OK && fno.fname[0] != 0) {
            if ((fno.fattrib & AM_DIR) && resp.count < 16) {
                // Filtrar familias (2 chars) y xtra
                size_t nameLen = strlen(fno.fname);
                if (nameLen <= 2 || strcmp(fno.fname, "xtra") == 0) continue;
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
        snprintf(path, sizeof(path), "/data/%s", lk.kitName);
        DIR dir; FILINFO fno;
        uint8_t padIdx = lk.startPad;
        if (f_opendir(&dir, path) == FR_OK) {
            while (f_readdir(&dir, &fno) == FR_OK && fno.fname[0] != 0
                   && padIdx < lk.startPad + lk.maxPads) {
                // Solo WAV files
                char* ext = strrchr(fno.fname, '.');
                if (!ext || strcasecmp(ext, ".wav") != 0) continue;
                
                char fpath[96];
                snprintf(fpath, sizeof(fpath), "%s/%s", path, fno.fname);
                FIL fil;
                if (f_open(&fil, fpath, FA_READ) == FR_OK) {
                    // Saltar header WAV (44 bytes)
                    f_lseek(&fil, 44);
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
    // TODO: f_getfree() para totalMB y freeMB
    for (int i = 0; i < MAX_PADS; i++)
        if (sampleLoaded[i]) resp.samplesLoaded |= (1 << i);
    strncpy(resp.currentKit, currentKitName, 31);
    BuildResponse(CMD_SD_STATUS, hdr->sequence, (uint8_t*)&resp, sizeof(resp));
    return;
}
```

---

### 15.11 Per-Track FX Sends + Mixer — Nuevos comandos (0x59-0x65)

> **Nuevo (25/02/2026):** Cada track ahora tiene send levels independientes para reverb, delay y chorus, más pan, mute/solo y EQ de 3 bandas.

#### Diagrama de señal por track

```
Track N audio → [EQ 3-band] → [Track Filter] → [Gate] → [Compressor]
       │                                                       │
       │            ┌─ [Pan L/R] → Master Mix L/R             │
       │            │                                          │
       └── gain ────┤─ [Reverb Send] → Master Reverb Bus ─────┘
                    │─ [Delay Send]  → Master Delay Bus
                    └─ [Chorus Send] → Master Chorus Bus
                          │
                          ▼
                    Master FX Chain → Out
```

#### Comandos nuevos

| CMD    | Nombre                | Payload (2 bytes mín.)          | DaisySP             |
|--------|-----------------------|---------------------------------|---------------------|
| `0x59` | TRACK_REVERB_SEND     | `{track, level 0-100}`          | Gain → reverb bus   |
| `0x5A` | TRACK_DELAY_SEND      | `{track, level 0-100}`          | Gain → delay bus    |
| `0x5B` | TRACK_CHORUS_SEND     | `{track, level 0-100}`          | Gain → chorus bus   |
| `0x5C` | TRACK_PAN             | `{track, pan -100..+100}`       | L/R gain            |
| `0x5D` | TRACK_MUTE            | `{track, 0/1}`                  | Gain = 0            |
| `0x5E` | TRACK_SOLO            | `{track, 0/1}`                  | Solo bus logic      |
| `0x5F` | TRACK_PHASER          | `TrackFlangerPayload` (16B)     | `Phaser`            |
| `0x60` | TRACK_TREMOLO         | `{track, active, rate, depth}`  | `Tremolo`           |
| `0x61` | TRACK_PITCH           | `{track, cents -1200..+1200}`   | `padPitch[]`        |
| `0x62` | TRACK_GATE            | `TrackGatePayload` (16B)        | Noise gate manual   |
| `0x63` | TRACK_EQ_LOW          | `{track, dB -12..+12}`          | `Biquad` lowshelf   |
| `0x64` | TRACK_EQ_MID          | `{track, dB -12..+12}`          | `Biquad` peaking    |
| `0x65` | TRACK_EQ_HIGH         | `{track, dB -12..+12}`          | `Biquad` highshelf  |

---

## 16. MAPA COMPLETO DE COMANDOS ESP32 → DaisySP

> El master tiene **50+ comandos** definidos en `protocol.h`. Este mapa muestra qué módulo de DaisySP implementa cada uno, cuáles ya están en `red808_daisy.cpp` (Section 15.4) y cuáles hay que añadir. Al final: FX que la Daisy puede ofrecer pero que el protocolo aún no cubre.

---

### 16.1 Estado actual de implementación

| 🟢 = en red808_daisy.cpp | 🟡 = falta en Daisy, master lo envía | 🔵 = Daisy lo puede hacer, master no lo pide aún |

---

### 16.2 TRIGGERS (0x01–0x05)

| CMD | Valor | Estado | DaisySP |
|-----|-------|--------|---------|
| CMD_TRIGGER_SEQ     | 0x01 | 🟢 implementado | `Voice::active + pos + speed` en AudioCallback |
| CMD_TRIGGER_LIVE    | 0x02 | 🟢 implementado | idem |
| CMD_TRIGGER_STOP    | 0x03 | 🟢 implementado | `voices[v].active = false` |
| CMD_TRIGGER_STOP_ALL| 0x04 | 🟢 implementado | loop `voices[v].active = false` |
| CMD_TRIGGER_SIDECHAIN| 0x05 | 🟡 falta | ADSR envelope ducking manual |

---

### 16.3 VOLUMEN (0x10–0x14)

| CMD | Valor | Estado | DaisySP |
|-----|-------|--------|---------|
| CMD_MASTER_VOLUME   | 0x10 | 🟢 implementado | `masterGain = v/100.0f` |
| CMD_SEQ_VOLUME      | 0x11 | 🟡 falta | `seqGain` escalar separado del live |
| CMD_LIVE_VOLUME     | 0x12 | 🟡 falta | `liveGain` escalar |
| CMD_TRACK_VOLUME    | 0x13 | 🟢 implementado | `trackGain[pad]` |
| CMD_LIVE_PITCH      | 0x14 | 🟡 falta | `voices[slot].speed = pitch` para triggers LIVE |

**Fix para 0x11, 0x12, 0x14 — añadir al `ProcessCommand()`:**
```cpp
static float seqGain  = 1.0f;
static float liveGain = 1.0f;
static float livePitch = 1.0f;

case CMD_SEQ_VOLUME:
    if (hdr->length >= 1) seqGain = p[0] / 100.0f;  break;
case CMD_LIVE_VOLUME:
    if (hdr->length >= 1) liveGain = p[0] / 100.0f; break;
case CMD_LIVE_PITCH:
    if (hdr->length >= 4) memcpy(&livePitch, p, 4);  break;
```
*En `TriggerPad()`: si el trigger es LIVE, multiplicar `voices[slot].gainL *= liveGain` y `voices[slot].speed = livePitch`.*

---

### 16.4 FILTRO GLOBAL (0x20–0x26) → `Biquad` + `Overdrive` + `Decimator`

| CMD | Valor | Estado | DaisySP módulo |
|-----|-------|--------|----------------|
| CMD_FILTER_SET      | 0x20 | 🟡 falta | `daisysp::Biquad` |
| CMD_FILTER_CUTOFF   | 0x21 | 🟡 falta | `biquad.SetFreq(cutoff)` |
| CMD_FILTER_RESONANCE| 0x22 | 🟡 falta | `biquad.SetRes(q)` |
| CMD_FILTER_BITDEPTH | 0x23 | 🟡 falta | `daisysp::Decimator::SetBitcrushFactor()` |
| CMD_FILTER_DISTORTION| 0x24 | 🟡 falta | `daisysp::Overdrive::SetDrive()` |
| CMD_FILTER_DIST_MODE| 0x25 | 🟡 falta | modo selecciona soft/hard/tube/fuzz en Overdrive |
| CMD_FILTER_SR_REDUCE| 0x26 | 🟡 falta | `daisysp::Decimator::SetDownsampleFactor()` |

**Añadir al archivo Daisy:**
```cpp
#include "daisysp.h"
using namespace daisysp;

// Filtro global master
static Biquad     masterFilter;
static Overdrive  masterDrive;
static Decimator  masterDecimator;
static bool       globalFilterActive = false;

// Inicializar en main():
masterFilter.Init(44100.0f);
masterFilter.SetRes(1.0f);
masterFilter.SetFreq(20000.0f);
masterDrive.Init();
masterDecimator.Init();

// Aplicar en AudioCallback, después del mezclador:
for (size_t i = 0; i < size; i++) {
    float s = out[0][i];
    if (globalFilterActive)  s = masterFilter.Process(s);
    if (globalDrive > 0.01f) s = masterDrive.Process(s);
    if (globalDecimActive)   s = masterDecimator.Process(s);
    out[0][i] = out[1][i] = tanhf(s * masterGain);
}

// CMD_FILTER_SET handler:
case CMD_FILTER_SET: {
    GlobalFilterPayload* gf = (GlobalFilterPayload*)p;
    masterFilter.SetFreq(gf->cutoff);
    masterFilter.SetRes(gf->resonance);
    // SetFilterMode según gf->filterType:
    // 0=off, 1=LP, 2=HP, 3=BP, 4=Notch
    globalFilterActive = (gf->filterType != 0);
    break;
}
```

---

### 16.5 MASTER FX (0x30–0x4F) → `DelayLine` + `Phaser` + `Flanger` + `Compressor` + `ReverbSc` + `Chorus` + `Tremolo` + `Fold` + `Limiter`

| CMD | Valor | Estado | DaisySP módulo |
|-----|-------|--------|----------------|
| CMD_DELAY_ACTIVE    | 0x30 | 🟡 falta | `daisysp::DelayLine<float, MAX_DELAY>` |
| CMD_DELAY_TIME      | 0x31 | 🟡 falta | `delay.SetDelay(time * 0.001f * SR)` |
| CMD_DELAY_FEEDBACK  | 0x32 | 🟡 falta | escalar en el bucle de feedback |
| CMD_DELAY_MIX       | 0x33 | 🟡 falta | wet/dry blend |
| CMD_PHASER_ACTIVE   | 0x34 | 🟡 falta | `daisysp::Phaser` |
| CMD_PHASER_RATE     | 0x35 | 🟡 falta | `phaser.SetLfoFreq(hz)` |
| CMD_PHASER_DEPTH    | 0x36 | 🟡 falta | `phaser.SetLfoDepth(d)` |
| CMD_PHASER_FEEDBACK | 0x37 | 🟡 falta | `phaser.SetFeedback(f)` |
| CMD_FLANGER_ACTIVE  | 0x38 | 🟡 falta | `DelayLine` + `Oscillator` (no hay clase Flanger, se construye) |
| CMD_FLANGER_RATE    | 0x39 | 🟡 falta | LFO rate |
| CMD_FLANGER_DEPTH   | 0x3A | 🟡 falta | LFO depth |
| CMD_FLANGER_FEEDBACK| 0x3B | 🟡 falta | feedback escalar |
| CMD_FLANGER_MIX     | 0x3C | 🟡 falta | wet/dry |
| CMD_COMP_ACTIVE     | 0x3D | 🟡 falta | `daisysp::Compressor` |
| CMD_COMP_THRESHOLD  | 0x3E | 🟡 falta | `comp.SetThreshold(dB)` |
| CMD_COMP_RATIO      | 0x3F | 🟡 falta | `comp.SetRatio(r)` |
| CMD_COMP_ATTACK     | 0x40 | 🟡 falta | `comp.SetAttack(ms)` |
| CMD_COMP_RELEASE    | 0x41 | 🟡 falta | `comp.SetRelease(ms)` |
| CMD_COMP_MAKEUP     | 0x42 | 🟡 falta | `comp.SetMakeupGain(dB)` |
| **CMD_REVERB_ACTIVE**   | **0x43** | 🟡 falta | `daisysp::ReverbSc` — on/off |
| **CMD_REVERB_FEEDBACK** | **0x44** | 🟡 falta | `reverb.SetFeedback(f)` — room/decay 0.0-1.0 |
| **CMD_REVERB_LPFREQ**   | **0x45** | 🟡 falta | `reverb.SetLpFreq(hz)` — color 200-12000 Hz |
| **CMD_REVERB_MIX**      | **0x46** | 🟡 falta | dry/wet 0.0-1.0 |
| **CMD_CHORUS_ACTIVE**   | **0x47** | 🟡 falta | `daisysp::Chorus` — on/off |
| **CMD_CHORUS_RATE**     | **0x48** | 🟡 falta | `chorus.SetLfoFreq(hz)` 0.1-10.0 Hz |
| **CMD_CHORUS_DEPTH**    | **0x49** | 🟡 falta | `chorus.SetLfoDepth(d)` 0.0-1.0 |
| **CMD_CHORUS_MIX**      | **0x4A** | 🟡 falta | dry/wet 0.0-1.0 |
| **CMD_TREMOLO_ACTIVE**  | **0x4B** | 🟡 falta | `daisysp::Tremolo` — on/off |
| **CMD_TREMOLO_RATE**    | **0x4C** | 🟡 falta | `tremolo.SetFreq(hz)` 0.1-20.0 Hz |
| **CMD_TREMOLO_DEPTH**   | **0x4D** | 🟡 falta | `tremolo.SetDepth(d)` 0.0-1.0 |
| **CMD_WAVEFOLDER_GAIN** | **0x4E** | 🟡 falta | `daisysp::Fold::SetGain(g)` 1.0-10.0 |
| **CMD_LIMITER_ACTIVE**  | **0x4F** | 🟡 falta | `daisysp::Limiter` — brick-wall 0dBFS |

**Esqueleto:**
```cpp
#define MAX_DELAY_SAMPLES  88200  // 2s a 44100
static DelayLine<float, MAX_DELAY_SAMPLES> masterDelay DSY_SDRAM_BSS;
static Phaser      masterPhaser;
static Compressor  masterComp;
static bool        delayActive = false, phaserActive = false, compActive = false;
static float       delayFeedback = 0.3f, delayMix = 0.3f;
static float       delaySend = 0.0f;  // último output del delay

// initDelayLine dentro de main():
masterDelay.Init();
masterPhaser.Init(44100.0f);
masterComp.Init(44100.0f);

// En AudioCallback (post-mezcla, antes del tanhf):
for (size_t i = 0; i < size; i++) {
    float dry = out[0][i];
    float wet = 0.0f;
    if (delayActive) {
        wet = masterDelay.Read();
        masterDelay.Write(dry + wet * delayFeedback);
    }
    float s = dry + wet * delayMix;
    if (phaserActive) s = masterPhaser.Process(s);
    if (compActive)   s = masterComp.Process(s);
    out[0][i] = out[1][i] = tanhf(s * masterGain);
}
```

---

### 16.6 PER-TRACK FX (0x50–0x65) → Biquad/Overdrive/Decimator/DelayLine + Sends + Mixer + EQ por track

| CMD | Valor | Estado | DaisySP módulo |
|-----|-------|--------|----------------|
| CMD_TRACK_FILTER    | 0x50 | 🟡 falta | `Biquad trackFilter[16]` |
| CMD_TRACK_CLEAR_FILTER | 0x51 | 🟡 falta | reset params biquad |
| CMD_TRACK_DISTORTION| 0x52 | 🟡 falta | `Overdrive trackDrive[16]` |
| CMD_TRACK_BITCRUSH  | 0x53 | 🟡 falta | `Decimator trackDecim[16]` |
| CMD_TRACK_ECHO      | 0x54 | 🟡 falta | `DelayLine<float,8820> trackDelay[16]` en SDRAM |
| CMD_TRACK_FLANGER_FX| 0x55 | 🟡 falta | `DelayLine` + `Oscillator` × 16 |
| CMD_TRACK_COMPRESSOR| 0x56 | 🟡 falta | `Compressor trackComp[16]` |
| CMD_TRACK_CLEAR_LIVE| 0x57 | 🟡 falta | reset flags por track |
| CMD_TRACK_CLEAR_FX  | 0x58 | 🟡 falta | reset todo por track |
| **CMD_TRACK_REVERB_SEND** | **0x59** | **🟡 nuevo** | `trackReverbSend[16]` → reverb bus |
| **CMD_TRACK_DELAY_SEND**  | **0x5A** | **🟡 nuevo** | `trackDelaySend[16]` → delay bus |
| **CMD_TRACK_CHORUS_SEND** | **0x5B** | **🟡 nuevo** | `trackChorusSend[16]` → chorus bus |
| **CMD_TRACK_PAN**         | **0x5C** | **🟡 nuevo** | `trackPan[16]` → L/R gain |
| **CMD_TRACK_MUTE**        | **0x5D** | **🟡 nuevo** | `trackMute[16]` → gain=0 |
| **CMD_TRACK_SOLO**        | **0x5E** | **🟡 nuevo** | `trackSolo[16]` → solo bus logic |
| **CMD_TRACK_PHASER**      | **0x5F** | **🟡 nuevo** | `Phaser trackPhaser[16]` |
| **CMD_TRACK_TREMOLO**     | **0x60** | **🟡 nuevo** | `Tremolo trackTrem[16]` |
| **CMD_TRACK_PITCH**       | **0x61** | **🟡 nuevo** | `padPitch[pad] = powf(2, cents/1200)` |
| **CMD_TRACK_GATE**        | **0x62** | **🟡 nuevo** | Noise gate (envelope follower) |
| **CMD_TRACK_EQ_LOW**      | **0x63** | **🟡 nuevo** | `Biquad trackEqLow[16]` lowshelf |
| **CMD_TRACK_EQ_MID**      | **0x64** | **🟡 nuevo** | `Biquad trackEqMid[16]` peaking |
| **CMD_TRACK_EQ_HIGH**     | **0x65** | **🟡 nuevo** | `Biquad trackEqHi[16]` highshelf |

> ⚠ 16 Biquad + 16 Decimator + 16 Compressor + 3×16 EQ Biquad = perfectamente factible en STM32H750 @ 480MHz. Para los 16 DelayLine de 8820 muestras (0.2s) = 16 × 8820 × 4 bytes = **565 KB en SDRAM** (`DSY_SDRAM_BSS`). La Daisy tiene 64MB, sin problema.

**FX Sends — Implementación en AudioCallback:**
```cpp
// Variables globales adicionales:
static float trackReverbSend[MAX_PADS] = {0};   // 0.0-1.0
static float trackDelaySend[MAX_PADS]  = {0};   // 0.0-1.0
static float trackChorusSend[MAX_PADS] = {0};   // 0.0-1.0
static float trackPanF[MAX_PADS];               // -1.0..+1.0 (init 0.0 = center)
static bool  trackMute[MAX_PADS]  = {false};
static bool  trackSolo[MAX_PADS]  = {false};

// En el AudioCallback, después de Voice mixing y antes de Master FX:
float reverbBusL = 0.0f, delayBusL = 0.0f, chorusBusL = 0.0f;
// ... acumular por voz:
//   reverbBusL += fout * trackReverbSend[vx.pad];
//   delayBusL  += fout * trackDelaySend[vx.pad];
//   chorusBusL += fout * trackChorusSend[vx.pad];
// Aplicar pan:
//   float panL = (1.0f - trackPanF[vx.pad]) * 0.5f;
//   float panR = (1.0f + trackPanF[vx.pad]) * 0.5f;
//   out[0][i] += fout * panL;
//   out[1][i] += fout * panR;
```

---

### 16.7 PER-PAD FX (0x70–0x7A)

| CMD | Valor | Estado | DaisySP / manual |
|-----|-------|--------|-----------------|
| CMD_PAD_FILTER      | 0x70 | 🟡 falta | `Biquad padFilter[24]` |
| CMD_PAD_CLEAR_FILTER| 0x71 | 🟡 falta | reset |
| CMD_PAD_DISTORTION  | 0x72 | 🟡 falta | `Overdrive padDrive[24]` |
| CMD_PAD_BITCRUSH    | 0x73 | 🟡 falta | `Decimator padDecim[24]` |
| CMD_PAD_LOOP        | 0x74 | � implementado | `padLoop[pad]` en AudioCallback — loop automático |
| CMD_PAD_REVERSE     | 0x75 | 🟢 implementado | `padReverse[pad]` — dirección negativa con bounds check |
| CMD_PAD_PITCH       | 0x76 | 🟢 implementado | `padPitch[pad] = powf(2, cents/1200)` — cents ±1200 |
| CMD_PAD_STUTTER     | 0x77 | 🟡 falta | timer que resetea `pos = 0` cada `intervalMs` |
| CMD_PAD_SCRATCH     | 0x78 | 🟡 falta | LFO sinusoidal sobre `speed`, + Decimator para crackle |
| CMD_PAD_TURNTABLISM | 0x79 | 🟡 falta | máquina de estados: PLAY→BRAKE→SPIN→SPINUP |
| CMD_PAD_CLEAR_FX    | 0x7A | 🟡 falta | reset todo por pad |

**Loop, Reverse y Pitch — ya implementados en Section 15.4 `red808_daisy.cpp`:**
- `padLoop[]`, `padReverse[]`, `padPitch[]` declarados como globales
- AudioCallback usa `padReverse` para dirección negativa y bounds check con loop
- `padPitch` se multiplica con `vx.speed` para ajustar velocidad de playback
- ProcessCommand() tiene case handlers para 0x74, 0x75, 0x76
- `main()` inicializa `padPitch[i] = 1.0f`

---

### 16.8 SIDECHAIN (0x90–0x91) → ADSR manual

| CMD | Valor | Estado | DaisySP módulo |
|-----|-------|--------|----------------|
| CMD_SIDECHAIN_SET   | 0x90 | 🟡 falta | `daisysp::Adsr` + envelope follower |
| CMD_SIDECHAIN_CLEAR | 0x91 | 🟡 falta | desactivar |

```cpp
// Ducking clásico kick→bass:
// Cuando llega CMD_TRIGGER_SEQ para sourceTrack → lanza ADSR
// El ADSR reduce el gain de destTracks durante Attack+Decay, luego Release
static Adsr sidechainEnv;
static bool sidechainActive = false;
static uint8_t scSource = 0;
static uint16_t scDestMask = 0;
static float scAmount = 0.8f;
// En AudioCallback, por muestra:
float scGain = 1.0f - (sidechainActive ? sidechainEnv.Process(scGate) * scAmount : 0.0f);
// Aplicar scGain a las voces cuyo pad esté en scDestMask
```

---

### 16.9 SAMPLE CONTROL (0xA3–0xA4) — fáciles

| CMD | Valor | Estado | |
|-----|-------|--------|-|
| CMD_SAMPLE_UNLOAD     | 0xA3 | � implementado | `sampleLoaded[pad] = false; sampleLength[pad] = 0;` |
| CMD_SAMPLE_UNLOAD_ALL | 0xA4 | 🟡 falta | loop sobre todos los pads |

#### Nuevos — Daisy SD Card (0xB0–0xB9)

| CMD | Valor | Estado | Descripción |
|-----|-------|--------|-------------|
| CMD_SD_LIST_FOLDERS  | 0xB0 | � implementado | Listar carpetas en `/data/` |
| CMD_SD_LIST_FILES    | 0xB1 | 🟢 implementado | Listar WAVs en una carpeta |
| CMD_SD_FILE_INFO     | 0xB2 | 🟢 implementado | Info de un WAV (SR, bits, duración) |
| CMD_SD_LOAD_SAMPLE   | 0xB3 | 🟢 implementado | Cargar WAV → pad slot en SDRAM |
| CMD_SD_LOAD_KIT      | 0xB4 | 🟢 implementado | Cargar kit completo SD → SDRAM |
| CMD_SD_KIT_LIST      | 0xB5 | 🟢 implementado | Lista nombres de kits (filtra familias) |
| CMD_SD_STATUS        | 0xB6 | 🟢 implementado | Estado SD (presente, espacio, kit cargado) |
| CMD_SD_UNLOAD_KIT    | 0xB7 | 🟡 nuevo | Descargar kit de SDRAM |
| CMD_SD_GET_LOADED    | 0xB8 | 🟡 nuevo | Qué kit está cargado ahora |
| CMD_SD_ABORT         | 0xB9 | 🟡 nuevo | Cancelar carga en progreso |

> Ver **Section 15.10** para flujo operativo, diagrama de señal y código de ejemplo.

---

### 16.10 STATUS QUERIES (0xE0–0xE4) — actualizado con eventos

| CMD | Valor | Estado | |
|-----|-------|--------|-|
| CMD_GET_STATUS  | 0xE0 | 🟢 implementado | `StatusResponse` V2 — 54 bytes (voces, CPU%, kit, SD, events) |
| CMD_GET_PEAKS   | 0xE1 | 🟢 implementado | trackPeaks[16] + masterPeak |
| CMD_GET_CPU_LOAD| 0xE2 | 🟢 implementado | `seed.system.GetCpuLoad()` |
| CMD_GET_VOICES  | 0xE3 | 🟢 implementado | contar `voices[v].active` |
| CMD_PING        | 0xEE | 🟢 implementado | echo timestamp + uptime |
| CMD_RESET       | 0xEF | 🟢 implementado | limpiar voces y samples |

---

### 16.11 BULK (0xF0–0xF1) — optimización futura

| CMD | Valor | Estado | |
|-----|-------|--------|-|
| CMD_BULK_TRIGGERS | 0xF0 | 🟡 falta | leer `BulkTriggersPayload.count` y llamar `TriggerPad()` en loop |
| CMD_BULK_FX       | 0xF1 | 🟡 falta | procesar múltiples cambios de FX en una transacción SPI |

---

### 16.12 FX adicionales que la Daisy podría ofrecer (sin CMDs definidos aún)

> Con la actualización del 25/02/2026, **Reverb, Chorus, Tremolo, WaveFolder y Limiter ya tienen CMDs** (0x43–0x4F) en `protocol.h` y el master los envía desde `SPIMaster.cpp`. Solo falta implementarlos en la Daisy (ver Sección 16.5).

Estos módulos DaisySP adicionales NO tienen CMDs definidos todavía. Si quieres usarlos, asignar en el rango `0x59–0x6F` (per-track libres) o `0xB0+` (nuevos).

| FX DaisySP | Módulo | Rango CMD sugerido | Descripción |
|-----------|--------|--------------------|-------------|
| **Resonator** | `StringVoice` | 0x59–0x5B | Karplus-Strong para samples sintéticos |
| **Looper** | `Looper` | 0xB0–0xB3 | Grabación y reproducción del audio master en tiempo real |
| **Bitcrush estéreo** | `SampleRateReducer` | reutilizar 0x26 | Reducción SR independiente L/R |

---

### 16.13 Resumen de prioridades de implementación en Daisy

| Prioridad | Grupo | CMDs | Impacto audible |
|-----------|-------|------|-----------------|
| **1 — CRÍTICO** | Ya en red808_daisy.cpp | Triggers, volume, samples, ping, peaks, reset | ✅ funciona |
| **2 — ALTO** | Reverb | 0x43–0x46 (master ya envía) | ⭐⭐⭐⭐⭐ |
| **3 — ALTO** | Pad control | Loop (0x74), Reverse (0x75), Pitch (0x76) | ⭐⭐⭐⭐ |
| **4 — ALTO** | Master FX legacy | Delay (0x30-0x33), Compressor (0x3D-0x42) | ⭐⭐⭐⭐ |
| **5 — ALTO** | Global filter | Filter (0x20-0x22), BitCrush (0x23) | ⭐⭐⭐ |
| **6 — MEDIO** | Chorus + Tremolo | 0x47–0x4D (master ya envía) | ⭐⭐⭐ |
| **7 — MEDIO** | Per-track FX + Sends | Filter (0x50), Echo (0x54), Comp (0x56), **Sends (0x59-0x5B)**, **EQ (0x63-0x65)** | ⭐⭐⭐ |
| **8 — MEDIO** | Sidechain | 0x90-0x91, 0x05 trigger | ⭐⭐⭐ |
| **9 — BAJO** | WaveFolder + Limiter | 0x4E–0x4F (master ya envía) | ⭐⭐ |
| **10 — STATUS** | Status queries | 0xE0, 0xE2, 0xE3, bulk 0xF0-0xF1 | ⭐ |
| **11 — NUEVO** | **SD Card kits** | **0xB0-0xB9 (master ya envía)** | ⭐⭐⭐⭐ |
| **12 — NUEVO** | **Track Pan/Mute/Solo** | **0x5C-0x5E (master ya envía)** | ⭐⭐⭐ |

> **Bottom line (actualizado 25/02/2026):** El master ESP32 ahora envía **70+ comandos** incluyendo:
> - FX master: Reverb, Chorus, Tremolo, WaveFolder, Limiter (0x43–0x4F)
> - **Per-track sends**: Reverb/Delay/Chorus send, Pan, Mute, Solo, EQ 3-band (0x59–0x65) ← NUEVO
> - **Daisy SD Card**: Listar kits, cargar WAVs directo SD→SDRAM (0xB0–0xB9) ← NUEVO
> - Per-pad: Loop, Reverse, Pitch (0x74-0x76) ya implementados en Daisy
> 
> La Daisy puede cargar kits **~50x más rápido** desde su propia SD que via SPI transfer. El ESP32 explora las carpetas remotamente y comanda la carga.