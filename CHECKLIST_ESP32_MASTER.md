# Checklist Maestro ESP32-S3 (Master) — Pre Build/Upload

Fecha: 2026-03-01
Repositorio: RED808Master
Branch: main

## Estado rápido

- [ ] 1) Frontera de responsabilidades (ESP32 manda, Daisy ejecuta)
- [ ] 2) Cobertura Web completa (todo controlable desde UI)
- [ ] 3) Limpieza de lógica “hacia Daisy”
- [ ] 4) Protocolo UART robusto y sincronizado
- [ ] 5) Sincronización de eventos y triggers
- [ ] 6) SD/Upload en background
- [ ] 7) Gate de build y release

---

## 1) Frontera de responsabilidades (ESP32 manda, Daisy ejecuta)

### Validaciones
- [ ] El secuenciador (steps, patrón, song mode, probabilidad, ratchet, humanize) vive solo en ESP32.
- [ ] El cálculo de LFO/modulación vive solo en ESP32.
- [ ] Daisy no decide timing ni lógica de patrón; solo recibe comandos.
- [ ] Ningún cálculo de audio/LFO/step-state cruza UART.

### Evidencia esperada
- Referencias en código de ESP32: secuenciador/LFO/timing centralizados.
- Referencias en protocolo: comandos de ejecución en Daisy sin lógica de patrón.

Resultado: [ ] OK  [ ] NOK
Notas:

---

## 2) Cobertura Web completa (todo controlable desde UI)

### Validaciones
- [ ] Cada control web tiene comando WS/REST real y handler backend.
- [ ] Expuestos: filtros global/track/pad, FX master/track/pad, env/locks por step, synth 808/909/505/303.
- [ ] Al cambiar patrón, la UI recibe y repinta todos los locks/valores (incl. cutoff/reverb por step).
- [ ] No hay parámetros huérfanos (control sin comando o comando sin control).

### Evidencia esperada
- Mapeo UI ↔ API/WS ↔ backend documentado/validado.
- Prueba manual de repintado completo por cambio de patrón.

Resultado: [ ] OK  [ ] NOK
Notas:

---

## 3) Limpieza de lógica hacia Daisy

### Validaciones
- [ ] Daisy no conserva estado lógico de sesión que compita con ESP32.
- [ ] Daisy no empuja telemetría no permitida en modo estricto (picos, waveform, lógica de step).
- [ ] Solo mantiene ACK/ERR, estado de carga SD y eventos de carga.

### Evidencia esperada
- Flags/modo estricto verificables.
- Tráfico UART revisado con payloads permitidos.

Resultado: [ ] OK  [ ] NOK
Notas:

---

## 4) Protocolo UART robusto y sincronizado

### Validaciones
- [ ] Comandos canónicos definidos y usados: `TRIGGER`, `PARAM_RT`, `TEMPO`, `SYNC/STATE`, `LOAD_SAMPLE`, `UPLOAD_START/CHUNK/END`, `MUTE`, `REQ_DIR`.
- [ ] Paquete con `magic`, `len`, `seq`, `crc` validado en ambos lados.
- [ ] Reintentos y timeout definidos por tipo de comando.
- [ ] `sequence/ACK` evita duplicados o comandos fuera de orden.
- [ ] Manejo explícito de errores de parseo/CRC/timeout.

### Evidencia esperada
- Tabla de comandos vigente.
- Casos de error reproducibles y observables en log.

Resultado: [ ] OK  [ ] NOK
Notas:

---

## 5) Sincronización de eventos y triggers

### Validaciones
- [ ] Trigger live y trigger sequencer llegan sin jitter audible.
- [ ] `TEMPO` se propaga a Daisy al arrancar y en cambios en vivo.
- [ ] Cambios de patrón no pierden eventos en transición.
- [ ] Locks por step se aplican exactamente en el step correcto.

### Evidencia esperada
- Smoke timing test sin dropouts.
- Logs/telemetría de confirmación de tempo y step-lock.

Resultado: [ ] OK  [ ] NOK
Notas:

---

## 6) SD/Upload en background

### Validaciones
- [ ] `REQ_DIR` devuelve listas consistentes de carpetas/archivos.
- [ ] `LOAD_SAMPLE` y `LOAD_KIT` reportan progreso y resultado.
- [ ] `UPLOAD_START/CHUNK/END` tolera cortes/reintentos sin corromper estado.
- [ ] Abort/cancel limpia correctamente buffers/estado.

### Evidencia esperada
- Pruebas con interrupción real en mitad de upload.
- Recuperación limpia sin reinicio manual.

Resultado: [ ] OK  [ ] NOK
Notas:

---

## 7) Gate de build y release

### Validaciones
- [ ] `pio run` limpio en master.
- [ ] Sin warnings críticos de protocolo/packing/overflow.
- [ ] Smoke test mínimo validado:
  - [ ] play/stop
  - [ ] tempo
  - [ ] trigger 16 pads
  - [ ] cambio patrón
  - [ ] 3 FX master
  - [ ] 2 FX track
  - [ ] 1 lock por step
  - [ ] load kit
  - [ ] upload wav
- [ ] Log final de validación guardado con timestamp.

### Evidencia esperada
- Salida de build archivada.
- Registro de smoke test completo.

Resultado: [ ] OK  [ ] NOK
Notas:

---

## Log de validación (timestamp)

### Ejecución
- Fecha/hora:
- Firmware hash/commit:
- Board/entorno:
- Operador:

### Build
- Comando: `pio run`
- Resultado:
- Warnings críticos: [ ] No  [ ] Sí (detallar)

### Smoke test
- Play/Stop:
- Tempo:
- 16 pads:
- Cambio patrón:
- FX master (3):
- FX track (2):
- Step lock (1):
- Load kit:
- Upload wav:

### Cierre
- Veredicto release: [ ] GO  [ ] NO-GO
- Incidencias abiertas:
- Acciones correctivas:
- Próxima revisión:
