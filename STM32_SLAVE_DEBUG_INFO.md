# RED808 — Información para el Equipo STM32 (SPI Audio Slave)

## Estado actual
El **ESP32-S3** (Master) ya está compilado, subido y funcionando correctamente.
Envía comandos SPI pero **no recibe respuestas** del STM32 → no hay sonido.

Este documento contiene todo lo necesario para implementar y depurar el firmware Slave en **STM32F411CE (BlackPill)**.

---

## 1. CONEXIONES FÍSICAS

```
ESP32-S3          STM32F411CE (BlackPill)
─────────         ──────────────────────
GPIO 10  (CS)  →  PA4  (NSS / SPI1_NSS)
GPIO 11  (MOSI)→  PA7  (SPI1_MOSI)
GPIO 12  (SCK) →  PA5  (SPI1_SCK)
GPIO 13  (MISO)←  PA6  (SPI1_MISO)
GND      ──────── GND

⚠️ Conectar GND entre ambas placas es OBLIGATORIO.
⚠️ Ambas placas operan a 3.3V → no necesitan level shifter.
```

### Pines opcionales (NO activos por ahora)
```
GPIO 9   (SYNC) →  PB0  (opcional, sincronización)
GPIO 14  (IRQ)  ←  PB1  (opcional, interrupción STM32→ESP32)
```
Estos pines están deshabilitados en el firmware ESP32 (`#define USE_SPI_SYNC_IRQ` está comentado).
**No conectar por ahora.**

---

## 2. CONFIGURACIÓN SPI

| Parámetro       | Valor                                  |
|-----------------|----------------------------------------|
| **Modo SPI**    | Mode 0 (CPOL=0, CPHA=0)               |
| **Velocidad**   | 20 MHz                                 |
| **Bit Order**   | MSB First                              |
| **Data Size**   | 8 bits                                 |
| **CS (NSS)**    | Hardware, activo LOW                   |
| **Bus ESP32**   | HSPI (SPI2)                            |
| **Bus STM32**   | SPI1                                   |

### Configuración STM32 SPI1 (HAL / CubeMX):
```c
// SPI1 en modo SLAVE
hspi1.Instance               = SPI1;
hspi1.Init.Mode              = SPI_MODE_SLAVE;
hspi1.Init.Direction         = SPI_DIRECTION_2LINES;  // Full duplex
hspi1.Init.DataSize          = SPI_DATASIZE_8BIT;
hspi1.Init.CLKPolarity       = SPI_POLARITY_LOW;      // CPOL=0
hspi1.Init.CLKPhase          = SPI_PHASE_1EDGE;       // CPHA=0
hspi1.Init.NSS               = SPI_NSS_HARD_INPUT;    // PA4 como NSS
hspi1.Init.FirstBit          = SPI_FIRSTBIT_MSB;
hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_2; // Ignorado en slave
```

### Recomendación: Usar DMA para SPI
```c
// Recibir con DMA para no perder bytes a 20MHz
HAL_SPI_Receive_DMA(&hspi1, rxBuffer, SPI_MAX_PACKET_SIZE);
```

---

## 3. FORMATO DE PAQUETE SPI

### Header (8 bytes, packed, little-endian)

```
Byte   Campo        Tamaño   Descripción
───────────────────────────────────────────────────
 0     magic        1 byte   0xA5 = comando del Master
 1     cmd          1 byte   Código del comando (ver tabla)
 2-3   length       2 bytes  Longitud del payload (little-endian)
 4-5   sequence     2 bytes  Número de secuencia (little-endian)
 6-7   checksum     2 bytes  CRC-16/MODBUS del payload (little-endian)
```

### Definición en C:
```c
typedef struct __attribute__((packed)) {
    uint8_t  magic;       // 0xA5 = comando, 0x5A = respuesta
    uint8_t  cmd;         // Código del comando
    uint16_t length;      // Longitud del payload en bytes
    uint16_t sequence;    // Número de secuencia
    uint16_t checksum;    // CRC-16 del payload
} SPIPacketHeader;
```

### Estructura completa del paquete:
```
[Header: 8 bytes] [Payload: 0-520 bytes]
```

