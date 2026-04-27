# Protocolo UDP Master-Slave - DrumMachine ESP32-S3

> Documento historico. El contrato canonico actual esta en `../RED808_COMMAND_CONTRACT.md`.
> Mantener este archivo solo como ejemplos extendidos; si hay conflicto, manda el contrato canonico.

## Configuración de Red

### MASTER (DrumMachine Principal)
- **IP**: `192.168.4.1` (Access Point)
- **Puerto UDP**: `8888`
- **WiFi SSID**: `RED808`
- **WiFi Password**: `red808esp32`

### SLAVE (Controlador Secundario)
- Conecta al WiFi del MASTER
- Obtiene IP automática (DHCP): `192.168.4.x`
- Puerto local: Cualquiera (aleatorio)

---

## Formato de Comunicación

**Protocolo**: UDP (sin conexión, rápido, ideal para tiempo real)  
**Formato de Datos**: JSON (texto plano)  
**Tamaño Máximo**: 4096 bytes por paquete  
**Encoding**: UTF-8

### Respuesta Estándar del MASTER
```json
{"status":"ok"}
```

El firmware actual tambien puede responder con `{"s":"ok"}` y, para comandos de estado, con un paquete posterior `state_sync`.

### Respuesta de Error
```json
{"status":"error","msg":"Invalid JSON"}
```

---

## Comandos del SLAVE al MASTER

### 1. CONTROL DEL SEQUENCER

#### Iniciar secuenciador
```json
{"cmd":"start"}
```

#### Detener secuenciador
```json
{"cmd":"stop"}
```

#### Cambiar tempo (60-200 BPM)
```json
{"cmd":"tempo","value":140}
```
- `value`: BPM (entero o float)

#### Cambiar patrón activo (0-15)
```json
{"cmd":"selectPattern","index":0}
```
- `index`: 0-15 (número de patrón)

---

### 2. EDICIÓN DE PATRÓN

#### Activar/desactivar un step
```json
{"cmd":"setStep","track":0,"step":0,"active":true}
```
- `track`: 0-15 (pista/instrumento)
- `step`: 0-15 (posición en el patrón)
- El contrato actual permite `step`: 0-63; algunas pantallas legacy solo renderizan 16.
- `active`: true/false

#### Silenciar/activar track
```json
{"cmd":"mute","track":0,"value":true}
```
- `track`: 0-15
- `value`: true (mute) / false (unmute)

#### Toggle loop en track
```json
{"cmd":"toggleLoop","track":0}
```
- `track`: 0-15

#### Pausar loop
```json
{"cmd":"pauseLoop","track":0}
```
- `track`: 0-15

---

### 3. TRIGGER DE PADS (LIVE)

#### Trigger manual con velocity
```json
{"cmd":"trigger","pad":0,"vel":127}
```
- `pad`: 0-15 (instrumento)
- `vel`: 0-127 (velocity/volumen, opcional, default=127)

#### Trigger simple
```json
{"cmd":"trigger","pad":5}
```

**Mapeo de Pads**:
```
0=BD  1=SD  2=CH  3=OH  4=CP  5=CB  6=RS  7=CL
8=MA  9=CY  10=HT 11=LT 12=MC 13=MT 14=HC 15=LC
```

---

### 4. CONTROL DE VOLUMEN

#### Volumen maestro (0-150)
```json
{"cmd":"setVolume","value":80}
```

#### Volumen del sequencer (0-150)
```json
{"cmd":"setSequencerVolume","value":70}
```

#### Volumen de live pads (0-150)
```json
{"cmd":"setLiveVolume","value":90}
```

#### **NUEVO: Volumen por track (0-100)**
```json
{"cmd":"setTrackVolume","track":0,"volume":75}
```
- `track`: 0-7 (pista del sequencer)
- `volume`: 0-100 (0% a 100%, default=100)

**Nota**: El volumen final del track es: `sequencerVolume * trackVolume / 100`

#### **NUEVO: Obtener volumen de un track**
```json
{"cmd":"getTrackVolume","track":0}
```

**Respuesta del MASTER**:
```json
{"type":"trackVolume","track":0,"volume":75}
```

#### **NUEVO: Obtener todos los volúmenes de tracks**
```json
{"cmd":"getTrackVolumes"}
```

**Respuesta del MASTER**:
```json
{"type":"trackVolumes","volumes":[100,75,90,100,80,100,100,100]}
```

---

### 5. EFECTOS DE AUDIO — MASTER FX

#### 5.1 Filtro Master

