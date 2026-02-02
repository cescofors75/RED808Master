/*
 * WebInterface.cpp
 * RED808 Web Interface con WebSockets
 */

#include "WebInterface.h"
#include "AudioEngine.h"
#include "Sequencer.h"
#include "KitManager.h"
#include "SampleManager.h"
#include <map>

// Timeout para clientes UDP (30 segundos sin actividad)
#define UDP_CLIENT_TIMEOUT 30000

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

static void populateStateDocument(StaticJsonDocument<6144>& doc) {
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
  
  // Verificar que LittleFS está montado
  if (!LittleFS.begin(false)) {
    Serial.println("[sendSampleCounts] ERROR: LittleFS not mounted!");
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
      Serial.printf("[SampleCount] WARN: Cannot open %s\n", path.c_str());
      sampleCountDoc[families[i]] = 0;
      yield(); // Yield inmediato si hay error
      continue;
    }
    
    if (!dir.isDirectory()) {
      Serial.printf("[SampleCount] WARN: %s is not a directory\n", path.c_str());
      dir.close();
      sampleCountDoc[families[i]] = 0;
      yield();
      continue;
    }
    
    // Iterar archivos en el directorio
    File file = dir.openNextFile();
    int fileCount = 0;
    while (file) {
      fileCount++;
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
        }
      }
      file.close();
      
      // Yield cada 5 archivos para evitar watchdog
      if (fileCount % 5 == 0) {
        yield();
      }
      
      file = dir.openNextFile();
    }
    dir.close();
    
    sampleCountDoc[families[i]] = count;
    totalFiles += count;
    Serial.printf("[SampleCount] %s: %d files\n", families[i], count);
    
    // Yield después de cada familia
    yield();
  }
  
  Serial.printf("[SampleCount] === TOTAL: %d samples ===\n", totalFiles);
  
  String countOutput;
  serializeJson(sampleCountDoc, countOutput);
  
  if (isClientReady(client)) {
    client->text(countOutput);
    Serial.printf("[SampleCount] Sent to client %u\n", client->id());
  } else {
    Serial.println("[sendSampleCounts] Client disconnected before sending data");
  }
}

WebInterface::WebInterface() {
  server = nullptr;
  ws = nullptr;
  initialized = false;
  midiController = nullptr;
}

WebInterface::~WebInterface() {
  if (server) delete server;
  if (ws) delete ws;
}