### Valores mágicos:
| Magic | Dirección               |
|-------|--------------------------|
| 0xA5  | ESP32 → STM32 (comando) |
| 0x5A  | STM32 → ESP32 (respuesta)|
| 0xDA  | Sample data transfer     |
| 0xBB  | Bulk multi-command       |

---

## 4. CRC-16/MODBUS

El checksum se calcula SOLO sobre el payload (NO sobre el header).
Si `length == 0`, el checksum es `0x0000`.

### Implementación:
```c
uint16_t crc16_modbus(const uint8_t* data, uint16_t len) {
    uint16_t crc = 0xFFFF;
    for (uint16_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 1)
                crc = (crc >> 1) ^ 0xA001;
            else
                crc >>= 1;
        }
    }
    return crc;
}
```

### Verificación del CRC:
```c
// Al recibir un paquete:
uint16_t calculado = crc16_modbus(payload, header.length);
if (calculado != header.checksum) {
    // ERROR: paquete corrupto, descartar
}
```

---

## 5. RESPUESTA DEL SLAVE

Cuando el ESP32 **envía un comando**, inmediatamente después lee la respuesta del STM32. El STM32 debe tener el buffer de respuesta **preparado antes o durante** la recepción del comando.

### Para comandos simples (sin respuesta de datos):
El STM32 debe colocar en su TX buffer al menos 8 bytes:
```c
SPIPacketHeader resp;
resp.magic    = 0x5A;           // Respuesta
resp.cmd      = received_cmd;   // El mismo cmd que recibió
resp.length   = 0;              // Sin payload extra
resp.sequence = received_seq;   // Mismo sequence del comando
resp.checksum = 0;              // Sin payload = sin CRC
// Cargar resp en el SPI TX (DMA)
```

### Para PING (cmd 0xEE):
```c
// El ESP32 envía:
//   Header: A5 EE 04 00 xx xx yy yy
//   Payload: [4 bytes timestamp uint32_t]

// El STM32 debe responder:
//   Header: 5A EE 08 00 xx xx zz zz
//   Payload: [4 bytes echoTimestamp] [4 bytes stm32Uptime]

typedef struct __attribute__((packed)) {
    uint32_t echoTimestamp;  // Devolver el mismo timestamp que envió ESP32
    uint32_t stm32Uptime;   // HAL_GetTick() del STM32
} PongResponse;
```

### Para GET_PEAKS (cmd 0xE1):
```c
// El ESP32 solicita peaks (VU meters) cada ~50ms
// El STM32 debe responder con:

typedef struct __attribute__((packed)) {
    float trackPeaks[16];   // Peak 0.0-1.0 por cada track
    float masterPeak;       // Peak 0.0-1.0 master
} PeaksResponse;
// Total: 68 bytes de payload
```

---

## 6. TABLA DE COMANDOS PRINCIPALES

### Trigger / Reproducción (los más importantes)
| Cmd  | Nombre            | Payload                                         |
|------|-------------------|-------------------------------------------------|
| 0x01 | TRIGGER_SEQ       | `{u8 pad, u8 velocity, u8 trackVol, u8 res, u32 maxSamples}` = 8 bytes |
| 0x02 | TRIGGER_LIVE      | `{u8 pad, u8 velocity}` = 2 bytes               |
| 0x03 | TRIGGER_STOP      | `{u8 pad}` = 1 byte                             |
| 0x04 | TRIGGER_STOP_ALL  | Sin payload (length=0)                           |

### Volumen
| Cmd  | Nombre           | Payload                                   |
|------|------------------|-------------------------------------------|
| 0x10 | MASTER_VOLUME    | `{u8 volume}` = 1 byte (0-100)            |
| 0x11 | SEQ_VOLUME       | `{u8 volume}` = 1 byte (0-150)            |
| 0x12 | LIVE_VOLUME      | `{u8 volume}` = 1 byte (0-180)            |
| 0x13 | TRACK_VOLUME     | `{u8 track, u8 volume}` = 2 bytes         |
| 0x14 | LIVE_PITCH       | `{float pitch}` = 4 bytes                 |