##### Tipo de filtro (0-19)
```json
{"cmd":"setFilter","type":1}
```
- `type`: 0=None, 1=LowPass, 2=HighPass, 3=BandPass, 4=Notch, 5-19=otros tipos

##### Frecuencia de corte (20-20000 Hz)
```json
{"cmd":"setFilterCutoff","value":2000}
```

##### Resonancia del filtro (0.1-10.0)
```json
{"cmd":"setFilterResonance","value":3.0}
```

#### 5.2 Distorsión & BitCrush Master

##### Distorsión (0.0-1.0)
```json
{"cmd":"setDistortion","value":0.5}
```

##### Modo de distorsión (0-3)
```json
{"cmd":"setDistortionMode","value":1}
```
- `value`: 0=Soft Clip, 1=Hard Clip, 2=Tube, 3=Fuzz

##### Bit Crush (1-16 bits)
```json
{"cmd":"setBitCrush","value":8}
```

##### Reducción de Sample Rate (1000-44100 Hz)
```json
{"cmd":"setSampleRate","value":11025}
```

#### 5.3 Delay Master

##### Activar/desactivar delay
```json
{"cmd":"setDelayActive","value":true}
```

##### Tiempo de delay (1-5000 ms)
```json
{"cmd":"setDelayTime","value":250}
```

##### Feedback del delay (0-100 %)
```json
{"cmd":"setDelayFeedback","value":40}
```

##### Mix dry/wet del delay (0-100 %)
```json
{"cmd":"setDelayMix","value":30}
```

##### Modo estéreo del delay
```json
{"cmd":"setDelayStereo","mode":1}
```
- `mode`: 0=mono, 1=stereo ping-pong

#### 5.4 Reverb Master

##### Activar/desactivar reverb
```json
{"cmd":"setReverbActive","value":true}
```

##### Feedback de reverb (0.0-1.0)
```json
{"cmd":"setReverbFeedback","value":0.45}
```

##### Frecuencia LP de damping (200-12000 Hz)
```json
{"cmd":"setReverbLpFreq","value":6000}
```

##### Mix dry/wet de reverb (0.0-1.0)
```json
{"cmd":"setReverbMix","value":0.12}
```

##### Early Reflections activas
```json
{"cmd":"setEarlyRefActive","active":true}
```

##### Early Reflections mix (0-100)
```json
{"cmd":"setEarlyRefMix","mix":30}
```

#### 5.5 Chorus Master

##### Activar/desactivar chorus
```json
{"cmd":"setChorusActive","value":true}
```

##### Rate del chorus (Hz)
```json
{"cmd":"setChorusRate","value":1.5}
```

##### Depth del chorus (0-100)
```json
{"cmd":"setChorusDepth","value":50}
```

##### Mix dry/wet del chorus (0-100)
```json
{"cmd":"setChorusMix","value":40}
```

##### Modo estéreo del chorus
```json
{"cmd":"setChorusStereo","mode":1}
```
- `mode`: 0=mono, 1=stereo

#### 5.6 Phaser Master

##### Activar/desactivar phaser
```json
{"cmd":"setPhaserActive","value":true}
```

##### Rate del phaser (0-100 → Hz/100)
```json
{"cmd":"setPhaserRate","value":50}
```

##### Depth del phaser (0-100)
```json
{"cmd":"setPhaserDepth","value":60}
```

##### Feedback del phaser (0-100)
```json
{"cmd":"setPhaserFeedback","value":40}
```

#### 5.7 Flanger Master

##### Activar/desactivar flanger
```json
{"cmd":"setFlangerActive","value":true}
```

##### Rate del flanger (0-100 → Hz/100)
```json
{"cmd":"setFlangerRate","value":30}
```

##### Depth del flanger (0-100)
```json
{"cmd":"setFlangerDepth","value":50}
```

##### Feedback del flanger (0-100)
```json
{"cmd":"setFlangerFeedback","value":40}
```

##### Mix dry/wet del flanger (0-100)
```json
{"cmd":"setFlangerMix","value":50}
```

#### 5.8 Compressor Master

##### Activar/desactivar compresor
```json
{"cmd":"setCompressorActive","value":true}
```

##### Threshold (-80 a 0 dB)
```json
{"cmd":"setCompressorThreshold","value":-12}
```

##### Ratio (1-20)
```json
{"cmd":"setCompressorRatio","value":2.5}
```

##### Attack (0-100 ms)
```json
{"cmd":"setCompressorAttack","value":10}
```

##### Release (0-1000 ms)
```json
{"cmd":"setCompressorRelease","value":100}
```

