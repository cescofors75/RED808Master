# Control UDP para DrumMachine ESP32-S3

## Configuración
- **IP del DrumMachine (Master)**: `192.168.4.1`
- **Puerto UDP**: `8888`
- **WiFi SSID**: `RED808`
- **WiFi Password**: `red808esp32`
- **Formato**: JSON (texto plano)
- **Respuesta**: El ESP32 responde con `{"status":"ok"}` o `{"status":"error","msg":"..."}`

---

## CÓDIGO ARDUINO/ESP32 (Slave Controller)

### Ejemplo Básico - Enviar Comandos UDP

```cpp
#include <WiFi.h>
#include <WiFiUdp.h>
#include <ArduinoJson.h>

// Configuración WiFi del DrumMachine
const char* ssid = "RED808";
const char* password = "red808esp32";

// IP y puerto del DrumMachine
const char* drumMachineIP = "192.168.4.1";
const int udpPort = 8888;

WiFiUDP udp;

void setup() {
  Serial.begin(115200);
  
  // Conectar al WiFi AP del DrumMachine
  Serial.println("Conectando a DrumMachine...");
  WiFi.begin(ssid, password);
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  
  Serial.println("\nConectado!");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());
  
  // Inicializar UDP
  udp.begin(0);  // Puerto local aleatorio
}

void sendCommand(const char* cmd) {
  Serial.print("Enviando: ");
  Serial.println(cmd);
  
  udp.beginPacket(drumMachineIP, udpPort);
  udp.write((uint8_t*)cmd, strlen(cmd));
  udp.endPacket();
  
  delay(10);  // Pequeña pausa para no saturar
}

void sendCommand(JsonDocument& doc) {
  String json;
  serializeJson(doc, json);
  sendCommand(json.c_str());
}

void triggerPad(int pad, int velocity = 127) {
  StaticJsonDocument<128> doc;
  doc["cmd"] = "trigger";
  doc["pad"] = pad;
  doc["vel"] = velocity;
  sendCommand(doc);
}

void setTempo(int bpm) {
  StaticJsonDocument<128> doc;
  doc["cmd"] = "tempo";
  doc["value"] = bpm;
  sendCommand(doc);
}

void startSequencer() {
  StaticJsonDocument<64> doc;
  doc["cmd"] = "start";
  sendCommand(doc);
}

void stopSequencer() {
  StaticJsonDocument<64> doc;
  doc["cmd"] = "stop";
  sendCommand(doc);
}

void setVolume(int volume) {
  StaticJsonDocument<128> doc;
  doc["cmd"] = "setVolume";
  doc["value"] = volume;
  sendCommand(doc);
}

void loop() {
  // Ejemplo: Enviar kick cada 2 segundos
  triggerPad(0, 127);  // Pad 0 = BD (Bombo)
  delay(2000);
}
```

---

### Ejemplo Avanzado - Control con Botones y Potenciómetros

