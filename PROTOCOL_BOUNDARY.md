# RED808 Protocol Boundary (ESP32 ↔ Daisy)

## Principio
ESP32 es **master de lógica y tiempo**. Daisy es **motor DSP**.
Por el enlace UART viajan **instrucciones**, no cálculos de audio.

## ESP32 → Daisy (Command Plane)
- `TRIGGER`: `CMD_TRIGGER_SEQ` / `CMD_TRIGGER_LIVE`
- `PARAM_RT`: familias `CMD_FILTER_*`, `CMD_DELAY_*`, `CMD_TRACK_*`, `CMD_PAD_*`, `CMD_SYNTH_*`
- `TEMPO`: `CMD_TEMPO` (BPM global de transporte)
- `LOAD_SAMPLE`: `CMD_SD_LOAD_SAMPLE` / `CMD_SD_LOAD_KIT`
- `UPLOAD_START/CHUNK/END`: `CMD_SAMPLE_BEGIN` / `CMD_SAMPLE_DATA` / `CMD_SAMPLE_END`
- `MUTE`: `CMD_TRACK_MUTE`
- `REQ_DIR`: `CMD_SD_LIST_FOLDERS` / `CMD_SD_LIST_FILES`

## Daisy → ESP32 (Control Responses)
- `ACK/ERR`: respuesta estándar (`SPI_MAGIC_RESP`)
- estado de carga / SD / eventos (`CMD_SD_STATUS`, `CMD_GET_EVENTS`)
- listados de directorios/archivos SD

## No cruza el cable
- valores de LFO ya calculados por sample
- formas de onda o buffers de análisis
- lógica de patrón/edición de steps
- timing del secuenciador (clock maestro permanece en ESP32)
- estado de red/web

## Nota
`SYNC_PATTERN` se mantiene como operación del dominio ESP32 (sequencer truth). Si se requiere handshake explícito Daisy-side, se añade un comando dedicado de metadatos sin mover el timing al slave.