##### Makeup Gain (0-30 dB)
```json
{"cmd":"setCompressorMakeupGain","value":2.0}
```

#### 5.9 Tremolo Master

##### Activar/desactivar tremolo
```json
{"cmd":"setTremoloActive","value":true}
```

##### Rate del tremolo (Hz)
```json
{"cmd":"setTremoloRate","value":5.0}
```

##### Depth del tremolo (0-100)
```json
{"cmd":"setTremoloDepth","value":60}
```

#### 5.10 Wavefolder & Limiter

##### Wavefolder gain (1.0=off, >1 = fold)
```json
{"cmd":"setWavefolderGain","value":3.0}
```

##### Limiter brick-wall 0dBFS
```json
{"cmd":"setLimiterActive","value":true}
```

#### 5.11 Auto-Wah Master

##### Activar/desactivar auto-wah
```json
{"cmd":"setAutoWahActive","active":true}
```

##### Nivel de sensibilidad (0-100)
```json
{"cmd":"setAutoWahLevel","level":80}
```

##### Mix del auto-wah (0-100)
```json
{"cmd":"setAutoWahMix","mix":50}
```

#### 5.12 Stereo Width

##### Ancho estéreo (0-200, 100=normal)
```json
{"cmd":"setStereoWidth","width":100}
```

#### 5.13 Tape Stop & Beat Repeat

##### Tape Stop
```json
{"cmd":"setTapeStop","mode":1}
```
- `mode`: 0=off, 1=activar efecto

##### Beat Repeat
```json
{"cmd":"setBeatRepeat","division":8}
```
- `division`: 0=off, 2/4/8/16=subdivisión del beat

#### 5.14 Master FX Route (Patchbay)

```json
{"cmd":"setMasterFxRoute","fxId":10,"connected":true}
```
- `fxId`: ID del módulo FX en la cadena
- `connected`: true/false (conectar/desconectar del bus)

---

### 6. EFECTOS PER-TRACK (0-15)

#### 6.1 Filtro por Track

##### Configurar filtro en track
```json
{"cmd":"setTrackFilter","track":0,"type":1,"cutoff":1000,"resonance":1.0,"gain":0}
```
- `track`: 0-15
- `type`: 0-19 (tipo de filtro)
- `cutoff`: 20-20000 Hz
- `resonance`: 0.1-10.0
- `gain`: dB (para filtros shelving/peaking)

##### Eliminar filtro de track
```json
{"cmd":"clearTrackFilter","track":0}
```

#### 6.2 Distorsión & BitCrush por Track

##### Distorsión por track
```json
{"cmd":"setTrackDistortion","track":0,"amount":0.5,"mode":1}
```
- `amount`: 0.0-1.0
- `mode`: 0=Soft, 1=Hard, 2=Tube, 3=Fuzz

##### BitCrush por track
```json
{"cmd":"setTrackBitCrush","track":0,"value":8}
```
- `value`: 1-16 bits

##### Limpiar todos los FX de un track
```json
{"cmd":"clearTrackFX","track":0}
```

#### 6.3 Sends por Track

##### Reverb Send (0-100)
```json
{"cmd":"setTrackReverbSend","track":0,"value":30}
```

##### Delay Send (0-100)
```json
{"cmd":"setTrackDelaySend","track":0,"value":25}
```

##### Chorus Send (0-100)
```json
{"cmd":"setTrackChorusSend","track":0,"value":20}
```

#### 6.4 Pan, Mute & Solo por Track

##### Pan (-100=L, 0=C, +100=R)
```json
{"cmd":"setTrackPan","track":0,"value":0}
```

##### Mute en DSP (silenciar en Daisy)
```json
{"cmd":"setTrackDspMute","track":0,"value":true}
```

##### Solo
```json
{"cmd":"setTrackSolo","track":0,"value":true}
```

#### 6.5 Echo por Track (Live FX)

```json
{"cmd":"setTrackEcho","track":0,"active":true,"time":100,"feedback":40,"mix":50}
```
- `time`: ms de delay
- `feedback`: % de retroalimentación
- `mix`: % dry/wet
- Modo alternativo one-knob: `{"cmd":"setTrackEcho","track":0,"value":80}` (0-127)

#### 6.6 Flanger por Track (Live FX)

```json
{"cmd":"setTrackFlanger","track":0,"active":true,"rate":50,"depth":50,"feedback":30}
```
- `rate`: velocidad (Hz %)
- `depth`: profundidad (%)
- `feedback`: retroalimentación (%)
- Modo alternativo one-knob: `{"cmd":"setTrackFlanger","track":0,"value":80}` (0-127)