bool WebInterface::begin(const char* ssid, const char* password) {
  // Inicio del WiFi con configuración estable
  Serial.println("  Configurando WiFi...");
  
  // Desactivar ahorro de energía WiFi
  WiFi.setSleep(false);
  
  WiFi.mode(WIFI_OFF);
  delay(200);
  
  Serial.println("  Activando modo AP...");
  WiFi.mode(WIFI_AP);
  delay(200);
  
  // Potencia reducida (11dBm) para mayor estabilidad
  Serial.println("  Configurando potencia TX reducida (11dBm)...");
  WiFi.setTxPower(WIFI_POWER_11dBm);
  delay(100);
  
  // IP fija para evitar conflictos
  IPAddress local_IP(192, 168, 4, 1);
  IPAddress gateway(192, 168, 4, 1);
  IPAddress subnet(255, 255, 255, 0);
  WiFi.softAPConfig(local_IP, gateway, subnet);
  
  Serial.println("  Iniciando SoftAP...");
  // Canal 6, no oculto, max 4 conexiones (3 WebSockets + 1 reserva)
  WiFi.softAP(ssid, password, 6, 0, 4);
  delay(500);
  
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
  
  // Servir página de administración con cache
  server->on("/adm", HTTP_GET, [](AsyncWebServerRequest *request){
    AsyncWebServerResponse *response = request->beginResponse(LittleFS, "/web/admin.html", "text/html");
    response->addHeader("Cache-Control", "max-age=3600");  // Cache 1h
    request->send(response);
  });
  
  // Servir archivos estáticos desde LittleFS con cache agresivo
  server->serveStatic("/", LittleFS, "/web/")
    .setDefaultFile("index.html")
    .setCacheControl("max-age=86400");  // Cache 24h para velocidad
  
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
  
  // Endpoint para info del sistema (para dashboard /adm)
  server->on("/api/sysinfo", HTTP_GET, [this](AsyncWebServerRequest *request){
    StaticJsonDocument<3072> doc;
    
    // Info de memoria
    doc["heapFree"] = ESP.getFreeHeap();
    doc["heapSize"] = ESP.getHeapSize();
    doc["psramFree"] = ESP.getFreePsram();
    doc["psramSize"] = ESP.getPsramSize();
    doc["flashSize"] = ESP.getFlashChipSize();
    
    // Info de WiFi
    doc["wifiMode"] = "AP";
    doc["ssid"] = WiFi.softAPSSID();
    doc["ip"] = WiFi.softAPIP().toString();
    doc["channel"] = WiFi.channel();
    doc["txPower"] = "11dBm";
    doc["connectedStations"] = WiFi.softAPgetStationNum();
    
    // Info de WebSocket
    if (ws) {
      doc["wsClients"] = ws->count();
      JsonArray clients = doc.createNestedArray("wsClientList");
      for (auto client : ws->getClients()) {
        JsonObject c = clients.createNestedObject();
        c["id"] = client->id();
        c["ip"] = client->remoteIP().toString();
        c["status"] = client->status();
      }
    }
    
    // Info de clientes UDP
    Serial.printf("[sysinfo] UDP clients count: %d\n", udpClients.size());
    doc["udpClients"] = udpClients.size();
    JsonArray udpClientsList = doc.createNestedArray("udpClientList");
    unsigned long now = millis();
    for (const auto& pair : udpClients) {
      JsonObject c = udpClientsList.createNestedObject();
      c["ip"] = pair.second.ip.toString();
      c["port"] = pair.second.port;
      c["lastSeen"] = (now - pair.second.lastSeen) / 1000; // segundos desde última actividad
      c["packets"] = pair.second.packetCount;
      Serial.printf("[sysinfo] Adding UDP client: %s:%d (packets: %d)\n", 
                    pair.second.ip.toString().c_str(), pair.second.port, pair.second.packetCount);
    }
    
    // Info del secuenciador
    doc["tempo"] = sequencer.getTempo();
    doc["playing"] = sequencer.isPlaying();
    doc["pattern"] = sequencer.getCurrentPattern();
    doc["samplesLoaded"] = sampleManager.getLoadedSamplesCount();
    doc["memoryUsed"] = sampleManager.getTotalMemoryUsed();
    
    // Info MIDI USB
    MIDIDeviceInfo midiInfo = midiController->getDeviceInfo();
    doc["midiConnected"] = midiInfo.connected;
    if (midiInfo.connected) {
      doc["midiDevice"] = midiInfo.deviceName;
      doc["midiVendorId"] = String(midiInfo.vendorId, HEX);
      doc["midiProductId"] = String(midiInfo.productId, HEX);
    }
    
    // Uptime
    doc["uptime"] = millis();
    
    String output;
    serializeJson(doc, output);
    request->send(200, "application/json", output);
  });
  
  // Endpoint para subir samples WAV
  server->on("/api/upload", HTTP_POST, 
    [](AsyncWebServerRequest *request){
      // No enviar respuesta aquí - se maneja en handleUpload cuando final=true
    },
    [this](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final){
      // Handler del upload (chunked)
      handleUpload(request, filename, index, data, len, final);
    }
  );
  
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
    
    // OPTIMIZED: Send only basic state on connect (512 bytes)
    // Full data will be sent via 'init' command from client
    StaticJsonDocument<512> basicState;
    basicState["type"] = "connected";
    basicState["playing"] = sequencer.isPlaying();
    basicState["tempo"] = sequencer.getTempo();
    basicState["pattern"] = sequencer.getCurrentPattern();
    basicState["clientId"] = client->id();
    basicState["message"] = "Connected. Send 'init' command to load full state.";
    
    String output;
    serializeJson(basicState, output);
    client->text(output);
    Serial.printf("[WebSocket] Client #%u connected - basic state sent (wait for init)\n", client->id());
    
    // NO enviar automáticamente - el cliente lo pedirá con comando "getSampleCounts"
    Serial.println("[WebSocket] Client connected, waiting for explicit requests");
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
            StaticJsonDocument<3072> responseDoc;
            responseDoc["type"] = "pattern";
            responseDoc["index"] = pattern;
            
            // Send steps (active/inactive) - Solo 8 tracks activos
            for (int track = 0; track < 8; track++) {
              JsonArray trackSteps = responseDoc.createNestedArray(String(track));
              for (int step = 0; step < 16; step++) {
                trackSteps.add(sequencer.getStep(track, step));
              }
            }
            
            // Send velocities
            JsonObject velocitiesObj = responseDoc.createNestedObject("velocities");
            for (int track = 0; track < 8; track++) {
              JsonArray trackVels = velocitiesObj.createNestedArray(String(track));
              for (int step = 0; step < 16; step++) {
                trackVels.add(sequencer.getStepVelocity(track, step));
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
          else if (cmd == "init") {
            // Cliente solicita inicialización completa (se llama después de conectar)
            Serial.printf("[init] Client %u requesting full initialization\n", client->id());
            
            // 1. Enviar solo estado del sequencer (sin patrón)
            yield(); // Give time to other tasks
            sendSequencerStateToClient(client);
            delay(50); // Aumentado delay para estabilidad
            
            // 2. Cliente solicitará patrón con getPattern cuando esté listo
            // NO enviamos patrón automáticamente para evitar overflow
            
            // 3. Cliente solicitará samples con getSampleCounts cuando esté listo
            Serial.println("[init] Complete. Client should request pattern and samples next.");
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
            
            // Verificar que LittleFS está montado
            if (!LittleFS.begin(false)) {
              Serial.println("[getSamples] ERROR: LittleFS not mounted!");
              return;
            }
            
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
                    
                    // Yield cada 3 samples para evitar watchdog
                    if (count % 3 == 0) {
                      yield();
                    }
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
  StaticJsonDocument<6144> doc;
  populateStateDocument(doc);
  String output;
  serializeJson(doc, output);
  ws->textAll(output);
}

void WebInterface::sendSequencerStateToClient(AsyncWebSocketClient* client) {
  if (!initialized || !ws || !isClientReady(client)) {
    return;
  }
  StaticJsonDocument<6144> doc;
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
  
  // DESACTIVADO: cleanupClients() causa crashes con clientes recién conectados
  // La librería maneja la limpieza automáticamente en segundo plano
  // ws->cleanupClients();
  
  // Limpiar clientes UDP inactivos cada 10 segundos
  static unsigned long lastCleanup = 0;
  if (millis() - lastCleanup > 10000) {
    cleanupStaleUdpClients();
    lastCleanup = millis();
  }
  
  // DESACTIVADO: Visualización ralentiza el sistema
  // // Broadcast audio visualization data every 200ms (~5fps)
  // static uint32_t lastVisUpdate = 0;
  // if (millis() - lastVisUpdate > 200) {
  //   broadcastVisualizationData();
  //   lastVisUpdate = millis();
  // }
}

String WebInterface::getIP() {
  return WiFi.softAPIP().toString();
}

void WebInterface::broadcastVisualizationData() {
  // DESACTIVADO: No enviar datos de visualización para evitar saturación
  return;
  
  // if (!initialized || !ws) return;
  // // Capture audio data
  // uint8_t spectrum[64];
  // uint8_t waveform[128];
  // audioEngine.captureAudioData(spectrum, waveform);
  // 
  // // Build JSON message - reducido a 512 bytes y eliminado waveform
  // StaticJsonDocument<512> doc;
  // doc["type"] = "audioData";
  // 
  // // Solo spectrum, reducido a 32 bandas para evitar heap corruption
  // JsonArray spectrumArray = doc.createNestedArray("spectrum");
  // for (int i = 0; i < 32; i++) {
  //   // Promediar cada 2 bandas para reducir de 64 a 32
  //   int avg = (spectrum[i*2] + spectrum[i*2+1]) / 2;
  //   spectrumArray.add(avg);
  // }
  // 
  // String output;
  // serializeJson(doc, output);
  // ws->textAll(output);
}

// Procesar comandos JSON (compartido entre WebSocket y UDP)
void WebInterface::processCommand(const JsonDocument& doc) {
  String cmd = doc["cmd"];
  
  if (cmd == "trigger") {
    int pad = doc["pad"];
    if (pad < 0 || pad >= 8) {
      Serial.printf("[WS] Invalid pad %d (must be 0-7)\n", pad);
      return;
    }
    int velocity = doc.containsKey("vel") ? doc["vel"].as<int>() : 127;
    triggerPadWithLED(pad, velocity);
    broadcastPadTrigger(pad);
  }
  else if (cmd == "setStep") {
    int track = doc["track"];
    int step = doc["step"];
    if (track < 0 || track >= 8 || step < 0 || step >= 16) {
      Serial.printf("[WS] Invalid track %d or step %d\n", track, step);
      return;
    }
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
    
    // Enviar estado actualizado
    broadcastSequencerState();
    
    // Enviar datos del patrón (matriz de steps) - Solo 8 tracks activos
    StaticJsonDocument<3072> patternDoc;
    patternDoc["type"] = "pattern";
    patternDoc["index"] = pattern;
    
    for (int track = 0; track < 8; track++) {
      JsonArray trackSteps = patternDoc.createNestedArray(String(track));
      for (int step = 0; step < 16; step++) {
        trackSteps.add(sequencer.getStep(track, step));
      }
    }
    
    JsonObject velocitiesObj = patternDoc.createNestedObject("velocities");
    for (int track = 0; track < 8; track++) {
      JsonArray trackVels = velocitiesObj.createNestedArray(String(track));
      for (int step = 0; step < 16; step++) {
        trackVels.add(sequencer.getStepVelocity(track, step));
      }
    }
    
    String patternOutput;
    serializeJson(patternDoc, patternOutput);
    ws->textAll(patternOutput);
  }
  else if (cmd == "loadSample") {
    const char* family = doc["family"];
    const char* filename = doc["filename"];
    int padIndex = doc["pad"];
    if (padIndex < 0 || padIndex >= 8) {
      Serial.printf("[WS] Invalid pad %d (must be 0-7)\n", padIndex);
      return;
    }
    
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
    if (track < 0 || track >= 8) {
      Serial.printf("[WS] Invalid track %d (must be 0-7)\n", track);
      return;
    }
    bool muted = doc["value"];
    sequencer.muteTrack(track, muted);
  }
  else if (cmd == "toggleLoop") {
    int track = doc["track"];
    if (track < 0 || track >= 8) {
      Serial.printf("[WS] Invalid track %d (must be 0-7)\n", track);
      return;
    }
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
    if (track < 0 || track >= 8) {
      Serial.printf("[WS] Invalid track %d (must be 0-7)\n", track);
      return;
    }
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
  // ============= NEW: Per-Track Filter Commands =============
  else if (cmd == "setTrackFilter") {
    int track = doc["track"];
    if (track < 0 || track >= 8) {
      Serial.printf("[WS] Invalid track %d (must be 0-7)\n", track);
      return;
    }
    int filterType = doc["filterType"];
    float cutoff = doc.containsKey("cutoff") ? doc["cutoff"].as<float>() : 1000.0f;
    float resonance = doc.containsKey("resonance") ? doc["resonance"].as<float>() : 1.0f;
    float gain = doc.containsKey("gain") ? doc["gain"].as<float>() : 0.0f;
    
    bool success = audioEngine.setTrackFilter(track, (FilterType)filterType, cutoff, resonance, gain);
    
    // Send response with filter parameters for UI badge
    StaticJsonDocument<256> responseDoc;
    responseDoc["type"] = "trackFilterSet";
    responseDoc["track"] = track;
    responseDoc["success"] = success;
    responseDoc["activeFilters"] = audioEngine.getActiveTrackFiltersCount();
    responseDoc["filterType"] = filterType;
    responseDoc["cutoff"] = (int)cutoff;
    responseDoc["resonance"] = resonance;
    
    String output;
    serializeJson(responseDoc, output);
    if (ws) ws->textAll(output);
  }
  else if (cmd == "clearTrackFilter") {
    int track = doc["track"];
    if (track < 0 || track >= 8) {
      Serial.printf("[WS] Invalid track %d (must be 0-7)\n", track);
      return;
    }
    audioEngine.clearTrackFilter(track);
    
    // Send response
    StaticJsonDocument<128> responseDoc;
    responseDoc["type"] = "trackFilterCleared";
    responseDoc["track"] = track;
    responseDoc["activeFilters"] = audioEngine.getActiveTrackFiltersCount();
    
    String output;
    serializeJson(responseDoc, output);
    if (ws) ws->textAll(output);
  }
  // ============= NEW: Per-Pad Filter Commands =============
  else if (cmd == "setPadFilter") {
    int pad = doc["pad"];
    if (pad < 0 || pad >= 8) {
      Serial.printf("[WS] Invalid pad %d (must be 0-7)\n", pad);
      return;
    }
    int filterType = doc["filterType"];
    float cutoff = doc.containsKey("cutoff") ? doc["cutoff"].as<float>() : 1000.0f;
    float resonance = doc.containsKey("resonance") ? doc["resonance"].as<float>() : 1.0f;
    float gain = doc.containsKey("gain") ? doc["gain"].as<float>() : 0.0f;
    
    bool success = audioEngine.setPadFilter(pad, (FilterType)filterType, cutoff, resonance, gain);
    
    // Send response
    StaticJsonDocument<128> responseDoc;
    responseDoc["type"] = "padFilterSet";
    responseDoc["pad"] = pad;
    responseDoc["success"] = success;
    responseDoc["activeFilters"] = audioEngine.getActivePadFiltersCount();
    
    String output;
    serializeJson(responseDoc, output);
    if (ws) ws->textAll(output);
  }
  else if (cmd == "clearPadFilter") {
    int pad = doc["pad"];
    if (pad < 0 || pad >= 8) {
      Serial.printf("[WS] Invalid pad %d (must be 0-7)\n", pad);
      return;
    }
    audioEngine.clearPadFilter(pad);
    
    // Send response
    StaticJsonDocument<128> responseDoc;
    responseDoc["type"] = "padFilterCleared";
    responseDoc["pad"] = pad;
    responseDoc["activeFilters"] = audioEngine.getActivePadFiltersCount();
    
    String output;
    serializeJson(responseDoc, output);
    if (ws) ws->textAll(output);
  }
  else if (cmd == "getFilterPresets") {
    // Return list of available filter presets
    StaticJsonDocument<512> responseDoc;
    responseDoc["type"] = "filterPresets";
    
    JsonArray presets = responseDoc.createNestedArray("presets");
    for (int i = 0; i <= 9; i++) {
      JsonObject preset = presets.createNestedObject();
      const FilterPreset* fp = AudioEngine::getFilterPreset((FilterType)i);
      preset["id"] = i;
      preset["name"] = fp->name;
      preset["cutoff"] = fp->cutoff;
      preset["resonance"] = fp->resonance;
      preset["gain"] = fp->gain;
    }
    
    String output;
    serializeJson(responseDoc, output);
    if (ws) ws->textAll(output);
  }
  // ============= NEW: Step Velocity Commands =============
  else if (cmd == "setStepVelocity") {
    int track = doc["track"];
    int step = doc["step"];
    int velocity = doc["velocity"];
    if (track < 0 || track >= 8 || step < 0 || step >= 16) {
      Serial.printf("[WS] Invalid track %d or step %d\n", track, step);
      return;
    }
    
    sequencer.setStepVelocity(track, step, velocity);
    
    // Broadcast to all clients
    StaticJsonDocument<128> responseDoc;
    responseDoc["type"] = "stepVelocitySet";
    responseDoc["track"] = track;
    responseDoc["step"] = step;
    responseDoc["velocity"] = velocity;
    
    String output;
    serializeJson(responseDoc, output);
    if (ws) ws->textAll(output);
  }
  else if (cmd == "getStepVelocity") {
    int track = doc["track"];
    int step = doc["step"];
    
    uint8_t velocity = sequencer.getStepVelocity(track, step);
    
    // Send response
    StaticJsonDocument<128> responseDoc;
    responseDoc["type"] = "stepVelocity";
    responseDoc["track"] = track;
    responseDoc["step"] = step;
    responseDoc["velocity"] = velocity;
    
    String output;
    serializeJson(responseDoc, output);
    if (ws) ws->textAll(output);
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

// Actualizar o registrar cliente UDP
void WebInterface::updateUdpClient(IPAddress ip, uint16_t port) {
  String key = ip.toString();
  
  if (udpClients.find(key) != udpClients.end()) {
    // Cliente existente, actualizar
    udpClients[key].lastSeen = millis();
    udpClients[key].packetCount++;
    Serial.printf("[UDP] Client updated: %s:%d (packets: %d)\n", 
                  ip.toString().c_str(), port, udpClients[key].packetCount);
  } else {
    // Nuevo cliente
    UdpClient client;
    client.ip = ip;
    client.port = port;
    client.lastSeen = millis();
    client.packetCount = 1;
    udpClients[key] = client;
    Serial.printf("[UDP] New client registered: %s:%d (total clients: %d)\n", 
                  ip.toString().c_str(), port, udpClients.size());
  }
}

// Limpiar clientes UDP inactivos
void WebInterface::cleanupStaleUdpClients() {
  unsigned long now = millis();
  auto it = udpClients.begin();
  
  while (it != udpClients.end()) {
    if (now - it->second.lastSeen > UDP_CLIENT_TIMEOUT) {
      Serial.printf("[UDP] Client timeout: %s\n", it->first.c_str());
      it = udpClients.erase(it);
    } else {
      ++it;
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
      
      // Registrar cliente UDP
      updateUdpClient(udp.remoteIP(), udp.remotePort());
      
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

// ========================================
// MIDI Functions
// ========================================

void WebInterface::setMIDIController(MIDIController* controller) {
  midiController = controller;
  Serial.println("[WebInterface] MIDI Controller reference set");
}

void WebInterface::broadcastMIDIMessage(const MIDIMessage& msg) {
  if (!initialized || !ws) return;
  
  StaticJsonDocument<256> doc;
  doc["type"] = "midiMessage";
  
  // Message type string
  const char* msgType = "unknown";
  switch (msg.type) {
    case MIDI_NOTE_ON: msgType = "noteOn"; break;
    case MIDI_NOTE_OFF: msgType = "noteOff"; break;
    case MIDI_CONTROL_CHANGE: msgType = "cc"; break;
    case MIDI_PROGRAM_CHANGE: msgType = "program"; break;
    case MIDI_PITCH_BEND: msgType = "pitchBend"; break;
    case MIDI_AFTERTOUCH: msgType = "aftertouch"; break;
    case MIDI_CHANNEL_PRESSURE: msgType = "pressure"; break;
  }
  
  doc["messageType"] = msgType;
  doc["channel"] = msg.channel + 1; // 1-16 for display
  doc["data1"] = msg.data1;
  doc["data2"] = msg.data2;
  doc["timestamp"] = msg.timestamp;
  
  String output;
  serializeJson(doc, output);
  ws->textAll(output);
}

void WebInterface::broadcastMIDIDeviceStatus(bool connected, const MIDIDeviceInfo& info) {
  if (!initialized || !ws) return;
  
  StaticJsonDocument<512> doc;
  doc["type"] = "midiDevice";
  doc["connected"] = connected;
  
  if (connected) {
    doc["deviceName"] = info.deviceName;
    doc["vendorId"] = info.vendorId;
    doc["productId"] = info.productId;
    doc["connectTime"] = info.connectTime;
  }
  
  String output;
  serializeJson(doc, output);
  ws->textAll(output);
  
  Serial.printf("[WebInterface] MIDI device status broadcast: %s\n", 
                connected ? "connected" : "disconnected");
}

// ========================================
// SAMPLE UPLOAD FUNCTIONS
// ========================================

// Variables estáticas para mantener estado del upload
static File uploadFile;
static String uploadFilename;
static int uploadPad = -1;
static size_t uploadSize = 0;
static size_t uploadReceived = 0;
static bool uploadError = false;
static String uploadErrorMsg = "";

void WebInterface::handleUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
  // Obtener pad del parámetro de la URL
  if (index == 0) {
    // Primera parte del upload - reset de variables
    uploadFilename = filename;
    uploadReceived = 0;
    uploadError = false;
    uploadErrorMsg = "";
    
    Serial.println("\n╔════════════════════════════════════════════════╗");
    Serial.println("║         📤 UPLOAD REQUEST RECEIVED            ║");
    Serial.println("╚════════════════════════════════════════════════╝");
    Serial.printf("[Upload] Filename: %s\n", filename.c_str());
    Serial.printf("[Upload] Checking for 'pad' parameter...\n");
    
    // Buscar el parámetro 'pad' en la query string (GET params)
    if (!request->hasParam("pad", false)) {
      Serial.println("[Upload] ERROR: Missing 'pad' parameter in query string");
      Serial.printf("[Upload] Request params count: %d\n", request->params());
      for (int i = 0; i < request->params(); i++) {
        AsyncWebParameter* p = request->getParam(i);
        Serial.printf("[Upload] Param[%d]: %s = %s (isPost=%d, isFile=%d)\n", 
                     i, p->name().c_str(), p->value().c_str(), p->isPost(), p->isFile());
      }
      uploadError = true;
      uploadErrorMsg = "Missing pad parameter";
      broadcastUploadComplete(-1, false, uploadErrorMsg);
      return;
    }
    
    uploadPad = request->getParam("pad", false)->value().toInt();
    Serial.printf("[Upload] ✓ Pad parameter found: %d\n", uploadPad);
    
    if (uploadPad < 0 || uploadPad >= MAX_SAMPLES) {
      Serial.printf("[Upload] ERROR: Invalid pad number: %d\n", uploadPad);
      uploadError = true;
      uploadErrorMsg = "Invalid pad number";
      broadcastUploadComplete(uploadPad, false, uploadErrorMsg);
      return;
    }
    
    // Validar extensión
    if (!filename.endsWith(".wav") && !filename.endsWith(".WAV")) {
      Serial.printf("[Upload] ERROR: Invalid file type: %s\n", filename.c_str());
      uploadError = true;
      uploadErrorMsg = "Only WAV files are supported";
      broadcastUploadComplete(uploadPad, false, uploadErrorMsg);
      return;
    }
    
    // Obtener nombre de la familia del pad
    const char* families[] = {"BD", "SD", "CH", "OH", "CP", "RS", "CL", "CY",
                              "CB", "MA", "HC", "HT", "MC", "MT", "LC", "LT"};
    String familyName = families[uploadPad];
    
    // Crear directorio si no existe
    String dirPath = "/" + familyName;
    if (!LittleFS.exists(dirPath)) {
      LittleFS.mkdir(dirPath);
    }
    
    // Crear path completo
    String filePath = dirPath + "/" + filename;
    
    Serial.println("\n╔═══════════════════════════════════════════════╗");
    Serial.printf("║  📤 UPLOAD INICIADO: %s\n", filename.c_str());
    Serial.println("╚═══════════════════════════════════════════════╝");
    Serial.printf("[Upload] Pad: %d (%s)\n", uploadPad, familyName.c_str());
    Serial.printf("[Upload] File: %s\n", filePath.c_str());
    
    // Abrir archivo para escritura
    uploadFile = LittleFS.open(filePath, "w");
    if (!uploadFile) {
      Serial.println("[Upload] ERROR: Failed to create file");
      uploadError = true;
      uploadErrorMsg = "Failed to create file on flash";
      broadcastUploadComplete(uploadPad, false, uploadErrorMsg);
      return;
    }
    
    uploadSize = request->contentLength();
    Serial.printf("[Upload] Expected size: %d bytes\n", uploadSize);
    
    // Validar tamaño (max 2MB para seguridad)
    if (uploadSize > 2 * 1024 * 1024) {
      Serial.println("[Upload] ERROR: File too large (max 2MB)");
      uploadFile.close();
      uploadError = true;
      uploadErrorMsg = "File too large (max 2MB)";
      broadcastUploadComplete(uploadPad, false, uploadErrorMsg);
      return;
    }
  }
  
  // Si hay error previo, no procesar más chunks
  if (uploadError) {
    if (final) {
      request->send(400, "application/json", "{\"success\":false,\"message\":\"" + uploadErrorMsg + "\"}");
    }
    return;
  }
  
  // Escribir chunk de datos
  if (uploadFile && len) {
    size_t written = uploadFile.write(data, len);
    if (written != len) {
      Serial.printf("[Upload] ERROR: Write failed (%d/%d bytes)\n", written, len);
      uploadFile.close();
      uploadError = true;
      uploadErrorMsg = "Write error";
      broadcastUploadComplete(uploadPad, false, uploadErrorMsg);
      if (final) {
        request->send(500, "application/json", "{\"success\":false,\"message\":\"Write error\"}");
      }
      return;
    }
    
    uploadReceived += len;
    
    // Progreso cada 10%
    int percent = (uploadReceived * 100) / uploadSize;
    static int lastPercent = -1;
    if (percent != lastPercent && percent % 10 == 0) {
      Serial.printf("[Upload] Progress: %d%% (%d/%d bytes)\n", percent, uploadReceived, uploadSize);
      broadcastUploadProgress(uploadPad, percent);
      lastPercent = percent;
    }
  }
  
  // Upload completado
  if (final) {
    if (uploadFile) {
      uploadFile.close();
      
      Serial.printf("[Upload] ✓ File written: %d bytes\n", uploadReceived);
      
      // Validar formato WAV
      String filePath = "/" + String((const char*[]){"BD", "SD", "CH", "OH", "CP", "RS", "CL", "CY",
                                                      "CB", "MA", "HC", "HT", "MC", "MT", "LC", "LT"}[uploadPad]) + "/" + uploadFilename;
      File checkFile = LittleFS.open(filePath, "r");
      
      uint32_t sampleRate = 0;
      uint16_t channels = 0;
      uint16_t bitsPerSample = 0;
      
      if (!validateWavFile(checkFile, sampleRate, channels, bitsPerSample)) {
        checkFile.close();
        LittleFS.remove(filePath);
        Serial.println("[Upload] ERROR: Invalid WAV format");
        request->send(400, "application/json", "{\"success\":false,\"message\":\"Invalid WAV format\"}");
        broadcastUploadComplete(uploadPad, false, "Invalid WAV format");
        uploadPad = -1;
        uploadFilename = "";
        uploadSize = 0;
        uploadReceived = 0;
        uploadError = false;
        uploadErrorMsg = "";
        return;
      }
      
      checkFile.close();
      
      Serial.printf("[Upload] ✓ Valid WAV: %dHz, %dch, %dbit\n", sampleRate, channels, bitsPerSample);
      
      // Cargar sample en el pad
      bool loaded = sampleManager.loadSample(filePath.c_str(), uploadPad);
      
      if (loaded) {
        Serial.printf("[Upload] ✓ Sample loaded to pad %d\n", uploadPad);
        Serial.println("╔═══════════════════════════════════════════════╗");
        Serial.println("║       ✅ UPLOAD COMPLETADO CON ÉXITO         ║");
        Serial.println("╚═══════════════════════════════════════════════╝\n");
        
        // Enviar respuesta HTTP exitosa
        request->send(200, "application/json", "{\"success\":true,\"message\":\"Sample uploaded successfully\"}");
        
        broadcastUploadComplete(uploadPad, true, "Sample uploaded and loaded successfully");
        
        // Broadcast state update
        broadcastSequencerState();
      } else {
        Serial.println("[Upload] ERROR: Failed to load sample");
        LittleFS.remove(filePath);
        
        // Enviar respuesta HTTP de error
        request->send(500, "application/json", "{\"success\":false,\"message\":\"Failed to load sample\"}");
        
        broadcastUploadComplete(uploadPad, false, "Failed to load sample");
      }
    }
    
    // Reset variables
    uploadPad = -1;
    uploadFilename = "";
    uploadSize = 0;
    uploadReceived = 0;
    uploadError = false;
    uploadErrorMsg = "";
  }
}

bool WebInterface::validateWavFile(File& file, uint32_t& sampleRate, uint16_t& channels, uint16_t& bitsPerSample) {
  if (!file || file.size() < 44) {
    return false;
  }
  
  file.seek(0);
  uint8_t header[44];
  if (file.read(header, 44) != 44) {
    return false;
  }
  
  // Verificar firma RIFF
  if (memcmp(header, "RIFF", 4) != 0 || memcmp(header + 8, "WAVE", 4) != 0) {
    return false;
  }
  
  // Extraer parámetros
  channels = header[22] | (header[23] << 8);
  sampleRate = header[24] | (header[25] << 8) | (header[26] << 16) | (header[27] << 24);
  bitsPerSample = header[34] | (header[35] << 8);
  
  // Validar parámetros
  if (sampleRate != 44100 && sampleRate != 48000) {
    Serial.printf("[Validate] Invalid sample rate: %d (expected 44100 or 48000)\n", sampleRate);
    return false;
  }
  
  if (channels < 1 || channels > 2) {
    Serial.printf("[Validate] Invalid channels: %d (expected 1 or 2)\n", channels);
    return false;
  }
  
  if (bitsPerSample != 16) {
    Serial.printf("[Validate] Invalid bit depth: %d (expected 16)\n", bitsPerSample);
    return false;
  }
  
  return true;
}

void WebInterface::broadcastUploadProgress(int pad, int percent) {
  if (!initialized || !ws) return;
  
  StaticJsonDocument<128> doc;
  doc["type"] = "uploadProgress";
  doc["pad"] = pad;
  doc["percent"] = percent;
  
  String output;
  serializeJson(doc, output);
  ws->textAll(output);
}

void WebInterface::broadcastUploadComplete(int pad, bool success, const String& message) {
  if (!initialized || !ws) return;
  
  StaticJsonDocument<256> doc;
  doc["type"] = "uploadComplete";
  doc["pad"] = pad;
  doc["success"] = success;
  doc["message"] = message;
  
  String output;
  serializeJson(doc, output);
  ws->textAll(output);
}