### Control Global
| Cmd  | Nombre    | Payload                                            |
|------|-----------|----------------------------------------------------|
| 0xEE | PING      | `{u32 timestamp}` = 4 bytes                        |
| 0xEF | RESET     | Sin payload                                         |
| 0xE1 | GET_PEAKS | Sin payload → espera PeaksResponse (68 bytes)       |
| 0xE0 | GET_STATUS| Sin payload → espera StatusResponse                  |

### Sample Transfer
| Cmd  | Nombre       | Payload                                          |
|------|--------------|--------------------------------------------------|
| 0xA0 | SAMPLE_BEGIN | `{u8 pad, u8 bits=16, u16 rate=44100, u32 totalBytes, u32 totalSamples}` = 12 bytes |
| 0xA1 | SAMPLE_DATA  | `{u8 pad, u8 res, u16 chunkSize, u32 offset, int16_t data[256]}` = 8 + max 512 bytes |
| 0xA2 | SAMPLE_END   | `{u8 pad, u8 status, u16 res, u32 crc32}` = 8 bytes |
| 0xA3 | SAMPLE_UNLOAD| `{u8 pad}` = 1 byte                              |

---

## 7. PROTOCOLO DE TRANSFERENCIA DE SAMPLES

Al arrancar, el ESP32 transfiere los 16+8 samples (24 pads) por SPI.
Cada sample se transfiere así:

```
1. SAMPLE_BEGIN (cmd 0xA0)
   → pad=0, bits=16, rate=44100, totalBytes=XXXX, totalSamples=XXXX

2. SAMPLE_DATA (cmd 0xA1) × N chunks
   → pad=0, chunkSize=512(o menos), offset=0,512,1024...
   → Datos PCM: int16_t signed, 16-bit, mono, 44.1kHz
   → Máximo 512 bytes (256 muestras) por chunk

3. SAMPLE_END (cmd 0xA2)
   → pad=0, status=0(OK), crc32=XXXX
```

### Almacenamiento en STM32:
```c
// Cada sample puede ocupar hasta ~200KB
// Para 24 pads necesitan bastante RAM
// Opciones:
// a) RAM interna STM32F411 (128KB total) → solo caben ~2-3 samples
// b) SRAM externa SPI (ej. 23LC1024, 128KB) → múltiples chips
// c) PSRAM SPI (ej. ESP-PSRAM64H, 8MB) → ideal
// d) SD Card con buffer en RAM → workable pero más lento

// Buffer de recepción:
int16_t* sampleBuffers[24];     // Punteros a cada sample
uint32_t sampleLengths[24];     // Longitud en int16_t samples
uint32_t sampleBytesReceived[24]; // Para tracking de transfer
```

---

## 8. IMPLEMENTACIÓN MÍNIMA VIABLE (MVP)

Para que suene lo más rápido posible, implementar solo esto:

### Paso 1: Responder al PING
```c
// En el handler SPI:
if (header.magic == 0xA5 && header.cmd == 0xEE) {
    // Preparar PONG
    PongResponse pong;
    pong.echoTimestamp = *(uint32_t*)payload; // Echo back
    pong.stm32Uptime = HAL_GetTick();
    
    // Enviar respuesta
    SPIPacketHeader resp = {0x5A, 0xEE, 8, header.sequence, 
                            crc16_modbus((uint8_t*)&pong, 8)};
    // Load resp + pong into SPI TX DMA buffer
}
```
**Verificación**: El Serial del ESP32 mostrará `[SPI] STM32 connected! RTT: XXX us`

### Paso 2: Recibir samples
```c
if (header.cmd == 0xA0) { // SAMPLE_BEGIN
    SampleBeginPayload* begin = (SampleBeginPayload*)payload;
    uint8_t pad = begin->padIndex;  // 0-23
    uint32_t bytes = begin->totalBytes;
    // Allocar buffer para este sample
    sampleBuffers[pad] = (int16_t*)malloc(bytes);
    sampleLengths[pad] = begin->totalSamples;
    sampleBytesReceived[pad] = 0;
}

if (header.cmd == 0xA1) { // SAMPLE_DATA
    SampleDataHeader* chunk = (SampleDataHeader*)payload;
    uint8_t pad = chunk->padIndex;
    uint16_t chunkBytes = chunk->chunkSize;
    uint32_t offset = chunk->offset;
    // Copiar datos PCM al buffer
    memcpy((uint8_t*)sampleBuffers[pad] + offset, 
           payload + 8,  // Datos empiezan después del header de 8 bytes
           chunkBytes);
    sampleBytesReceived[pad] += chunkBytes;
}

if (header.cmd == 0xA2) { // SAMPLE_END
    SampleEndPayload* end = (SampleEndPayload*)payload;
    // Verificar CRC32 si se desea
    // Marcar sample como listo
}
```