```cpp
#include <WiFi.h>
#include <WiFiUdp.h>
#include <ArduinoJson.h>

// WiFi
const char* ssid = "RED808";
const char* password = "red808esp32";
const char* drumMachineIP = "192.168.4.1";
const int udpPort = 8888;

WiFiUDP udp;

// Pines de botones (conectar a GND con pull-up interno)
const int BTN_KICK = 32;
const int BTN_SNARE = 33;
const int BTN_HIHAT = 25;
const int BTN_START = 26;
const int BTN_STOP = 27;

// Potenciómetros (0-3.3V)
const int POT_TEMPO = 34;
const int POT_VOLUME = 35;

// Variables de estado
unsigned long lastTempoUpdate = 0;
unsigned long lastVolumeUpdate = 0;
int lastTempo = 0;
int lastVolume = 0;

void setup() {
  Serial.begin(115200);
  
  // Configurar botones con pull-up
  pinMode(BTN_KICK, INPUT_PULLUP);
  pinMode(BTN_SNARE, INPUT_PULLUP);
  pinMode(BTN_HIHAT, INPUT_PULLUP);
  pinMode(BTN_START, INPUT_PULLUP);
  pinMode(BTN_STOP, INPUT_PULLUP);
  
  // Conectar WiFi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConectado a DrumMachine");
  
  udp.begin(0);
}

void sendCommand(JsonDocument& doc) {
  String json;
  serializeJson(doc, json);
  
  udp.beginPacket(drumMachineIP, udpPort);
  udp.print(json);
  udp.endPacket();
}

void triggerPad(int pad, int vel = 127) {
  StaticJsonDocument<128> doc;
  doc["cmd"] = "trigger";
  doc["pad"] = pad;
  doc["vel"] = vel;
  sendCommand(doc);
  Serial.printf("Trigger pad %d\n", pad);
}

void setTempo(int bpm) {
  StaticJsonDocument<128> doc;
  doc["cmd"] = "tempo";
  doc["value"] = bpm;
  sendCommand(doc);
  Serial.printf("Tempo: %d BPM\n", bpm);
}

void setVolume(int vol) {
  StaticJsonDocument<128> doc;
  doc["cmd"] = "setVolume";
  doc["value"] = vol;
  sendCommand(doc);
  Serial.printf("Volume: %d\n", vol);
}

void startSequencer() {
  StaticJsonDocument<64> doc;
  doc["cmd"] = "start";
  sendCommand(doc);
  Serial.println("START");
}

void stopSequencer() {
  StaticJsonDocument<64> doc;
  doc["cmd"] = "stop";
  sendCommand(doc);
  Serial.println("STOP");
}

void loop() {
  // Botones de trigger (con debounce simple)
  static bool lastKick = HIGH;
  bool kick = digitalRead(BTN_KICK);
  if (kick == LOW && lastKick == HIGH) {
    triggerPad(0);  // BD
    delay(50);
  }
  lastKick = kick;
  
  static bool lastSnare = HIGH;
  bool snare = digitalRead(BTN_SNARE);
  if (snare == LOW && lastSnare == HIGH) {
    triggerPad(1);  // SD
    delay(50);
  }
  lastSnare = snare;
  
  static bool lastHihat = HIGH;
  bool hihat = digitalRead(BTN_HIHAT);
  if (hihat == LOW && lastHihat == HIGH) {
    triggerPad(2);  // CH
    delay(50);
  }
  lastHihat = hihat;
  
  static bool lastStart = HIGH;
  bool start = digitalRead(BTN_START);
  if (start == LOW && lastStart == HIGH) {
    startSequencer();
    delay(50);
  }
  lastStart = start;
  
  static bool lastStop = HIGH;
  bool stop = digitalRead(BTN_STOP);
  if (stop == LOW && lastStop == HIGH) {
    stopSequencer();
    delay(50);
  }
  lastStop = stop;
  
  // Potenciómetros (actualizar cada 200ms para no saturar)
  if (millis() - lastTempoUpdate > 200) {
    int rawTempo = analogRead(POT_TEMPO);
    int tempo = map(rawTempo, 0, 4095, 60, 200);  // 60-200 BPM
    
    if (abs(tempo - lastTempo) > 2) {  // Hysteresis de 2 BPM
      setTempo(tempo);
      lastTempo = tempo;
    }
    lastTempoUpdate = millis();
  }
  
  if (millis() - lastVolumeUpdate > 200) {
    int rawVolume = analogRead(POT_VOLUME);
    int volume = map(rawVolume, 0, 4095, 0, 100);
    
    if (abs(volume - lastVolume) > 3) {  // Hysteresis de 3%
      setVolume(volume);
      lastVolume = volume;
    }
    lastVolumeUpdate = millis();
  }
  
  delay(10);
}
```

---

### Ejemplo - Step Sequencer Remoto (Programar Patrón)

```cpp
// Programar un patrón básico de 4/4
void programBasicPattern() {
  StaticJsonDocument<128> doc;
  
  // Kicks en beats 1, 5, 9, 13 (track 0 = BD)
  for (int step : {0, 4, 8, 12}) {
    doc.clear();
    doc["cmd"] = "setStep";
    doc["track"] = 0;
    doc["step"] = step;
    doc["active"] = true;
    sendCommand(doc);
    delay(20);
  }
  
  // Snares en beats 5, 13 (track 1 = SD)
  for (int step : {4, 12}) {
    doc.clear();
    doc["cmd"] = "setStep";
    doc["track"] = 1;
    doc["step"] = step;
    doc["active"] = true;
    sendCommand(doc);
    delay(20);
  }
  
  // Hi-hats en cada 8vo (track 2 = CH)
  for (int step = 0; step < 16; step += 2) {
    doc.clear();
    doc["cmd"] = "setStep";
    doc["track"] = 2;
    doc["step"] = step;
    doc["active"] = true;
    sendCommand(doc);
    delay(20);
  }
  
  Serial.println("Patrón programado!");
}
```

---

### Ejemplo - Recibir Respuestas del DrumMachine

```cpp
void loop() {
  // Enviar comando
  triggerPad(0);
  
  // Esperar respuesta (opcional)
  unsigned long timeout = millis() + 100;
  while (millis() < timeout) {
    int packetSize = udp.parsePacket();
    if (packetSize) {
      char response[256];
      int len = udp.read(response, 255);
      response[len] = 0;
      
      Serial.print("Respuesta: ");
      Serial.println(response);
      
      // Parsear respuesta
      StaticJsonDocument<256> doc;
      if (deserializeJson(doc, response) == DeserializationError::Ok) {
        const char* status = doc["status"];
        Serial.printf("Status: %s\n", status);
      }
      break;
    }
    delay(1);
  }
  
  delay(1000);
}
```

---

### Ejemplo - Sincronizar Patrón desde MASTER a SLAVE

```cpp
// Variables globales para almacenar el patrón sincronizado
bool syncedPattern[16][16];  // [track][step]
int syncedPatternNum = -1;

void requestPattern(int patternNum) {
  StaticJsonDocument<128> doc;
  doc["cmd"] = "get_pattern";
  doc["pattern"] = patternNum;
  
  String json;
  serializeJson(doc, json);
  
  udp.beginPacket(drumMachineIP, udpPort);
  udp.print(json);
  udp.endPacket();
  
  Serial.printf("Requesting pattern %d from MASTER...\n", patternNum);
}

void receivePatternSync() {
  int packetSize = udp.parsePacket();
  if (packetSize > 0) {
    char response[2048];
    int len = udp.read(response, 2047);
    response[len] = 0;
    
    // Parsear respuesta
    StaticJsonDocument<2048> doc;
    if (deserializeJson(doc, response) == DeserializationError::Ok) {
      const char* cmd = doc["cmd"];
      
      if (strcmp(cmd, "pattern_sync") == 0) {
        syncedPatternNum = doc["pattern"];
        JsonArray data = doc["data"];
        
        // Copiar datos del patrón
        for (int t = 0; t < 16; t++) {
          JsonArray track = data[t];
          for (int s = 0; s < 16; s++) {
            syncedPattern[t][s] = track[s] ? true : false;
          }
        }
        
        Serial.printf("✓ Pattern %d synced from MASTER!\n", syncedPatternNum + 1);
        
        // Opcional: Mostrar patrón en Serial
        printPattern();
      }
    }
  }
}

void printPattern() {
  Serial.println("\n=== Pattern ===");
  for (int t = 0; t < 16; t++) {
    Serial.printf("Track %2d: ", t);
    for (int s = 0; s < 16; s++) {
      Serial.print(syncedPattern[t][s] ? "■" : "·");
      Serial.print(" ");
    }
    Serial.println();
  }
  Serial.println("===============\n");
}

void loop() {
  // Solicitar patrón 0 del MASTER
  requestPattern(0);
  
  // Esperar respuesta (timeout de 500ms)
  unsigned long timeout = millis() + 500;
  while (millis() < timeout) {
    receivePatternSync();
    if (syncedPatternNum != -1) {
      break;
    }
    delay(10);
  }
  
  if (syncedPatternNum == -1) {
    Serial.println("⚠ Timeout - No response from MASTER");
  }
  
  delay(5000);  // Repetir cada 5 segundos
}
```

---

## Ejemplos de Comandos JSON por UDP

### 1. CONTROL DEL SEQUENCER

#### Iniciar reproducción
```json
{"cmd":"start"}
```

#### Detener reproducción
```json
{"cmd":"stop"}
```

#### Cambiar tempo (BPM)
```json
{"cmd":"tempo","value":140}
```

#### Cambiar patrón (0-4: HIP HOP, TECHNO, DnB, BREAK, HOUSE)
```json
{"cmd":"selectPattern","index":0}
```

#### Solicitar sincronización de patrón (MASTER → SLAVE)
```json
{"cmd":"get_pattern","pattern":0}
```
- `pattern`: 0-15 (número de patrón a sincronizar, opcional - por defecto el patrón activo)
- **Respuesta**: El MASTER envía un mensaje `pattern_sync` con todos los steps del patrón:
```json
{
  "cmd":"pattern_sync",
  "pattern":0,
  "data":[
    [1,0,0,0,1,0,0,0,1,0,0,0,1,0,0,0],
    [0,0,0,0,1,0,0,0,0,0,0,0,1,0,0,0],
    ...
  ]
}
```

---

### 2. EDITAR SEQUENCER

#### Activar/desactivar un step
```json
{"cmd":"setStep","track":0,"step":0,"active":true}
```
- `track`: 0-15 (BD, SD, CH, OH, CP, CB, RS, CL, MA, CY, HT, LT, MC, MT, HC, LC)
- `step`: 0-15 (steps del patrón)
- `active`: true/false

#### Silenciar/activar un track
```json
{"cmd":"mute","track":0,"value":true}
```

#### Activar/desactivar loop en un track
```json
{"cmd":"toggleLoop","track":0}
```

#### Pausar loop
```json
{"cmd":"pauseLoop","track":0}
```

---

### 3. LIVE PADS (Triggers manuales)

#### Trigger un pad (con velocity)
```json
{"cmd":"trigger","pad":0,"vel":127}
```
- `pad`: 0-15 (instrumento)
- `vel`: 0-127 (velocity/volumen, opcional, por defecto 127)

#### Trigger simple
```json
{"cmd":"trigger","pad":5}
```

---

### 4. CARGAR SAMPLES

#### Cargar un sample específico en un pad
```json
{"cmd":"loadSample","family":"BD","filename":"BD_01.wav","pad":0}
```

---

### 5. CONTROL DE VOLUMEN

#### Volumen maestro (0-100)
```json
{"cmd":"setVolume","value":80}
```

#### Volumen del sequencer (0-100)
```json
{"cmd":"setSequencerVolume","value":70}
```

#### Volumen de live pads (0-100)
```json
{"cmd":"setLiveVolume","value":90}
```

---

### 6. EFECTOS (FX)

#### Filtro tipo (0=None, 1=LowPass, 2=HighPass, 3=BandPass, 4=Notch)
```json
{"cmd":"setFilter","type":1}
```

#### Cutoff del filtro (20-20000 Hz)
```json
{"cmd":"setFilterCutoff","value":2000}
```

#### Resonancia del filtro (0.1-10.0)
```json
{"cmd":"setFilterResonance","value":3.0}
```

#### Bit Crush (1-16 bits)
```json
{"cmd":"setBitCrush","value":8}
```

#### Distorsión (0.0-1.0)
```json
{"cmd":"setDistortion","value":0.5}
```

#### Sample Rate Reduction (1000-44100 Hz)
```json
{"cmd":"setSampleRate","value":11025}
```

---

### 7. LED RGB

#### Modo mono (un solo color)
```json
{"cmd":"setLedMonoMode","value":true}
```

---

## EJEMPLOS PYTHON/POWERSHELL (Opcional - PC/Tablet)

### Script Python para enviar comandos UDP

```python
import socket
import json
import time

# Configuración
ESP32_IP = "192.168.4.1"
UDP_PORT = 8888

# Crear socket UDP
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.settimeout(1.0)  # Timeout de 1 segundo

def send_command(cmd_dict):
    """Envía un comando JSON por UDP y espera respuesta"""
    message = json.dumps(cmd_dict)
    print(f"Enviando: {message}")
    
    try:
        # Enviar comando
        sock.sendto(message.encode(), (ESP32_IP, UDP_PORT))
        
        # Esperar respuesta
        data, addr = sock.recvfrom(1024)
        response = json.loads(data.decode())
        print(f"Respuesta: {response}")
        return response
    
    except socket.timeout:
        print("Timeout - no se recibió respuesta")
        return None
    except Exception as e:
        print(f"Error: {e}")
        return None

# EJEMPLOS DE USO:

# 1. Iniciar sequencer a 140 BPM
send_command({"cmd": "tempo", "value": 140})
time.sleep(0.1)
send_command({"cmd": "start"})

# 2. Trigger de pads (simular batería)
pads = [0, 1, 6, 3]  # BD, SD, RS, OH
for pad in pads:
    send_command({"cmd": "trigger", "pad": pad, "vel": 120})
    time.sleep(0.25)

# 3. Cambiar volumen del sequencer
send_command({"cmd": "setSequencerVolume", "value": 80})

# 4. Activar steps en el patrón
for step in [0, 4, 8, 12]:  # Kicks en 1, 5, 9, 13
    send_command({"cmd": "setStep", "track": 0, "step": step, "active": True})
    time.sleep(0.05)

# 5. Aplicar filtro low-pass
send_command({"cmd": "setFilter", "type": 1})
send_command({"cmd": "setFilterCutoff", "value": 1500})

# 6. Detener
time.sleep(5)
send_command({"cmd": "stop"})

# Cerrar socket
sock.close()
```

---

### Script PowerShell (Windows)

```powershell
# Configuración
$ESP32_IP = "192.168.4.1"
$UDP_PORT = 8888

# Crear socket UDP
$udpClient = New-Object System.Net.Sockets.UdpClient

function Send-UdpCommand {
    param($Command)
    
    $json = $Command | ConvertTo-Json -Compress
    $bytes = [Text.Encoding]::UTF8.GetBytes($json)
    
    Write-Host "Enviando: $json"
    $udpClient.Send($bytes, $bytes.Length, $ESP32_IP, $UDP_PORT) | Out-Null
    
    Start-Sleep -Milliseconds 100
}

# EJEMPLOS:

# Iniciar a 120 BPM
Send-UdpCommand @{cmd="tempo"; value=120}
Send-UdpCommand @{cmd="start"}

# Trigger pad 0 (BD)
Send-UdpCommand @{cmd="trigger"; pad=0; vel=127}

# Cambiar volumen
Send-UdpCommand @{cmd="setVolume"; value=75}

# Detener
Send-UdpCommand @{cmd="stop"}

# Cerrar
$udpClient.Close()
```

---

## Notas Importantes

1. **No necesitas autenticación** - El UDP es directo
2. **Baja latencia** - Ideal para triggers en tiempo real
3. **Sin estado de conexión** - Cada paquete es independiente
4. **Límite de tamaño** - Máximo 512 bytes por paquete JSON
5. **El ESP32 responde** - Siempre confirma recepción con JSON
6. **Broadcast funciona** - Los cambios se reflejan en todos los clientes WebSocket conectados

---

## Debugging

Para ver los mensajes UDP en el Serial Monitor del ESP32:
```
[UDP] Received 25 bytes from 192.168.4.2:54321
[UDP] Data: {"cmd":"trigger","pad":0}
```

Si hay error de parsing JSON:
```
[UDP] JSON parse error: InvalidInput
```
