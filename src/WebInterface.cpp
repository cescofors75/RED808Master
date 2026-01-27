/*
 * WebInterface.cpp
 * RED808 Web Interface con WebSockets
 */

#include "WebInterface.h"
#include "AudioEngine.h"
#include "Sequencer.h"
#include "KitManager.h"
#include "SampleManager.h"

extern AudioEngine audioEngine;
extern Sequencer sequencer;
extern KitManager kitManager;
extern SampleManager sampleManager;
extern void triggerPadWithLED(int track, uint8_t velocity);  // Función que enciende LED
extern void setLedMonoMode(bool enabled);

static bool isSupportedSampleFile(const String& filename) {
  String lower = filename;
  lower.toLowerCase();
  return lower.endsWith(".raw") || lower.endsWith(".wav");
}

static const char* detectSampleFormat(const char* filename) {
  if (!filename) {
    return "";
  }
  String name = String(filename);
  if (name.endsWith(".wav") || name.endsWith(".WAV")) {
    return "wav";
  }
  if (name.endsWith(".raw") || name.endsWith(".RAW")) {
    return "raw";
  }
  return "";
}

static bool readWavInfo(File& file, uint32_t& rate, uint16_t& channels, uint16_t& bits) {
  if (!file) return false;
  file.seek(0, SeekSet);
  uint8_t header[44];
  if (file.read(header, sizeof(header)) != sizeof(header)) {
    return false;
  }
  if (memcmp(header, "RIFF", 4) != 0 || memcmp(header + 8, "WAVE", 4) != 0) {
    return false;
  }
  channels = header[22] | (header[23] << 8);
  rate = header[24] | (header[25] << 8) | (header[26] << 16) | (header[27] << 24);
  bits = header[34] | (header[35] << 8);
  return true;
}

static void populateStateDocument(StaticJsonDocument<4096>& doc) {
  doc["type"] = "state";
  doc["playing"] = sequencer.isPlaying();
  doc["tempo"] = sequencer.getTempo();
  doc["pattern"] = sequencer.getCurrentPattern();
  doc["step"] = sequencer.getCurrentStep();
  doc["sequencerVolume"] = audioEngine.getSequencerVolume();
  doc["liveVolume"] = audioEngine.getLiveVolume();
  doc["samplesLoaded"] = sampleManager.getLoadedSamplesCount();
  doc["memoryUsed"] = sampleManager.getTotalMemoryUsed();
  doc["psramFree"] = sampleManager.getFreePSRAM();

    JsonArray loopActive = doc.createNestedArray("loopActive");
    JsonArray loopPaused = doc.createNestedArray("loopPaused");
    for (int track = 0; track < MAX_TRACKS; track++) {
      loopActive.add(sequencer.isLooping(track));
      loopPaused.add(sequencer.isLoopPaused(track));
    }

    JsonArray trackMuted = doc.createNestedArray("trackMuted");
    for (int track = 0; track < MAX_TRACKS; track++) {
      trackMuted.add(sequencer.isTrackMuted(track));
    }

  JsonArray sampleArray = doc.createNestedArray("samples");
  for (int pad = 0; pad < MAX_SAMPLES; pad++) {
    JsonObject sampleObj = sampleArray.createNestedObject();
    sampleObj["pad"] = pad;
    bool loaded = sampleManager.isSampleLoaded(pad);
    sampleObj["loaded"] = loaded;
    if (loaded) {
      const char* name = sampleManager.getSampleName(pad);
      sampleObj["name"] = name ? name : "";
      sampleObj["size"] = sampleManager.getSampleLength(pad) * 2;
      sampleObj["format"] = detectSampleFormat(name);
    }
  }
}

static bool isClientReady(AsyncWebSocketClient* client) {
  return client != nullptr && client->status() == WS_CONNECTED;
}