### Paso 3: Trigger + I2S output
```c
if (header.cmd == 0x01) { // TRIGGER_SEQ
    uint8_t pad = payload[0];
    uint8_t velocity = payload[1];
    uint8_t trackVol = payload[2];
    // Iniciar reproducción del sample en el mixer
    startVoice(pad, velocity, trackVol);
}

// I2S output (44100Hz, 16-bit, stereo):
// Configurar I2S con DMA circular
// En el callback DMA half/complete:
void mixAudio(int16_t* buffer, uint16_t frames) {
    memset(buffer, 0, frames * 4); // Stereo = 2 bytes × 2 canales
    for (int v = 0; v < activeVoices; v++) {
        for (int i = 0; i < frames; i++) {
            int32_t sample = voices[v].sampleData[voices[v].position++];
            sample = (sample * voices[v].velocity) / 127;
            buffer[i*2]   += sample; // Left
            buffer[i*2+1] += sample; // Right (mono → stereo)
        }
    }
}
```

---

## 9. CONEXIÓN I2S (STM32 → DAC PCM5102A)

```
STM32F411CE         PCM5102A
────────────        ────────
PB12 (I2S2_WS)  →  LCK
PB13 (I2S2_CK)  →  BCK
PB15 (I2S2_SD)  →  DIN
GND              →  GND, SCK(a GND)
3.3V             →  VCC, XMT(a 3.3V)
```

### Configuración I2S STM32:
```c
hi2s2.Instance          = SPI2;  // I2S2 usa SPI2
hi2s2.Init.Mode         = I2S_MODE_MASTER_TX;
hi2s2.Init.Standard     = I2S_STANDARD_PHILIPS;
hi2s2.Init.DataFormat   = I2S_DATAFORMAT_16B;
hi2s2.Init.MCLKOutput   = I2S_MCLKOUTPUT_DISABLE; // PCM5102A genera MCLK interno
hi2s2.Init.AudioFreq    = I2S_AUDIOFREQ_44K;
hi2s2.Init.CPOL         = I2S_CPOL_LOW;
```

---

## 10. DATOS REALES DEL SERIAL LOG (ESP32)

Esto es lo que el ESP32 está enviando ahora mismo:

### Boot sequence:
```
[SPI] Master initialized on HSPI (4-wire mode)
[SPI] Pins: MOSI=11 MISO=13 SCK=12 CS=10
[SPI TX] #000 PING      cmd=0xEE len=4 crc=0x5F11
[SPI TX] #001 PING      cmd=0xEE len=4 crc=0xB083
[SPI TX] #002 PING      cmd=0xEE len=4 crc=0xEBF9
[SPI TX] #003 PING      cmd=0xEE len=4 crc=0x0D6B
[SPI TX] #004 PING      cmd=0xEE len=4 crc=0x6B54
[SPI] WARNING: STM32 not responding - will retry in background
```

### Sample transfer (16 samples):
```
[SPI TX] #005 SMPL_BEG  cmd=0xA0 len=12 crc=0xXXXX   ← BEGIN pad 0
[SPI TX] #006 SMPL_DAT  cmd=0xA1 len=520 ...          ← DATA chunks
...
[SPI TX] #NNN SMPL_END  cmd=0xA2 len=8 crc=0xXXXX     ← END pad 0
```
Esto se repite para los 16 pads del sequencer (0-15).

### Cuando el usuario presiona PLAY:
```
[SPI TX] #XXX TRIG_SEQ  cmd=0x01 len=8 crc=0xXXXX
         data: 00 7F 0A 00 XX XX XX XX
         ↑pad  ↑vel ↑vol      ↑maxSamples
```

---

## 11. DIAGRAMA DE FLUJO: RECEPCIÓN SPI SLAVE

```
             ┌─────────────────┐
             │  CS goes LOW    │
             │  (NSS interrupt)│
             └────────┬────────┘
                      │
             ┌────────▼────────┐
             │ Receive 8 bytes │
             │ (SPI Header)    │
             └────────┬────────┘
                      │
             ┌────────▼────────┐
             │ Check magic     │
             │ == 0xA5 ?       │──── NO → descarta
             └────────┬────────┘
                      │ YES
             ┌────────▼────────┐
             │ Read .length    │
             │ bytes payload   │
             └────────┬────────┘
                      │
             ┌────────▼────────┐
             │ Verify CRC-16   │
             │ of payload      │──── FAIL → descarta
             └────────┬────────┘
                      │ OK
             ┌────────▼────────┐
             │ Process command │
             │ (switch on .cmd)│
             └────────┬────────┘
                      │
             ┌────────▼────────┐
             │ Load response   │
             │ into TX buffer  │
             │ (magic=0x5A)    │
             └─────────────────┘
```

---

## 12. CHECKLIST DE DEPURACIÓN

### ¿No conecta? (PING falla)
- [ ] Verificar GND conectado entre ambas placas
- [ ] Verificar cables MOSI→PA7, SCK→PA5, MISO→PA6, CS→PA4
- [ ] Verificar SPI Mode 0 (CPOL=0, CPHA=0) en ambos lados
- [ ] Verificar NSS como Hardware Input en STM32
- [ ] Verificar que SPI1 está habilitado en el reloj (RCC)
- [ ] Probar con osciloscopio/analizador lógico:
  - SCK: ¿hay señal de 20MHz?
  - CS: ¿baja a LOW durante la transmisión?
  - MOSI: ¿se ve el byte 0xA5 al inicio?
- [ ] Probar con un programa mínimo que solo haga echo SPI

### ¿Conecta pero no suena?
- [ ] ¿Se reciben los SAMPLE_DATA chunks?
- [ ] ¿Se almacenan correctamente los PCM samples?
- [ ] ¿Se recibe el TRIGGER_SEQ (0x01)?
- [ ] ¿I2S está configurado y corriendo?
- [ ] ¿El DAC PCM5102A tiene XMT conectado a 3.3V?
- [ ] ¿SCK del PCM5102A está conectado a GND?

### Herramienta de test rápido:
```c
// Test mínimo: responder A5→5A
void SPI1_IRQHandler(void) {
    if (SPI1->SR & SPI_SR_RXNE) {
        uint8_t received = SPI1->DR;
        if (received == 0xA5) {
            SPI1->DR = 0x5A;  // Respond with magic response
        }
    }
}
```

---

## 13. ARCHIVO protocol.h COMPARTIDO

El archivo `src/protocol.h` del proyecto ESP32 contiene TODAS las definiciones del protocolo.
**Copiar este archivo al proyecto STM32** para asegurar que ambos lados usan exactamente las mismas estructuras.

Archivo: `src/protocol.h` (425 líneas)
- Todos los `#define CMD_*` 
- Todas las estructuras de payload `*Payload`
- Enums de filtros y distorsión
- Función `crc16_modbus()`

---

## 14. RESUMEN RÁPIDO

```
┌──────────────────────────────────────────────────────────────────┐
│                    PRIORIDAD DE IMPLEMENTACIÓN                    │
├────┬──────────────────────────────────────────────────────────────┤
│ 1  │ SPI Slave funcional (recibir bytes, detectar magic 0xA5)    │
│ 2  │ Responder PING (cmd 0xEE → responder 0x5A 0xEE + PongResp) │
│ 3  │ Recibir samples (0xA0 → 0xA1×N → 0xA2)                    │
│ 4  │ I2S output a DAC (44.1kHz, 16-bit, stereo)                  │
│ 5  │ Trigger (cmd 0x01) → reproducir sample por I2S              │
│ 6  │ Volumen master/track (cmd 0x10, 0x13)                       │
│ 7  │ Peaks response (cmd 0xE1 → PeaksResponse 68 bytes)          │
│ 8  │ Efectos per-pad y per-track (fase 2)                        │
└────┴──────────────────────────────────────────────────────────────┘
```

**Con los pasos 1-5 implementados, ya debería sonar.**