#### 6.7 Compressor por Track (Live FX)

```json
{"cmd":"setTrackCompressor","track":0,"active":true,"threshold":-20,"ratio":4}
```
- `threshold`: dB (en one-knob: value 0=-60dB, 127=0dB)
- `ratio`: ratio de compresión
- Modo alternativo one-knob: `{"cmd":"setTrackCompressor","track":0,"value":80}` (0-127)

#### 6.8 Phaser por Track

```json
{"cmd":"setTrackPhaser","track":0,"active":true,"rate":1.0,"depth":50,"feedback":50}
```

#### 6.9 Tremolo por Track

```json
{"cmd":"setTrackTremolo","track":0,"active":true,"rate":4,"depth":50,"wave":0,"target":0}
```
- `wave`: 0=sine, 1=triangle, 2=square, 3=saw, 4=random
- `target`: 0=volume, 1=pan

#### 6.10 Pitch por Track (-1200 a +1200 cents)

```json
{"cmd":"setTrackPitch","track":0,"value":100}
```
- `value`: cents (-1200 a +1200, 0=no pitch shift)

#### 6.11 Gate por Track

```json
{"cmd":"setTrackGate","track":0,"active":true,"threshold":-40,"attack":1,"release":50}
```
- `threshold`: dB de apertura
- `attack`: ms
- `release`: ms

#### 6.12 EQ por Track (3 bandas)

##### EQ completo (low/mid/high en un solo comando)
```json
{"cmd":"setTrackEq","track":0,"low":3,"mid":-2,"high":5}
```

##### EQ individual por banda
```json
{"cmd":"setTrackEqLow","track":0,"value":3}
{"cmd":"setTrackEqMid","track":0,"value":-2}
{"cmd":"setTrackEqHigh","track":0,"value":5}
```
- `value`/`low`/`mid`/`high`: -12 a +12 dB

#### 6.13 LFO por Track (Daisy-side)

```json
{"cmd":"setTrackLfo","track":0,"wave":0,"target":3,"rate":100,"depth":500}
```
- `wave`: 0=sine, 1=triangle, 2=square, 3=saw, 4=random
- `target`: 0=volume, 1=pitch, 2=filter cutoff, 3=pan, 4=decay, 5=LED brightness, 6=distortion drive, 7=bitcrush, 8=reverb send, 9=delay send
- `rate`: centésimas de Hz (100 = 1.00 Hz)
- `depth`: milésimas (500 = 0.500)

#### 6.14 Limpiar Live FX de track

```json
{"cmd":"clearTrackLiveFX","track":0}
```
> Elimina echo, flanger y compressor del track

---

### 7. EFECTOS PER-PAD (0-23)

> Los pads 0-15 = tracks del sequencer, 16-23 = live pads extra

#### 7.1 Filtro por Pad

```json
{"cmd":"setPadFilter","pad":0,"type":1,"cutoff":1000,"resonance":1.0,"gain":0}
```

#### Eliminar filtro de pad
```json
{"cmd":"clearPadFilter","pad":0}
```

#### 7.2 Distorsión por Pad

```json
{"cmd":"setPadDistortion","pad":0,"amount":0.5,"mode":1}
```

#### 7.3 BitCrush por Pad

```json
{"cmd":"setPadBitCrush","pad":0,"value":8}
```

#### 7.4 Limpiar todos los FX del pad

```json
{"cmd":"clearPadFX","pad":0}
```

---

### 8. EFECTOS DE SAMPLE (Track o Pad)

> Estos comandos aceptan `"track"` O `"pad"` como clave.

#### 8.1 Reverse

```json
{"cmd":"setReverse","track":0,"value":true}
```
```json
{"cmd":"setReverse","pad":0,"value":true}
```

#### 8.2 Pitch Shift (0.25 - 3.0)

```json
{"cmd":"setPitchShift","track":0,"value":1.5}
```
- `value`: multiplicador (1.0=original, 0.5=octava abajo, 2.0=octava arriba)

#### 8.3 Stutter

```json
{"cmd":"setStutter","track":0,"value":true,"interval":100}
```
- `value` / `active`: true/false
- `interval`: ms entre repeticiones

#### 8.4 Scratch (configurable)

```json
{"cmd":"setScratch","track":0,"value":true,"rate":5,"depth":0.85,"filter":4000,"crackle":0.25}
```
- `rate`: velocidad del scratch
- `depth`: profundidad (0.0-1.0)
- `filter`: frecuencia de filtro (Hz)
- `crackle`: nivel de ruido tipo vinilo (0.0-1.0)