static void sendSampleCounts(AsyncWebSocketClient* client) {
  if (!client || !isClientReady(client)) {
    Serial.println("[sendSampleCounts] Client not ready");
    return;
  }
  
  StaticJsonDocument<512> sampleCountDoc;
  sampleCountDoc["type"] = "sampleCounts";
  const char* families[] = {"BD", "SD", "CH", "OH", "CP", "CB", "RS", "CL", "MA", "CY", "HT", "LT", "MC", "MT", "HC", "LC"};
  
  Serial.println("[SampleCount] === Counting samples in LittleFS ===");
  int totalFiles = 0;
  
  for (int i = 0; i < 16; i++) {
    String path = String("/") + String(families[i]);
    int count = 0;
    
    File dir = LittleFS.open(path);
    if (!dir) {
      Serial.printf("[SampleCount] ERROR: Cannot open %s\n", path.c_str());
      sampleCountDoc[families[i]] = 0;
      continue;
    }
    
    if (!dir.isDirectory()) {
      Serial.printf("[SampleCount] ERROR: %s is not a directory\n", path.c_str());
      dir.close();
      sampleCountDoc[families[i]] = 0;
      continue;
    }
    
    // Iterar archivos en el directorio
    File file = dir.openNextFile();
    while (file) {
      if (!file.isDirectory()) {
        // Obtener nombre del archivo
        String fullName = file.name();
        String fileName = fullName;
        
        // Extraer solo el nombre del archivo si incluye ruta
        int lastSlash = fullName.lastIndexOf('/');
        if (lastSlash >= 0) {
          fileName = fullName.substring(lastSlash + 1);
        }
        
        // Verificar si es un archivo de audio soportado
        if (isSupportedSampleFile(fileName)) {
          count++;
          Serial.printf("[SampleCount]   %s/%s\n", families[i], fileName.c_str());
        }
      }
      file.close();
      file = dir.openNextFile();
    }
    dir.close();
    
    sampleCountDoc[families[i]] = count;
    totalFiles += count;
    Serial.printf("[SampleCount] %s: %d files\n", families[i], count);
  }
  
  Serial.printf("[SampleCount] === TOTAL: %d samples ===\n", totalFiles);
  
  String countOutput;
  serializeJson(sampleCountDoc, countOutput);
  Serial.printf("[SampleCount] JSON: %s\n", countOutput.c_str());
  client->text(countOutput);
  Serial.printf("[SampleCount] Sent to client %u\n", client->id());
}

WebInterface::WebInterface() {
  server = nullptr;
  ws = nullptr;
  initialized = false;
}

WebInterface::~WebInterface() {
  if (server) delete server;
  if (ws) delete ws;
}

bool WebInterface::begin(const char* ssid, const char* password) {
  // Inicio del WiFi
  Serial.println("  Configurando WiFi...");
  WiFi.mode(WIFI_OFF);
  delay(100);
  
  Serial.println("  Activando modo AP...");
  WiFi.mode(WIFI_AP);
  delay(100);
  
  // Potencia media (15dBm) - más estable que máxima
  Serial.println("  Configurando potencia TX media (15dBm)...");
  WiFi.setTxPower(WIFI_POWER_15dBm);
  delay(50);
  
  Serial.println("  Iniciando SoftAP...");
  WiFi.softAP(ssid, password, 1, 0, 4);  // Canal 1, no oculto, max 4 conexiones
  delay(200);
  
  IPAddress IP = WiFi.softAPIP();
  Serial.print("RED808 AP IP: ");
  Serial.println(IP);
  
  // Crear servidor web
  server = new AsyncWebServer(80);
  ws = new AsyncWebSocket("/ws");
  
  // WebSocket handler
  ws->onEvent([this](AsyncWebSocket *server, AsyncWebSocketClient *client, 
                     AwsEventType type, void *arg, uint8_t *data, size_t len) {
    this->onWebSocketEvent(server, client, type, arg, data, len);
  });
  
  server->addHandler(ws);
  
  // Servir archivos estáticos desde LittleFS
  server->serveStatic("/", LittleFS, "/web/").setDefaultFile("index.html");
  
  // API REST
  server->on("/api/trigger", HTTP_POST, [](AsyncWebServerRequest *request){
    if (request->hasParam("pad", true)) {
      int pad = request->getParam("pad", true)->value().toInt();
      triggerPadWithLED(pad, 127);  // Enciende LED RGB
      request->send(200, "text/plain", "OK");
    } else {
      request->send(400, "text/plain", "Missing pad parameter");
    }
  });
  
  server->on("/api/tempo", HTTP_POST, [](AsyncWebServerRequest *request){
    if (request->hasParam("value", true)) {
      float tempo = request->getParam("value", true)->value().toFloat();
      sequencer.setTempo(tempo);
      request->send(200, "text/plain", "OK");
    }
  });
  
  server->on("/api/pattern", HTTP_POST, [](AsyncWebServerRequest *request){
    if (request->hasParam("index", true)) {
      int pattern = request->getParam("index", true)->value().toInt();
      sequencer.selectPattern(pattern);
      request->send(200, "text/plain", "OK");
    }
  });
  
  server->on("/api/sequencer", HTTP_POST, [](AsyncWebServerRequest *request){
    if (request->hasParam("action", true)) {
      String action = request->getParam("action", true)->value();
      if (action == "start") sequencer.start();
      else if (action == "stop") sequencer.stop();
      request->send(200, "text/plain", "OK");
    }
  });
  
  server->on("/api/getPattern", HTTP_GET, [](AsyncWebServerRequest *request){
    int pattern = sequencer.getCurrentPattern();
    StaticJsonDocument<2048> doc;
    
    for (int track = 0; track < 8; track++) {
      JsonArray trackSteps = doc.createNestedArray(String(track));
      for (int step = 0; step < 16; step++) {
        trackSteps.add(sequencer.getStep(track, step));
      }
    }
    
    String output;
    serializeJson(doc, output);
    request->send(200, "application/json", output);
  });
  
  server->begin();
  Serial.println("✓ RED808 Web Server iniciado");
  
  // Iniciar servidor UDP
  if (udp.begin(UDP_PORT)) {
    Serial.printf("✓ UDP Server listening on port %d\n", UDP_PORT);
    Serial.printf("  Send JSON commands to %s:%d\n", WiFi.localIP().toString().c_str(), UDP_PORT);
  } else {
    Serial.println("⚠ Failed to start UDP server");
  }
  
  initialized = true;
  return true;
}

void WebInterface::onWebSocketEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, 
                                     AwsEventType type, void *arg, uint8_t *data, size_t len) {
  if (type == WS_EVT_CONNECT) {
    Serial.printf("WebSocket client #%u connected\n", client->id());
    sendSequencerStateToClient(client);
    
    // Enviar patrón actual con los 16 tracks solo al nuevo cliente
    if (isClientReady(client)) {
      int pattern = sequencer.getCurrentPattern();
      StaticJsonDocument<4096> responseDoc;
      responseDoc["type"] = "pattern";
      responseDoc["index"] = pattern;
      
      for (int track = 0; track < 16; track++) {
        JsonArray trackSteps = responseDoc.createNestedArray(String(track));
        for (int step = 0; step < 16; step++) {
          trackSteps.add(sequencer.getStep(track, step));
        }
      }
      
      String output;
      serializeJson(responseDoc, output);
      client->text(output);
      Serial.printf("[WebSocket] Sent pattern %d with 16 tracks to client %u\n", pattern, client->id());
    }
    
    // Enviar conteo de samples disponibles por familia al nuevo cliente
    sendSampleCounts(client);
  } else if (type == WS_EVT_DISCONNECT) {
    Serial.printf("WebSocket client #%u disconnected\n", client->id());
  } else if (type == WS_EVT_DATA) {
    AwsFrameInfo *info = (AwsFrameInfo*)arg;
    if (info->final && info->index == 0 && info->len == len) {
      
      // 1. MANEJO DE BINARIO (Baja latencia para Triggers)
      if (info->opcode == WS_BINARY) {
        // Protocolo: [0x90, PAD, VEL]
        if (len == 3 && data[0] == 0x90) {
           int pad = data[1];
           int velocity = data[2];
           triggerPadWithLED(pad, velocity);
           // Opcional: Broadcast para feedback visual en otros clientes
           // broadcastPadTrigger(pad); 
        }
      }
      // 2. MANEJO DE TEXTO (JSON normal)
      else if (info->opcode == WS_TEXT) {
        data[len] = 0;
        
        StaticJsonDocument<512> doc;
        DeserializationError error = deserializeJson(doc, (char*)data);
        
        if (!error) {
          // Usar función común para procesar comandos
          processCommand(doc);
          
          // Comandos específicos del WebSocket que requieren respuesta
          String cmd = doc["cmd"];
          
          if (cmd == "getPattern") {
            int pattern = sequencer.getCurrentPattern();
            StaticJsonDocument<4096> responseDoc;
            responseDoc["type"] = "pattern";
            responseDoc["index"] = pattern;
            
            for (int track = 0; track < 16; track++) {
              JsonArray trackSteps = responseDoc.createNestedArray(String(track));
              for (int step = 0; step < 16; step++) {
                trackSteps.add(sequencer.getStep(track, step));
              }
            }
            
            String output;
            serializeJson(responseDoc, output);
            if (isClientReady(client)) {
              client->text(output);
            } else {
              ws->textAll(output);
            }
          }
          else if (cmd == "getSampleCounts") {
            // Nuevo comando para obtener conteos de samples
            Serial.println("[getSampleCounts] Request received");
            sendSampleCounts(client);
          }
          else if (cmd == "getSamples") {
            // Obtener lista de samples de una familia desde LittleFS
            const char* family = doc["family"];
            int padIndex = doc["pad"];
            
            Serial.printf("[getSamples] Family: %s, Pad: %d\n", family, padIndex);
            
            StaticJsonDocument<2048> responseDoc;
            responseDoc["type"] = "sampleList";
            responseDoc["family"] = family;
            responseDoc["pad"] = padIndex;
            
            String path = String("/") + String(family);
            Serial.printf("[getSamples] Opening: %s\n", path.c_str());
            
            File dir = LittleFS.open(path, "r");
            
            if (dir && dir.isDirectory()) {
              Serial.println("[getSamples] Directory OK, listing files:");
              JsonArray samples = responseDoc.createNestedArray("samples");
              File file = dir.openNextFile();
              int count = 0;
              
              while (file) {
                if (!file.isDirectory()) {
                  String filename = file.name();
                  int lastSlash = filename.lastIndexOf('/');
                  if (lastSlash >= 0) {
                    filename = filename.substring(lastSlash + 1);
                  }
                  
                  if (isSupportedSampleFile(filename)) {
                    JsonObject sampleObj = samples.createNestedObject();
                    sampleObj["name"] = filename;
                    sampleObj["size"] = file.size();
                    const char* format = detectSampleFormat(filename.c_str());
                    sampleObj["format"] = format;
                    uint32_t rate = 0;
                    uint16_t channels = 0;
                    uint16_t bits = 0;
                    if (format && String(format) == "wav") {
                      if (readWavInfo(file, rate, channels, bits)) {
                        sampleObj["rate"] = rate;
                        sampleObj["channels"] = channels;
                        sampleObj["bits"] = bits;
                      } else {
                        sampleObj["rate"] = 0;
                        sampleObj["channels"] = 0;
                        sampleObj["bits"] = 0;
                      }
                    } else {
                      sampleObj["rate"] = 44100;
                      sampleObj["channels"] = 1;
                      sampleObj["bits"] = 16;
                    }
                    count++;
                    Serial.printf("  [%d] %s (%d KB)\n", count, filename.c_str(), file.size() / 1024);
                  }
                }
                file.close();
                file = dir.openNextFile();
              }
              dir.close();
              Serial.printf("[getSamples] Total: %d samples\n", count);
            } else {
              Serial.printf("[getSamples] ERROR: Cannot open %s\n", path.c_str());
            }
            
            String output;
            serializeJson(responseDoc, output);
            if (isClientReady(client)) {
              client->text(output);
            } else {
              ws->textAll(output);
            }
          }
          // getSamples y loadSample ahora manejados en processCommand()
          // Comandos restantes ya procesados por processCommand()
        }
      }
    }
  }
}

void WebInterface::broadcastSequencerState() {
  if (!initialized || !ws) return;
  StaticJsonDocument<4096> doc;
  populateStateDocument(doc);
  String output;
  serializeJson(doc, output);
  ws->textAll(output);
}

void WebInterface::sendSequencerStateToClient(AsyncWebSocketClient* client) {
  if (!initialized || !ws || !isClientReady(client)) {
    return;
  }
  StaticJsonDocument<4096> doc;
  populateStateDocument(doc);
  String output;
  serializeJson(doc, output);
  client->text(output);
}

void WebInterface::broadcastPadTrigger(int pad) {
  if (!initialized || !ws) return;
  StaticJsonDocument<128> doc;
  doc["type"] = "pad";
  doc["pad"] = pad;
  
  String output;
  serializeJson(doc, output);
  ws->textAll(output);
}

void WebInterface::broadcastStep(int step) {
  if (!initialized || !ws) return;
  // Mensaje ultra-compacto para mínima latencia
  StaticJsonDocument<64> doc;
  doc["type"] = "step";
  doc["step"] = step;
  doc["t"] = millis(); // timestamp para sincronización
  
  String output;
  serializeJson(doc, output);
  ws->textAll(output);
}

void WebInterface::update() {
  // Proteger contra llamadas antes de inicialización
  if (!initialized || !ws || !server) return;
  
  // Solo cleanup, el broadcast de steps se hace via callback
  ws->cleanupClients();
  
  // Broadcast audio visualization data every 200ms (~5fps)
  static uint32_t lastVisUpdate = 0;
  if (millis() - lastVisUpdate > 200) {
    broadcastVisualizationData();
    lastVisUpdate = millis();
  }
}