#### 8.5 Turntablism (configurable)

```json
{"cmd":"setTurntablism","track":0,"value":true,"control":"auto","mode":-1,"brakeSpeed":350,"backspinSpeed":450,"transformRate":11,"vinylNoise":0.35}
```
- `control`: "auto" o "manual"
- `mode`: -1=random, 0=brake, 1=backspin, 2=transform
- `brakeSpeed`: ms de frenado
- `backspinSpeed`: ms de backspin
- `transformRate`: velocidad del transform (Hz)
- `vinylNoise`: nivel de ruido vinilo (0.0-1.0)

---

### 9. SIDECHAIN PRO

```json
{"cmd":"setSidechainPro","active":true,"source":0,"destinations":[1,2,3],"amount":50,"attack":6,"release":180,"knee":0.4}
```
- `source`: track fuente (0-15, típicamente BD=0)
- `destinations`: array de tracks destino (se excluye source)
- `amount`: cantidad de ducking (0-100 %)
- `attack`: ms de attack
- `release`: ms de release
- `knee`: suavidad (0.0-1.0)

---

### 10. CHOKE GROUPS

```json
{"cmd":"setChokeGroup","pad":6,"group":1}
```
- `pad`: 0-23
- `group`: 0=sin grupo, 1-8=grupo (pads del mismo grupo se silencian mutuamente)

---

### 11. CARGA DE SAMPLES

#### Cargar sample en un pad
```json
{"cmd":"loadSample","family":"BD","filename":"BD_01.wav","pad":0}
```
- `family`: "BD","SD","CH","OH","CP","CB","RS","CL","MA","CY","HT","LT","MC","MT","HC","LC"
- `filename`: Nombre del archivo WAV en la carpeta
- `pad`: 0-15 (destino)

---

### 12. LED RGB

#### Modo mono (un solo color)
```json
{"cmd":"setLedMonoMode","value":true}
```
- `value`: true/false

---

## Comandos del MASTER al SLAVE

### SINCRONIZACIÓN DE PATRÓN

#### Solicitud del SLAVE al MASTER
```json
{"cmd":"get_pattern","pattern":0}
```
- `pattern`: 0-15 (opcional, si se omite devuelve el patrón activo)

#### Respuesta del MASTER al SLAVE
```json
{
  "cmd":"pattern_sync",
  "pattern":0,
  "data":[
    [1,0,0,0,1,0,0,0,1,0,0,0,1,0,0,0],
    [0,0,0,0,1,0,0,0,0,0,0,0,1,0,0,0],
    [1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0],
    [0,0,0,0,0,0,1,0,0,0,0,0,0,0,1,0],
    [0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0],
    [0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0],
    [0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0],
    [0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0],
    [0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0],
    [0,1,0,0,0,0,0,0,0,1,0,0,0,0,0,0],
    [0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0],
    [0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0],
    [0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0],
    [0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0],
    [0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0],
    [0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0]
  ]
}
```

**Estructura de `data`**:
- Array de 16 tracks
- Cada track tiene 16 steps
- `1` = step activo, `0` = step inactivo

---

## Ejemplo Completo - Código SLAVE (ESP32/Arduino)

```cpp
#include <WiFi.h>
#include <WiFiUdp.h>
#include <ArduinoJson.h>

// Configuración WiFi del MASTER
const char* ssid = "RED808";
const char* password = "red808esp32";
const char* masterIP = "192.168.4.1";
const int udpPort = 8888;

WiFiUDP udp;

// Almacenar patrón sincronizado
bool syncedPattern[16][16];  // [track][step]
int currentPattern = -1;

void setup() {
  Serial.begin(115200);
  
  // Conectar al MASTER
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\n✓ Conectado al MASTER");
  Serial.printf("IP: %s\n", WiFi.localIP().toString().c_str());
  
  // Iniciar UDP
  udp.begin(0);  // Puerto local aleatorio
}

// Enviar comando al MASTER
void sendCommand(JsonDocument& doc) {
  String json;
  serializeJson(doc, json);
  
  udp.beginPacket(masterIP, udpPort);
  udp.print(json);
  udp.endPacket();
  
  Serial.printf("→ MASTER: %s\n", json.c_str());
}

// Trigger pad
void triggerPad(int pad, int vel = 127) {
  StaticJsonDocument<128> doc;
  doc["cmd"] = "trigger";
  doc["pad"] = pad;
  doc["vel"] = vel;
  sendCommand(doc);
}

// Cambiar tempo
void setTempo(int bpm) {
  StaticJsonDocument<128> doc;
  doc["cmd"] = "tempo";
  doc["value"] = bpm;
  sendCommand(doc);
}

// Iniciar/detener secuenciador
void start() {
  StaticJsonDocument<64> doc;
  doc["cmd"] = "start";
  sendCommand(doc);
}

void stop() {
  StaticJsonDocument<64> doc;
  doc["cmd"] = "stop";
  sendCommand(doc);
}

// Solicitar sincronización de patrón
void requestPattern(int patternNum) {
  StaticJsonDocument<128> doc;
  doc["cmd"] = "get_pattern";
  doc["pattern"] = patternNum;
  sendCommand(doc);
}

// Recibir respuesta del MASTER
void receiveResponse() {
  int packetSize = udp.parsePacket();
  if (packetSize > 0) {
    char buffer[2048];
    int len = udp.read(buffer, 2047);
    buffer[len] = 0;
    
    Serial.printf("← MASTER: %s\n", buffer);
    
    // Parsear JSON
    StaticJsonDocument<2048> doc;
    if (deserializeJson(doc, buffer) == DeserializationError::Ok) {
      
      // Respuesta estándar
      if (doc.containsKey("status")) {
        const char* status = doc["status"];
        if (strcmp(status, "ok") == 0) {
          Serial.println("✓ Comando OK");
        } else {
          Serial.printf("✗ Error: %s\n", doc["msg"].as<const char*>());
        }
      }
      
      // Sincronización de patrón
      else if (doc["cmd"] == "pattern_sync") {
        currentPattern = doc["pattern"];
        JsonArray data = doc["data"];
        
        // Copiar patrón
        for (int t = 0; t < 16; t++) {
          JsonArray track = data[t];
          for (int s = 0; s < 16; s++) {
            syncedPattern[t][s] = track[s] ? true : false;
          }
        }
        
        Serial.printf("✓ Patrón %d sincronizado!\n", currentPattern);
      }
    }
  }
}

// Establecer step en el secuenciador
void setStep(int track, int step, bool active) {
  StaticJsonDocument<128> doc;
  doc["cmd"] = "setStep";
  doc["track"] = track;
  doc["step"] = step;
  doc["active"] = active;
  sendCommand(doc);
}

// Cambiar volumen maestro
void setVolume(int volume) {
  StaticJsonDocument<128> doc;
  doc["cmd"] = "setVolume";
  doc["value"] = volume;
  sendCommand(doc);
}

void loop() {
  // Ejemplo: Solicitar patrón 0
  requestPattern(0);
  delay(100);
  receiveResponse();
  
  // Ejemplo: Trigger pads
  triggerPad(0, 127);  // BD
  delay(500);
  triggerPad(1, 120);  // SD
  delay(500);
  
  // Ejemplo: Cambiar tempo
  setTempo(140);
  delay(100);
  receiveResponse();
  
  // Ejemplo: Iniciar secuenciador
  start();
  delay(100);
  receiveResponse();
  
  delay(5000);
  
  // Detener
  stop();
  delay(100);
  receiveResponse();
  
  delay(5000);
}
```

---

## Resumen de Flujo de Datos

```
SLAVE → MASTER: Comandos de control (trigger, tempo, start, stop, etc.)
MASTER → SLAVE: {"status":"ok"} o {"status":"error","msg":"..."}

SLAVE → MASTER: {"cmd":"get_pattern","pattern":0}
MASTER → SLAVE: {"cmd":"pattern_sync","pattern":0,"data":[[...],[...],...]}
```

---

## Tabla Resumen de Comandos UDP