String WebInterface::getIP() {
  return WiFi.softAPIP().toString();
}

void WebInterface::broadcastVisualizationData() {
  if (!initialized || !ws) return;
  // Capture audio data
  uint8_t spectrum[64];
  uint8_t waveform[128];
  audioEngine.captureAudioData(spectrum, waveform);
  
  // Build JSON message - reducido a 512 bytes y eliminado waveform
  StaticJsonDocument<512> doc;
  doc["type"] = "audioData";
  
  // Solo spectrum, reducido a 32 bandas para evitar heap corruption
  JsonArray spectrumArray = doc.createNestedArray("spectrum");
  for (int i = 0; i < 32; i++) {
    // Promediar cada 2 bandas para reducir de 64 a 32
    int avg = (spectrum[i*2] + spectrum[i*2+1]) / 2;
    spectrumArray.add(avg);
  }
  
  String output;
  serializeJson(doc, output);
  ws->textAll(output);
}

// Procesar comandos JSON (compartido entre WebSocket y UDP)
void WebInterface::processCommand(const JsonDocument& doc) {
  String cmd = doc["cmd"];
  
  if (cmd == "trigger") {
    int pad = doc["pad"];
    int velocity = doc.containsKey("vel") ? doc["vel"].as<int>() : 127;
    triggerPadWithLED(pad, velocity);
    broadcastPadTrigger(pad);
  }
  else if (cmd == "setStep") {
    int track = doc["track"];
    int step = doc["step"];
    bool active = doc["active"];
    sequencer.setStep(track, step, active);
  }
  else if (cmd == "start") {
    sequencer.start();
  }
  else if (cmd == "stop") {
    sequencer.stop();
  }
  else if (cmd == "tempo") {
    float tempo = doc["value"];
    sequencer.setTempo(tempo);
  }
  else if (cmd == "selectPattern") {
    int pattern = doc["index"];
    sequencer.selectPattern(pattern);
    delay(50);
    broadcastSequencerState();
  }
  else if (cmd == "loadSample") {
    const char* family = doc["family"];
    const char* filename = doc["filename"];
    int padIndex = doc["pad"];
    
    String fullPath = String("/") + String(family) + String("/") + String(filename);
    Serial.printf("[loadSample] Loading %s to pad %d\n", fullPath.c_str(), padIndex);
    
    if (sampleManager.loadSample(fullPath.c_str(), padIndex)) {
      StaticJsonDocument<256> responseDoc;
      responseDoc["type"] = "sampleLoaded";
      responseDoc["pad"] = padIndex;
      responseDoc["filename"] = filename;
      responseDoc["size"] = sampleManager.getSampleLength(padIndex) * 2;
      responseDoc["format"] = detectSampleFormat(filename);
      
      String output;
      serializeJson(responseDoc, output);
      if (ws) ws->textAll(output);
      
      Serial.printf("[loadSample] Success! Size: %d bytes\n", sampleManager.getSampleLength(padIndex) * 2);
    }
  }
  else if (cmd == "mute") {
    int track = doc["track"];
    bool muted = doc["value"];
    sequencer.muteTrack(track, muted);
  }
  else if (cmd == "toggleLoop") {
    int track = doc["track"];
    sequencer.toggleLoop(track);
    
    StaticJsonDocument<128> responseDoc;
    responseDoc["type"] = "loopState";
    responseDoc["track"] = track;
    responseDoc["active"] = sequencer.isLooping(track);
    responseDoc["paused"] = sequencer.isLoopPaused(track);
    
    String output;
    serializeJson(responseDoc, output);
    if (ws) ws->textAll(output);
  }
  else if (cmd == "pauseLoop") {
    int track = doc["track"];
    sequencer.pauseLoop(track);
    
    StaticJsonDocument<128> responseDoc;
    responseDoc["type"] = "loopState";
    responseDoc["track"] = track;
    responseDoc["active"] = sequencer.isLooping(track);
    responseDoc["paused"] = sequencer.isLoopPaused(track);
    
    String output;
    serializeJson(responseDoc, output);
    if (ws) ws->textAll(output);
  }
  else if (cmd == "setLedMonoMode") {
    bool monoMode = doc["value"];
    setLedMonoMode(monoMode);
  }
  else if (cmd == "setFilter") {
    int type = doc["type"];
    audioEngine.setFilterType((FilterType)type);
  }
  else if (cmd == "setFilterCutoff") {
    float cutoff = doc["value"];
    audioEngine.setFilterCutoff(cutoff);
  }
  else if (cmd == "setFilterResonance") {
    float resonance = doc["value"];
    audioEngine.setFilterResonance(resonance);
  }
  else if (cmd == "setBitCrush") {
    int bits = doc["value"];
    audioEngine.setBitDepth(bits);
  }
  else if (cmd == "setDistortion") {
    float amount = doc["value"];
    audioEngine.setDistortion(amount);
  }
  else if (cmd == "setSampleRate") {
    int rate = doc["value"];
    audioEngine.setSampleRateReduction(rate);
  }
  else if (cmd == "setSequencerVolume") {
    int volume = doc["value"];
    audioEngine.setSequencerVolume(volume);
  }
  else if (cmd == "setLiveVolume") {
    int volume = doc["value"];
    audioEngine.setLiveVolume(volume);
  }
  else if (cmd == "setVolume") {
    int volume = doc["value"];
    audioEngine.setMasterVolume(volume);
  }
  else if (cmd == "get_pattern") {
    int patternNum = doc.containsKey("pattern") ? doc["pattern"].as<int>() : sequencer.getCurrentPattern();
    
    // Crear respuesta con el patrón
    StaticJsonDocument<2048> response;
    response["cmd"] = "pattern_sync";
    response["pattern"] = patternNum;
    
    JsonArray data = response.createNestedArray("data");
    for (int t = 0; t < MAX_TRACKS; t++) {
      JsonArray track = data.createNestedArray();
      for (int s = 0; s < STEPS_PER_PATTERN; s++) {
        track.add(sequencer.getStep(patternNum, t, s) ? 1 : 0);
      }
    }
    
    // Enviar UDP de vuelta al slave (solo si es una petición UDP)
    if (udp.remoteIP() != IPAddress(0, 0, 0, 0)) {
      String json;
      serializeJson(response, json);
      udp.beginPacket(udp.remoteIP(), udp.remotePort());
      udp.write((uint8_t*)json.c_str(), json.length());
      udp.endPacket();
      
      Serial.printf("► Pattern %d sent to SLAVE %s\n", patternNum + 1, udp.remoteIP().toString().c_str());
    }
  }
}

// Manejar paquetes UDP entrantes
void WebInterface::handleUdp() {
  int packetSize = udp.parsePacket();
  if (packetSize > 0) {
    char incomingPacket[512];
    int len = udp.read(incomingPacket, 511);
    if (len > 0) {
      incomingPacket[len] = 0;
      
      Serial.printf("[UDP] Received %d bytes from %s:%d\n", 
                    len, udp.remoteIP().toString().c_str(), udp.remotePort());
      Serial.printf("[UDP] Data: %s\n", incomingPacket);
      
      // Parsear JSON
      StaticJsonDocument<512> doc;
      DeserializationError error = deserializeJson(doc, incomingPacket);
      
      if (!error) {
        // Procesar comando usando la función común
        processCommand(doc);
        
        // Enviar respuesta OK al cliente UDP
        StaticJsonDocument<64> responseDoc;
        responseDoc["status"] = "ok";
        String response;
        serializeJson(responseDoc, response);
        
        udp.beginPacket(udp.remoteIP(), udp.remotePort());
        udp.write((const uint8_t*)response.c_str(), response.length());
        udp.endPacket();
      } else {
        Serial.printf("[UDP] JSON parse error: %s\n", error.c_str());
        
        // Enviar error al cliente
        udp.beginPacket(udp.remoteIP(), udp.remotePort());
        udp.print("{\"status\":\"error\",\"msg\":\"Invalid JSON\"}");
        udp.endPacket();
      }
    }
  }
}