| Categoría | Comando | Parámetros | Descripción |
|-----------|---------|------------|-------------|
| **Sequencer** | `start` | - | Iniciar secuenciador |
| | `stop` | - | Detener secuenciador |
| | `tempo` | `value` (60-200) | Cambiar BPM |
| | `selectPattern` | `index` (0-15) | Cambiar patrón activo |
| **Pattern** | `setStep` | `track`, `step`, `active` | Activar/desactivar step |
| | `mute` | `track`, `value` | Silenciar/activar track |
| | `toggleLoop` | `track` | Toggle loop en track |
| | `pauseLoop` | `track` | Pausar loop |
| | `setStepVelocity` | `track`, `step`, `velocity` | Velocity de step (0-127) |
| | `getStepVelocity` | `track`, `step` | Obtener velocity |
| **Pads** | `trigger` | `pad`, `vel` (opc.) | Trigger pad con velocity |
| **Volumen** | `setVolume` | `value` (0-150) | Volumen maestro |
| | `setSequencerVolume` | `value` (0-150) | Volumen sequencer |
| | `setLiveVolume` | `value` (0-150) | Volumen live pads |
| | `setTrackVolume` | `track`, `volume` (0-100) | Volumen por track |
| | `getTrackVolume` | `track` | Obtener volumen track |
| | `getTrackVolumes` | - | Todos los volúmenes |
| **Master Filter** | `setFilter` | `type` (0-19) | Tipo de filtro global |
| | `setFilterCutoff` | `value` (20-20000) | Frecuencia corte Hz |
| | `setFilterResonance` | `value` (0.1-10.0) | Resonancia |
| **Master Dist/Crush** | `setDistortion` | `value` (0.0-1.0) | Distorsión |
| | `setDistortionMode` | `value` (0-3) | Modo: soft/hard/tube/fuzz |
| | `setBitCrush` | `value` (1-16) | Bit depth |
| | `setSampleRate` | `value` (1000-44100) | Sample rate reduction |
| **Master Delay** | `setDelayActive` | `value` (bool) | On/off |
| | `setDelayTime` | `value` (1-5000) | Tiempo ms |
| | `setDelayFeedback` | `value` (0-100) | Feedback % |
| | `setDelayMix` | `value` (0-100) | Mix % |
| | `setDelayStereo` | `mode` (0-1) | Mono/stereo |
| **Master Reverb** | `setReverbActive` | `value` (bool) | On/off |
| | `setReverbFeedback` | `value` (0.0-1.0) | Feedback |
| | `setReverbLpFreq` | `value` (200-12000) | Damping Hz |
| | `setReverbMix` | `value` (0.0-1.0) | Mix dry/wet |
| | `setEarlyRefActive` | `active` (bool) | Early reflections on/off |
| | `setEarlyRefMix` | `mix` (0-100) | Early ref mix |
| **Master Chorus** | `setChorusActive` | `value` (bool) | On/off |
| | `setChorusRate` | `value` (Hz) | Rate |
| | `setChorusDepth` | `value` (0-100) | Depth |
| | `setChorusMix` | `value` (0-100) | Mix |
| | `setChorusStereo` | `mode` (0-1) | Mono/stereo |
| **Master Phaser** | `setPhaserActive` | `value` (bool) | On/off |
| | `setPhaserRate` | `value` (0-100) | Rate |
| | `setPhaserDepth` | `value` (0-100) | Depth |
| | `setPhaserFeedback` | `value` (0-100) | Feedback |
| **Master Flanger** | `setFlangerActive` | `value` (bool) | On/off |
| | `setFlangerRate` | `value` (0-100) | Rate |
| | `setFlangerDepth` | `value` (0-100) | Depth |
| | `setFlangerFeedback` | `value` (0-100) | Feedback |
| | `setFlangerMix` | `value` (0-100) | Mix |
| **Master Compressor** | `setCompressorActive` | `value` (bool) | On/off |
| | `setCompressorThreshold` | `value` (-80~0) | Threshold dB |
| | `setCompressorRatio` | `value` (1-20) | Ratio |
| | `setCompressorAttack` | `value` (0-100) | Attack ms |
| | `setCompressorRelease` | `value` (0-1000) | Release ms |
| | `setCompressorMakeupGain` | `value` (0-30) | Makeup dB |
| **Master Tremolo** | `setTremoloActive` | `value` (bool) | On/off |
| | `setTremoloRate` | `value` (Hz) | Rate |
| | `setTremoloDepth` | `value` (0-100) | Depth |
| **Wavefolder/Limiter** | `setWavefolderGain` | `value` (1.0-10.0) | Gain (1=off) |
| | `setLimiterActive` | `value` (bool) | Limiter on/off |
| **Auto-Wah** | `setAutoWahActive` | `active` (bool) | On/off |
| | `setAutoWahLevel` | `level` (0-100) | Sensibilidad |
| | `setAutoWahMix` | `mix` (0-100) | Mix |
| **Stereo/FX Route** | `setStereoWidth` | `width` (0-200) | Ancho estéreo |
| | `setMasterFxRoute` | `fxId`, `connected` | Patchbay route |
| | `setTapeStop` | `mode` (0-1) | Tape stop effect |
| | `setBeatRepeat` | `division` (0/2/4/8/16) | Beat repeat |
| **Track Filter** | `setTrackFilter` | `track`, `type`, `cutoff`, `resonance`, `gain` | Filtro per-track |
| | `clearTrackFilter` | `track` | Eliminar filtro |
| **Track Dist/Crush** | `setTrackDistortion` | `track`, `amount`, `mode` | Distorsión per-track |
| | `setTrackBitCrush` | `track`, `value` | BitCrush per-track |
| | `clearTrackFX` | `track` | Limpiar todos FX |
| **Track Sends** | `setTrackReverbSend` | `track`, `value` (0-100) | Send reverb |
| | `setTrackDelaySend` | `track`, `value` (0-100) | Send delay |
| | `setTrackChorusSend` | `track`, `value` (0-100) | Send chorus |
| **Track Mixer** | `setTrackPan` | `track`, `value` (-100~100) | Pan L/R |
| | `setTrackDspMute` | `track`, `value` (bool) | Mute DSP |
| | `setTrackSolo` | `track`, `value` (bool) | Solo |
| **Track Live FX** | `setTrackEcho` | `track`, `active/value`, `time`, `feedback`, `mix` | Echo per-track |
| | `setTrackFlanger` | `track`, `active/value`, `rate`, `depth`, `feedback` | Flanger per-track |
| | `setTrackCompressor` | `track`, `active/value`, `threshold`, `ratio` | Comp per-track |
| | `clearTrackLiveFX` | `track` | Limpiar live FX |
| **Track Advanced** | `setTrackPhaser` | `track`, `active`, `rate`, `depth`, `feedback` | Phaser per-track |
| | `setTrackTremolo` | `track`, `active`, `rate`, `depth`, `wave`, `target` | Tremolo per-track |
| | `setTrackPitch` | `track`, `value` (-1200~1200) | Pitch cents |
| | `setTrackGate` | `track`, `active`, `threshold`, `attack`, `release` | Gate per-track |
| **Track EQ** | `setTrackEq` | `track`, `low`, `mid`, `high` | EQ 3 bandas |
| | `setTrackEqLow/Mid/High` | `track`, `value` (-12~12) | EQ individual |
| **Track LFO** | `setTrackLfo` | `track`, `wave`, `target`, `rate`, `depth` | LFO per-track |
| **Pad FX** | `setPadFilter` | `pad`, `type`, `cutoff`, `resonance`, `gain` | Filtro per-pad |
| | `clearPadFilter` | `pad` | Eliminar filtro pad |
| | `setPadDistortion` | `pad`, `amount`, `mode` | Distorsión per-pad |
| | `setPadBitCrush` | `pad`, `value` | BitCrush per-pad |
| | `clearPadFX` | `pad` | Limpiar FX pad |
| **Sample FX** | `setReverse` | `track/pad`, `value` | Reverse sample |
| | `setPitchShift` | `track/pad`, `value` (0.25-3.0) | Pitch shift |
| | `setStutter` | `track/pad`, `value`, `interval` | Stutter |
| | `setScratch` | `track`, `value`, `rate`, `depth`, `filter`, `crackle` | Scratch DJ |
| | `setTurntablism` | `track`, `value`, `control`, `mode`, etc. | Turntablism DJ |
| **Sidechain** | `setSidechainPro` | `active`, `source`, `destinations`, `amount`, etc. | Sidechain ducking |
| **Choke Groups** | `setChokeGroup` | `pad`, `group` (0-8) | Choke group |
| **Samples** | `loadSample` | `family`, `filename`, `pad` | Cargar sample |
| **LED** | `setLedMonoMode` | `value` (bool) | Modo mono LED |
| **Sync** | `get_pattern` | `pattern` (opc.) | Solicitar patrón |

---

## Notas Importantes

1. **Sin autenticación** - El protocolo UDP es abierto
2. **Baja latencia** - Ideal para triggers en tiempo real (<10ms)
3. **Sin garantía de entrega** - UDP no garantiza que lleguen todos los paquetes
4. **Sin orden** - Los paquetes pueden llegar desordenados
5. **El MASTER siempre responde** - Confirma recepción con JSON
6. **Tamaño máximo** - 512 bytes por comando, 2048 bytes para `pattern_sync`
7. **Los cambios se propagan** - Todos los clientes WebSocket también reciben los cambios
8. **Timeout recomendado** - 500ms para esperar respuesta del MASTER

---

## Debugging

### En el MASTER (Serial Monitor):
```
[UDP] Received 25 bytes from 192.168.4.2:54321
[UDP] Data: {"cmd":"trigger","pad":0}
► Pattern 0 sent to SLAVE 192.168.4.2
```

### En el SLAVE:
```
→ MASTER: {"cmd":"trigger","pad":0,"vel":127}
← MASTER: {"status":"ok"}
✓ Comando OK
```
