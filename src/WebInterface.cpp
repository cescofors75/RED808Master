/*
 * WebInterface.cpp
 * RED808 Web Interface con WebSockets
 */

#include "WebInterface.h"
#include "SPIMaster.h"
#include "Sequencer.h"
#include "KitManager.h"
#include "SampleManager.h"
#include <map>
#include <esp_wifi.h>

// Timeout para clientes UDP (30 segundos sin actividad)
#define UDP_CLIENT_TIMEOUT 30000

extern SPIMaster spiMaster;
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

static void populateStateDocument(DynamicJsonDocument& doc) {
  doc["type"] = "state";
  doc["playing"] = sequencer.isPlaying();
  doc["tempo"] = sequencer.getTempo();
  doc["pattern"] = sequencer.getCurrentPattern();
  doc["step"] = sequencer.getCurrentStep();
  doc["sequencerVolume"] = spiMaster.getSequencerVolume();
  doc["liveVolume"] = spiMaster.getLiveVolume();
  doc["samplesLoaded"] = sampleManager.getLoadedSamplesCount();
  doc["memoryUsed"] = sampleManager.getTotalMemoryUsed();
  doc["psramFree"] = sampleManager.getFreePSRAM();
  doc["songMode"] = sequencer.isSongMode();
  doc["songLength"] = sequencer.getSongLength();
  doc["humanizeTimingMs"] = sequencer.getHumanizeTimingMs();
  doc["humanizeVelocity"] = sequencer.getHumanizeVelocityAmount();
  doc["heap"] = ESP.getFreeHeap();

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
  
  JsonArray trackVolumes = doc.createNestedArray("trackVolumes");
  for (int track = 0; track < MAX_TRACKS; track++) {
    trackVolumes.add(sequencer.getTrackVolume(track));
  }

  // Compact samples: only send loaded sample info (not empty pads)
  JsonArray sampleArray = doc.createNestedArray("samples");
  for (int pad = 0; pad < MAX_SAMPLES; pad++) {
    bool loaded = sampleManager.isSampleLoaded(pad);
    if (loaded) {
      JsonObject sampleObj = sampleArray.createNestedObject();
      sampleObj["pad"] = pad;
      sampleObj["loaded"] = true;
      const char* name = sampleManager.getSampleName(pad);
      sampleObj["name"] = name ? name : "";
      sampleObj["size"] = sampleManager.getSampleLength(pad) * 2;
      sampleObj["format"] = detectSampleFormat(name);
    }
  }
  
  // Send pad filter states (for live pads)
  JsonArray padFilters = doc.createNestedArray("padFilters");
  for (int pad = 0; pad < 16; pad++) {
    FilterType filterType = spiMaster.getPadFilter(pad);
    padFilters.add((int)filterType);
  }
  
  // Send track filter states (for sequencer tracks)
  JsonArray trackFilters = doc.createNestedArray("trackFilters");
  for (int track = 0; track < 16; track++) {
    FilterType filterType = spiMaster.getTrackFilter(track);
    trackFilters.add((int)filterType);
  }
}

static bool isClientReady(AsyncWebSocketClient* client) {
  return client != nullptr && client->status() == WS_CONNECTED;
}

// Cache of sample counts per family — avoid scanning filesystem in WS callback
static int cachedSampleCounts[16] = {-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1};
static const char* sampleFamilies[] = {"BD", "SD", "CH", "OH", "CP", "CB", "RS", "CL", "MA", "CY", "HT", "LT", "MC", "MT", "HC", "LC"};

static void rebuildSampleCountCache() {
  for (int i = 0; i < 16; i++) {
    String path = String("/") + String(sampleFamilies[i]);
    int count = 0;
    
    File dir = LittleFS.open(path);
    if (dir && dir.isDirectory()) {
      File file = dir.openNextFile();
      while (file) {
        if (!file.isDirectory()) {
          String fullName = file.name();
          int lastSlash = fullName.lastIndexOf('/');
          String fileName = lastSlash >= 0 ? fullName.substring(lastSlash + 1) : fullName;
          if (isSupportedSampleFile(fileName)) {
            count++;
          }
        }
        file.close();
        file = dir.openNextFile();
      }
      dir.close();
    }
    cachedSampleCounts[i] = count;
  }
  Serial.println("[SampleCount] Cache rebuilt");
}

static void sendSampleCounts(AsyncWebSocketClient* client) {
  if (!client || !isClientReady(client)) {
    Serial.println("[sendSampleCounts] Client not ready");
    return;
  }
  
  // Build cache on first request
  if (cachedSampleCounts[0] < 0) {
    rebuildSampleCountCache();
  }
  
  StaticJsonDocument<512> sampleCountDoc;
  sampleCountDoc["type"] = "sampleCounts";
  
  for (int i = 0; i < 16; i++) {
    sampleCountDoc[sampleFamilies[i]] = cachedSampleCounts[i];
  }
  
  String countOutput;
  serializeJson(sampleCountDoc, countOutput);
  
  if (isClientReady(client)) {
    client->text(countOutput);
  }
}

WebInterface::WebInterface() {
  server = nullptr;
  ws = nullptr;
  initialized = false;
  midiController = nullptr;
  
  // Inicializar variables de rate limiting
  lastTriggerTime = 0;
  lastStepChangeTime = 0;
  lastBroadcastTime = 0;
  _staConnected = false;
}

WebInterface::~WebInterface() {
  if (server) delete server;
  if (ws) delete ws;
}

bool WebInterface::begin(const char* apSsid, const char* apPassword,
                         const char* staSSID, const char* staPassword,
                         unsigned long staTimeoutMs) {
  Serial.println("  Configurando WiFi...");
  _staConnected = false;
  
  // Desactivar ahorro de energía WiFi
  WiFi.setSleep(false);
  WiFi.mode(WIFI_OFF);
  delay(50);
  
  // Potencia TX máxima
  WiFi.setTxPower(WIFI_POWER_19_5dBm);
  
  // ── Intentar modo STA (red doméstica) si se proporcionó SSID ──
  if (staSSID && strlen(staSSID) > 0) {
    Serial.printf("  [WiFi] Intentando AP+STA → %s ...\n", staSSID);
    WiFi.mode(WIFI_AP_STA);
    
    // Configurar AP primero (siempre disponible)
    IPAddress local_IP(192, 168, 4, 1);
    IPAddress gateway(192, 168, 4, 1);
    IPAddress subnet(255, 255, 255, 0);
    WiFi.softAPConfig(local_IP, gateway, subnet);
    WiFi.softAP(apSsid, apPassword, 1, 0, 4);
    delay(100);
    
    // Protocolo b/g/n y beacon para AP
    esp_wifi_set_protocol(WIFI_IF_AP, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N);
    wifi_config_t conf;
    esp_wifi_get_config(WIFI_IF_AP, &conf);
    conf.ap.beacon_interval = 100;
    esp_wifi_set_config(WIFI_IF_AP, &conf);
    
    Serial.printf("  [WiFi] ✓ AP activo → %s (IP: %s)\n", apSsid, WiFi.softAPIP().toString().c_str());
    
    // Ahora conectar a red doméstica
    WiFi.begin(staSSID, staPassword);
    
    unsigned long t0 = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - t0) < staTimeoutMs) {
      delay(250);
      Serial.print(".");
    }
    Serial.println();
    
    if (WiFi.status() == WL_CONNECTED) {
      _staConnected = true;
      WiFi.setSleep(false);
      
      // Reconfigure AP to match STA channel (avoids channel conflicts in AP+STA mode)
      uint8_t staChannel = WiFi.channel();
      WiFi.softAP(apSsid, apPassword, staChannel, 0, 4);
      Serial.printf("  [WiFi] ✓ STA conectado! IP: %s (ch:%d)\n", WiFi.localIP().toString().c_str(), staChannel);
      Serial.printf("  [WiFi]   Gateway: %s  RSSI: %d dBm\n",
                    WiFi.gatewayIP().toString().c_str(), WiFi.RSSI());
      Serial.printf("  [WiFi]   AP reconfigured to channel %d to match STA\n", staChannel);
      Serial.printf("  [WiFi]   Modo AP+STA: Surface→%s:%s  PC→%s\n",
                    apSsid, WiFi.softAPIP().toString().c_str(),
                    WiFi.localIP().toString().c_str());
    } else {
      Serial.println("  [WiFi] ✗ STA falló — AP sigue activo");
      // AP ya está corriendo, no hay que hacer nada más
    }
  }
  
  // ── Fallback: modo AP solo (sin SSID doméstico configurado) ──
  if (!_staConnected && !(staSSID && strlen(staSSID) > 0)) {
    Serial.printf("  [WiFi] Creando AP: %s\n", apSsid);
    WiFi.mode(WIFI_AP);
    delay(50);
    
    IPAddress local_IP(192, 168, 4, 1);
    IPAddress gateway(192, 168, 4, 1);
    IPAddress subnet(255, 255, 255, 0);
    WiFi.softAPConfig(local_IP, gateway, subnet);
    
    WiFi.softAP(apSsid, apPassword, 1, 0, 4);
    delay(200);
    
    // Protocolo b/g/n
    esp_wifi_set_protocol(WIFI_IF_AP, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N);
    
    wifi_config_t conf;
    esp_wifi_get_config(WIFI_IF_AP, &conf);
    conf.ap.beacon_interval = 100;
    esp_wifi_set_config(WIFI_IF_AP, &conf);
    
    WiFi.setSleep(false);
    
    Serial.printf("  [WiFi] ✓ AP activo → %s\n", WiFi.softAPIP().toString().c_str());
  }
  
  // Crear servidor web
  server = new AsyncWebServer(80);
  ws = new AsyncWebSocket("/ws");
  
  // WebSocket handler
  ws->onEvent([this](AsyncWebSocket *server, AsyncWebSocketClient *client, 
                     AwsEventType type, void *arg, uint8_t *data, size_t len) {
    this->onWebSocketEvent(server, client, type, arg, data, len);
  });
  
  server->addHandler(ws);
  
  // Servir archivos grandes con gzip explícito para máximo rendimiento
  server->on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    if (LittleFS.exists("/web/index.html.gz")) {
      AsyncWebServerResponse *response = request->beginResponse(LittleFS, "/web/index.html.gz", "text/html");
      response->addHeader("Content-Encoding", "gzip");
      response->addHeader("Cache-Control", "max-age=86400");
      request->send(response);
    } else {
      request->send(LittleFS, "/web/index.html", "text/html");
    }
  });
  
  server->on("/index.html", HTTP_GET, [](AsyncWebServerRequest *request){
    if (LittleFS.exists("/web/index.html.gz")) {
      AsyncWebServerResponse *response = request->beginResponse(LittleFS, "/web/index.html.gz", "text/html");
      response->addHeader("Content-Encoding", "gzip");
      response->addHeader("Cache-Control", "max-age=86400");
      request->send(response);
    } else {
      request->send(LittleFS, "/web/index.html", "text/html");
    }
  });
  
  server->on("/app.js", HTTP_GET, [](AsyncWebServerRequest *request){
    if (LittleFS.exists("/web/app.js.gz")) {
      AsyncWebServerResponse *response = request->beginResponse(LittleFS, "/web/app.js.gz", "application/javascript");
      response->addHeader("Content-Encoding", "gzip");
      response->addHeader("Cache-Control", "max-age=86400");
      request->send(response);
    } else {
      request->send(LittleFS, "/web/app.js", "application/javascript");
    }
  });
  
  server->on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request){
    if (LittleFS.exists("/web/style.css.gz")) {
      AsyncWebServerResponse *response = request->beginResponse(LittleFS, "/web/style.css.gz", "text/css");
      response->addHeader("Content-Encoding", "gzip");
      response->addHeader("Cache-Control", "max-age=86400");
      request->send(response);
    } else {
      request->send(LittleFS, "/web/style.css", "text/css");
    }
  });
  
  server->on("/keyboard-controls.js", HTTP_GET, [](AsyncWebServerRequest *request){
    if (LittleFS.exists("/web/keyboard-controls.js.gz")) {
      AsyncWebServerResponse *response = request->beginResponse(LittleFS, "/web/keyboard-controls.js.gz", "application/javascript");
      response->addHeader("Content-Encoding", "gzip");
      response->addHeader("Cache-Control", "max-age=86400");
      request->send(response);
    } else {
      request->send(LittleFS, "/web/keyboard-controls.js", "application/javascript");
    }
  });
  
  server->on("/keyboard-styles.css", HTTP_GET, [](AsyncWebServerRequest *request){
    if (LittleFS.exists("/web/keyboard-styles.css.gz")) {
      AsyncWebServerResponse *response = request->beginResponse(LittleFS, "/web/keyboard-styles.css.gz", "text/css");
      response->addHeader("Content-Encoding", "gzip");
      response->addHeader("Cache-Control", "max-age=86400");
      request->send(response);
    } else {
      request->send(LittleFS, "/web/keyboard-styles.css", "text/css");
    }
  });
  
  server->on("/midi-import.js", HTTP_GET, [](AsyncWebServerRequest *request){
    if (LittleFS.exists("/web/midi-import.js.gz")) {
      AsyncWebServerResponse *response = request->beginResponse(LittleFS, "/web/midi-import.js.gz", "application/javascript");
      response->addHeader("Content-Encoding", "gzip");
      response->addHeader("Cache-Control", "max-age=86400");
      request->send(response);
    } else {
      request->send(LittleFS, "/web/midi-import.js", "application/javascript");
    }
  });
  
  server->on("/chat-agent.js", HTTP_GET, [](AsyncWebServerRequest *request){
    if (LittleFS.exists("/web/chat-agent.js.gz")) {
      AsyncWebServerResponse *response = request->beginResponse(LittleFS, "/web/chat-agent.js.gz", "application/javascript");
      response->addHeader("Content-Encoding", "gzip");
      response->addHeader("Cache-Control", "no-cache");
      request->send(response);
    } else {
      request->send(LittleFS, "/web/chat-agent.js", "application/javascript");
    }
  });
  
  server->on("/waveform-visualizer.js", HTTP_GET, [](AsyncWebServerRequest *request){
    if (LittleFS.exists("/web/waveform-visualizer.js.gz")) {
      AsyncWebServerResponse *response = request->beginResponse(LittleFS, "/web/waveform-visualizer.js.gz", "application/javascript");
      response->addHeader("Content-Encoding", "gzip");
      response->addHeader("Cache-Control", "no-cache");
      request->send(response);
    } else {
      request->send(LittleFS, "/web/waveform-visualizer.js", "application/javascript");
    }
  });
  
  // Patchbay page
  server->on("/patchbay", HTTP_GET, [](AsyncWebServerRequest *request){
    if (LittleFS.exists("/web/patchbay.html.gz")) {
      AsyncWebServerResponse *response = request->beginResponse(LittleFS, "/web/patchbay.html.gz", "text/html");
      response->addHeader("Content-Encoding", "gzip");
      response->addHeader("Cache-Control", "no-cache");
      request->send(response);
    } else {
      request->send(LittleFS, "/web/patchbay.html", "text/html");
    }
  });
  
  server->on("/patchbay.css", HTTP_GET, [](AsyncWebServerRequest *request){
    if (LittleFS.exists("/web/patchbay.css.gz")) {
      AsyncWebServerResponse *response = request->beginResponse(LittleFS, "/web/patchbay.css.gz", "text/css");
      response->addHeader("Content-Encoding", "gzip");
      response->addHeader("Cache-Control", "no-cache");
      request->send(response);
    } else {
      request->send(LittleFS, "/web/patchbay.css", "text/css");
    }
  });
  
  server->on("/patchbay.js", HTTP_GET, [](AsyncWebServerRequest *request){
    if (LittleFS.exists("/web/patchbay.js.gz")) {
      AsyncWebServerResponse *response = request->beginResponse(LittleFS, "/web/patchbay.js.gz", "application/javascript");
      response->addHeader("Content-Encoding", "gzip");
      response->addHeader("Cache-Control", "no-cache");
      request->send(response);
    } else {
      request->send(LittleFS, "/web/patchbay.js", "application/javascript");
    }
  });

  // Multiview page — redirect to .html served by serveStatic (avoids AsyncFileResponse 500 edge case)
  server->on("/multiview", HTTP_GET, [](AsyncWebServerRequest *request){
    request->redirect("/multiview.html");
  });

  server->on("/multiview.css", HTTP_GET, [](AsyncWebServerRequest *request){
    if (LittleFS.exists("/web/multiview.css.gz")) {
      AsyncWebServerResponse *response = request->beginResponse(LittleFS, "/web/multiview.css.gz", "text/css");
      response->addHeader("Content-Encoding", "gzip");
      response->addHeader("Cache-Control", "no-cache");
      request->send(response);
    } else {
      request->send(LittleFS, "/web/multiview.css", "text/css");
    }
  });

  server->on("/multiview.js", HTTP_GET, [](AsyncWebServerRequest *request){
    if (LittleFS.exists("/web/multiview.js.gz")) {
      AsyncWebServerResponse *response = request->beginResponse(LittleFS, "/web/multiview.js.gz", "application/javascript");
      response->addHeader("Content-Encoding", "gzip");
      response->addHeader("Cache-Control", "no-cache");
      request->send(response);
    } else {
      request->send(LittleFS, "/web/multiview.js", "application/javascript");
    }
  });

  // Admin page
  server->on("/adm", HTTP_GET, [](AsyncWebServerRequest *request){
    if (LittleFS.exists("/web/admin.html.gz")) {
      AsyncWebServerResponse *response = request->beginResponse(LittleFS, "/web/admin.html.gz", "text/html");
      response->addHeader("Content-Encoding", "gzip");
      response->addHeader("Cache-Control", "max-age=600");
      request->send(response);
    } else {
      request->send(LittleFS, "/web/admin.html", "text/html");
    }
  });
  
  // Fallback para otros archivos estáticos (favicon, etc)
  server->serveStatic("/", LittleFS, "/web/")
    .setCacheControl("max-age=86400");
  
  // API REST
  server->on("/api/trigger", HTTP_POST, [](AsyncWebServerRequest *request){
    if (request->hasParam("pad", true)) {
      int pad = request->getParam("pad", true)->value().toInt();
      Serial.printf("[API] /api/trigger POST pad=%d\n", pad);
      triggerPadWithLED(pad, 127);  // Enciende LED RGB
      request->send(200, "text/plain", "OK");
    } else {
      request->send(400, "text/plain", "Missing pad parameter");
    }
  });

  server->on("/api/trigger", HTTP_GET, [](AsyncWebServerRequest *request){
    if (request->hasParam("pad")) {
      int pad = request->getParam("pad")->value().toInt();
      Serial.printf("[API] /api/trigger GET pad=%d\n", pad);
      triggerPadWithLED(pad, 127);
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
    DynamicJsonDocument doc(4096);
    
    for (int track = 0; track < 16; track++) {
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
    
    // Info de WiFi (adaptado a STA o AP)
    extern WebInterface webInterface;
    if (webInterface.isSTAMode()) {
      doc["wifiMode"] = "STA";
      doc["ssid"] = WiFi.SSID();
      doc["ip"] = WiFi.localIP().toString();
      doc["gateway"] = WiFi.gatewayIP().toString();
      doc["rssi"] = WiFi.RSSI();
      doc["channel"] = WiFi.channel();
      doc["txPower"] = "19.5dBm";
    } else {
      doc["wifiMode"] = "AP";
      doc["ssid"] = WiFi.softAPSSID();
      doc["ip"] = WiFi.softAPIP().toString();
      doc["channel"] = WiFi.channel();
      doc["txPower"] = "19.5dBm";
      doc["connectedStations"] = WiFi.softAPgetStationNum();
    }
    
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
    // Serial.printf("[sysinfo] UDP clients count: %d\n", udpClients.size()); // Comentado
    doc["udpClients"] = udpClients.size();
    JsonArray udpClientsList = doc.createNestedArray("udpClientList");
    unsigned long now = millis();
    for (const auto& pair : udpClients) {
      JsonObject c = udpClientsList.createNestedObject();
      c["ip"] = pair.second.ip.toString();
      c["port"] = pair.second.port;
      c["lastSeen"] = (now - pair.second.lastSeen) / 1000;
      c["packets"] = pair.second.packetCount;
      // Serial.printf("[sysinfo] Adding UDP client: %s:%d (packets: %d)\n", // Comentado
      //               pair.second.ip.toString().c_str(), pair.second.port, pair.second.packetCount);
    }
    
    // Info del secuenciador
    doc["tempo"] = sequencer.getTempo();
    doc["playing"] = sequencer.isPlaying();
    doc["pattern"] = sequencer.getCurrentPattern();
    doc["samplesLoaded"] = sampleManager.getLoadedSamplesCount();
    doc["memoryUsed"] = sampleManager.getTotalMemoryUsed();
    
    // Info MIDI USB
    if (midiController) {
      MIDIDeviceInfo midiInfo = midiController->getDeviceInfo();
      doc["midiEnabled"] = true;
      doc["midiConnected"] = midiInfo.connected;
      if (midiInfo.connected) {
        doc["midiDevice"] = midiInfo.deviceName;
        doc["midiVendorId"] = String(midiInfo.vendorId, HEX);
        doc["midiProductId"] = String(midiInfo.productId, HEX);
      }
    } else {
      doc["midiEnabled"] = false;
      doc["midiConnected"] = false;
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
  
  // MIDI Mapping endpoints
  server->on("/api/midi/mapping", HTTP_GET, [this](AsyncWebServerRequest *request){
    if (!midiController) {
      request->send(503, "application/json", "{\"error\":\"MIDI disabled\"}");
      return;
    }

    StaticJsonDocument<2048> doc;
    JsonArray mappings = doc.createNestedArray("mappings");
    
    int count = 0;
    const MIDINoteMapping* maps = midiController->getAllMappings(count);
    for (int i = 0; i < count; i++) {
      if (maps[i].enabled) {
        JsonObject mapping = mappings.createNestedObject();
        mapping["note"] = maps[i].note;
        mapping["pad"] = maps[i].pad;
      }
    }
    
    String output;
    serializeJson(doc, output);
    request->send(200, "application/json", output);
  });
  
  server->on("/api/midi/mapping", HTTP_POST, [this](AsyncWebServerRequest *request){}, NULL,
    [this](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){
      if (!midiController) {
        request->send(503, "application/json", "{\"error\":\"MIDI disabled\"}");
        return;
      }

      StaticJsonDocument<512> doc;
      DeserializationError error = deserializeJson(doc, data, len);
      
      if (error) {
        request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
        return;
      }
      
      // Procesar mappings
      if (doc.containsKey("note") && doc.containsKey("pad")) {
        uint8_t note = doc["note"];
        int8_t pad = doc["pad"];
        midiController->setPadMapping(pad, note);  // busca por pad, actualiza nota
        request->send(200, "application/json", "{\"success\":true}");
      } else if (doc.containsKey("reset") && doc["reset"] == true) {
        midiController->resetToDefaultMapping();
        request->send(200, "application/json", "{\"success\":true,\"message\":\"Mapping reset to default\"}");
      } else {
        request->send(400, "application/json", "{\"error\":\"Missing parameters\"}");
      }
    }
  );
  
  // Endpoint para obtener forma de onda de un sample cargado (para visualizador)
  server->on("/api/waveform", HTTP_GET, [](AsyncWebServerRequest *request){
    // Mode 1: waveform from loaded pad (?pad=N)
    // Mode 2: waveform from file on LittleFS (?file=/family/name.wav)
    
    int points = 200;
    if (request->hasParam("points")) {
      points = request->getParam("points")->value().toInt();
      if (points < 20) points = 20;
      if (points > 400) points = 400;
    }
    
    // === MODE 2: From file path (for preview before loading) ===
    if (request->hasParam("file")) {
      String filePath = request->getParam("file")->value();
      if (!filePath.startsWith("/")) filePath = "/" + filePath;
      
      File file = LittleFS.open(filePath, "r");
      if (!file) {
        request->send(404, "application/json", "{\"error\":\"File not found\"}");
        return;
      }
      
      size_t fileSize = file.size();
      
      // Parse WAV header to find data chunk
      uint32_t dataOffset = 0;
      uint32_t dataSize = 0;
      uint16_t numChannels = 1;
      uint16_t bitsPerSample = 16;
      uint32_t sampleRate = 44100;
      
      String fname = filePath;
      fname.toLowerCase();
      
      if (fname.endsWith(".wav")) {
        // Read WAV header
        uint8_t header[44];
        file.seek(0);
        if (file.read(header, 44) == 44 && memcmp(header, "RIFF", 4) == 0) {
          numChannels = header[22] | (header[23] << 8);
          sampleRate = header[24] | (header[25] << 8) | (header[26] << 16) | (header[27] << 24);
          bitsPerSample = header[34] | (header[35] << 8);
          
          // Find data chunk (with safety limit to prevent infinite loop)
          file.seek(36);
          int chunkSearchLimit = 20;  // Max 20 chunks to search
          while (file.available() >= 8 && chunkSearchLimit-- > 0) {
            char chunkId[4];
            uint32_t chunkSize;
            if (file.read((uint8_t*)chunkId, 4) != 4) break;
            if (file.read((uint8_t*)&chunkSize, 4) != 4) break;
            if (memcmp(chunkId, "data", 4) == 0) {
              dataOffset = file.position();
              dataSize = chunkSize;
              break;
            }
            if (chunkSize == 0) break;  // Prevent infinite loop on corrupted WAV
            file.seek(file.position() + chunkSize);
          }
        }
        if (dataSize == 0) {
          file.close();
          request->send(400, "application/json", "{\"error\":\"Invalid WAV\"}");
          return;
        }
      } else {
        // RAW file - whole file is data
        dataOffset = 0;
        dataSize = fileSize;
      }
      
      uint32_t bytesPerSample = bitsPerSample / 8;
      uint32_t totalSamples = dataSize / bytesPerSample;
      if (numChannels == 2) totalSamples /= 2;
      
      uint32_t samplesPerPoint = totalSamples / points;
      if (samplesPerPoint == 0) samplesPerPoint = 1;
      int actualPoints = totalSamples / samplesPerPoint;
      if (actualPoints > points) actualPoints = points;
      
      float durationMs = (totalSamples * 1000.0f) / sampleRate;
      
      // Extract filename from path
      String name = filePath;
      int lastSlash = name.lastIndexOf('/');
      if (lastSlash >= 0) name = name.substring(lastSlash + 1);
      
      // Build response while reading file in chunks
      // Pre-reserve String to avoid repeated reallocs (~14 bytes per point)
      String json;
      json.reserve(200 + actualPoints * 14);
      json = "{\"file\":\"";
      json += name;
      json += "\",\"samples\":";
      json += totalSamples;
      json += ",\"duration\":";
      json += String(durationMs, 1);
      json += ",\"rate\":";
      json += sampleRate;
      json += ",\"points\":";
      json += actualPoints;
      json += ",\"peaks\":[";
      
      // Read peaks in small chunks — heap allocated to avoid stack overflow in async handler
      const int CHUNK_SAMPLES = 256;  // Reduced for safety
      int16_t* chunkBuf = (int16_t*)malloc(CHUNK_SAMPLES * 2 * sizeof(int16_t));
      if (!chunkBuf) {
        file.close();
        request->send(500, "application/json", "{\"error\":\"Memory\"}");
        return;
      }
      
      file.seek(dataOffset);
      
      for (int p = 0; p < actualPoints; p++) {
        int16_t maxVal = 0, minVal = 0;
        uint32_t remaining = samplesPerPoint;
        
        while (remaining > 0) {
          uint32_t toRead = remaining;
          if (toRead > CHUNK_SAMPLES) toRead = CHUNK_SAMPLES;
          
          size_t bytesToRead = toRead * bytesPerSample * numChannels;
          size_t bytesRead = file.read((uint8_t*)chunkBuf, bytesToRead);
          if (bytesRead == 0) break;
          
          uint32_t samplesRead = bytesRead / (bytesPerSample * numChannels);
          
          for (uint32_t j = 0; j < samplesRead; j++) {
            int16_t s;
            if (numChannels == 2) {
              s = (chunkBuf[j * 2] / 2) + (chunkBuf[j * 2 + 1] / 2);
            } else {
              s = chunkBuf[j];
            }
            if (s > maxVal) maxVal = s;
            if (s < minVal) minVal = s;
          }
          remaining -= samplesRead;
        }
        
        if (p > 0) json += ",";
        json += "[";
        json += (int)(maxVal >> 8);
        json += ",";
        json += (int)(minVal >> 8);
        json += "]";
        
        if (p % 10 == 9) yield();
      }
      json += "]}";
      
      free(chunkBuf);
      file.close();
      request->send(200, "application/json", json);
      return;
    }
    
    // === MODE 1: From loaded pad ===
    if (!request->hasParam("pad")) {
      request->send(400, "application/json", "{\"error\":\"Missing pad or file parameter\"}");
      return;
    }
    int pad = request->getParam("pad")->value().toInt();
    if (pad < 0 || pad >= MAX_SAMPLES) {
      request->send(400, "application/json", "{\"error\":\"Invalid pad\"}");
      return;
    }
    if (!sampleManager.isSampleLoaded(pad)) {
      request->send(404, "application/json", "{\"error\":\"No sample loaded\"}");
      return;
    }
    
    // 'points' already declared at top of lambda
    
    // Get waveform peaks (pairs: max, min per point)
    int8_t* peaks = (int8_t*)malloc(points * 2);
    if (!peaks) {
      request->send(500, "application/json", "{\"error\":\"Memory\"}");
      return;
    }
    
    int actualPoints = sampleManager.getWaveformPeaks(pad, peaks, points);
    
    // Build compact JSON response
    uint32_t sampleLen = sampleManager.getSampleLength(pad);
    float durationMs = (sampleLen * 1000.0f) / SAMPLE_RATE;
    
    // Use chunked response to avoid large buffer allocation
    String json = "{\"pad\":";
    json += pad;
    json += ",\"name\":\"";
    json += sampleManager.getSampleName(pad);
    json += "\",\"samples\":";
    json += sampleLen;
    json += ",\"duration\":";
    json += String(durationMs, 1);
    json += ",\"points\":";
    json += actualPoints;
    json += ",\"peaks\":[";
    
    for (int i = 0; i < actualPoints; i++) {
      if (i > 0) json += ",";
      json += "[";
      json += (int)peaks[i * 2];      // max (positive)
      json += ",";
      json += (int)peaks[i * 2 + 1];  // min (negative)
      json += "]";
      if (i % 20 == 19) yield(); // Yield cada 20 puntos
    }
    json += "]}";
    
    free(peaks);
    request->send(200, "application/json", json);
  });

  // Endpoint para descargar audio RAW del pad cargado (PCM 16-bit mono 44100Hz)
  server->on("/api/sampledata", HTTP_GET, [](AsyncWebServerRequest *request){
    if (!request->hasParam("pad")) {
      request->send(400, "application/json", "{\"error\":\"Missing pad parameter\"}");
      return;
    }
    int pad = request->getParam("pad")->value().toInt();
    if (pad < 0 || pad >= MAX_SAMPLES) {
      request->send(400, "application/json", "{\"error\":\"Invalid pad\"}");
      return;
    }
    if (!sampleManager.isSampleLoaded(pad)) {
      request->send(404, "application/json", "{\"error\":\"No sample loaded\"}");
      return;
    }

    int16_t* buffer = sampleManager.getSampleBuffer(pad);
    uint32_t length = sampleManager.getSampleLength(pad);
    if (!buffer || length == 0) {
      request->send(404, "application/json", "{\"error\":\"Empty sample\"}");
      return;
    }

    // Build WAV header + stream PCM data
    uint32_t dataSize = length * 2; // 16-bit = 2 bytes per sample
    uint32_t fileSize = 44 + dataSize;

    AsyncWebServerResponse *response = request->beginChunkedResponse("audio/wav",
      [buffer, length, dataSize, fileSize](uint8_t *buf, size_t maxLen, size_t index) -> size_t {
        if (index >= fileSize) return 0;

        size_t written = 0;

        // Write WAV header (first 44 bytes)
        if (index < 44) {
          uint8_t header[44];
          memcpy(header, "RIFF", 4);
          uint32_t riffSize = fileSize - 8;
          memcpy(header + 4, &riffSize, 4);
          memcpy(header + 8, "WAVE", 4);
          memcpy(header + 12, "fmt ", 4);
          uint32_t fmtSize = 16;
          memcpy(header + 16, &fmtSize, 4);
          uint16_t audioFormat = 1; // PCM
          memcpy(header + 20, &audioFormat, 2);
          uint16_t numChannels = 1;
          memcpy(header + 22, &numChannels, 2);
          uint32_t sampleRate = 44100;
          memcpy(header + 24, &sampleRate, 4);
          uint32_t byteRate = 44100 * 2;
          memcpy(header + 28, &byteRate, 4);
          uint16_t blockAlign = 2;
          memcpy(header + 32, &blockAlign, 2);
          uint16_t bitsPerSample = 16;
          memcpy(header + 34, &bitsPerSample, 2);
          memcpy(header + 36, "data", 4);
          memcpy(header + 40, &dataSize, 4);

          size_t headerBytesToCopy = 44 - (size_t)index;
          if (headerBytesToCopy > maxLen) headerBytesToCopy = maxLen;
          memcpy(buf, header + index, headerBytesToCopy);
          written += headerBytesToCopy;
        }

        // Write PCM data
        if (index + written >= 44 && written < maxLen) {
          size_t pcmOffset = (index + written) - 44;
          size_t pcmRemaining = dataSize - pcmOffset;
          size_t toCopy = maxLen - written;
          if (toCopy > pcmRemaining) toCopy = pcmRemaining;
          memcpy(buf + written, ((uint8_t*)buffer) + pcmOffset, toCopy);
          written += toCopy;
        }

        return written;
      }
    );

    response->addHeader("Access-Control-Allow-Origin", "*");
    response->addHeader("Content-Disposition", "inline");
    request->send(response);
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
    // ⚠️ LÍMITE DE 3 CLIENTES para estabilidad
    if (ws->count() > 3) {
      Serial.printf("[WS] MAX CLIENTS reached, rejecting #%u\n", client->id());
      client->close(1008, "Max clients reached");
      return;
    }
    
    Serial.printf("[WS] Client #%u connected (%u total)\n", client->id(), ws->count());
    
    StaticJsonDocument<512> basicState;
    basicState["type"] = "connected";
    basicState["playing"] = sequencer.isPlaying();
    basicState["tempo"] = sequencer.getTempo();
    basicState["pattern"] = sequencer.getCurrentPattern();
    basicState["clientId"] = client->id();
    
    String output;
    serializeJson(basicState, output);
    
    if (isClientReady(client)) {
      client->text(output);
    }
  } else if (type == WS_EVT_DISCONNECT) {
    unsigned int remaining = ws->count();
    Serial.printf("[WS] Client #%u disconnected (%u remaining)\n", client->id(), remaining > 0 ? remaining - 1 : 0);
  } else if (type == WS_EVT_DATA) {
    AwsFrameInfo *info = (AwsFrameInfo*)arg;
    
    // --- Frame reassembly for fragmented WebSocket messages ---
    // NOTE: static vars are per-task (single network task on ESP32), but we track
    // the client id to detect cross-client fragment interleaving and discard safely.
    static uint8_t* _wsReassemblyBuf = nullptr;
    static size_t   _wsReassemblySize = 0;
    static uint32_t _wsReassemblyClientId = 0xFFFFFFFF;
    bool _wsFreeAfter = false;
    
    // Safe buffer variables (used for WS_TEXT to avoid data[len]=0 overflow)
    char* safeData = nullptr;
    bool safeFreeNeeded = false;
    
    if (!(info->final && info->index == 0 && info->len == len)) {
      // Fragmented frame - reassemble chunks into complete message
      if (info->index == 0) {
        // If a different client already owns the buffer, discard it to avoid corruption
        if (_wsReassemblyBuf && _wsReassemblyClientId != client->id()) {
          Serial.printf("[WS] Reassembly conflict: client %u vs %u — discarding old buf\n",
                        _wsReassemblyClientId, client->id());
          free(_wsReassemblyBuf);
          _wsReassemblyBuf = nullptr;
        } else if (_wsReassemblyBuf) {
          free(_wsReassemblyBuf); _wsReassemblyBuf = nullptr;
        }
        if (ESP.getFreeHeap() < (uint32_t)(info->len + 4096)) {
          Serial.printf("[WS] HEAP too low for reassembly: need %u, free %d\n",
                        info->len, ESP.getFreeHeap());
          return;
        }
        _wsReassemblyBuf = (uint8_t*)malloc(info->len + 2); // +2 for null terminator safety
        if (!_wsReassemblyBuf) {
          Serial.printf("[WS] ALLOC FAIL reassembly: %u bytes\n", info->len);
          return;
        }
        _wsReassemblySize = info->len;
        _wsReassemblyClientId = client->id();
      } else if (_wsReassemblyClientId != client->id()) {
        // Mid-frame data from wrong client — discard silently
        return;
      }
      if (_wsReassemblyBuf && info->index + len <= _wsReassemblySize) {
        memcpy(_wsReassemblyBuf + info->index, data, len);
      }
      if (info->final && (info->index + len) == info->len && _wsReassemblyBuf) {
        _wsReassemblyBuf[_wsReassemblySize] = 0; // Safe null-terminate
        data = _wsReassemblyBuf;
        len = _wsReassemblySize;
        _wsFreeAfter = true;
      } else {
        return; // Still accumulating chunks
      }
    }
      
    Serial.printf("[WS DATA] opcode=%u len=%u final=%u idx=%u total=%u\n",
                  (unsigned)info->opcode, (unsigned)len,
                  (unsigned)info->final, (unsigned)info->index, (unsigned)info->len);

    // 1. MANEJO DE BINARIO (Baja latencia para Triggers)
    if (info->opcode == WS_BINARY) {
      Serial.printf("[WS BIN] len=%d data:", len);
      for (size_t i = 0; i < len && i < 8; i++) Serial.printf(" %02X", data[i]);
      Serial.println();
      // Protocolo: [0x90, PAD, VEL]
      if (len == 3 && data[0] == 0x90) {
         int pad = data[1];
         int velocity = data[2];
         triggerPadWithLED(pad, velocity);
      }
    }
    // 2. MANEJO DE TEXTO (JSON normal)
    else if (info->opcode == WS_TEXT) {
      // Reject if heap critically low — prevent crash during JSON processing
      if (ESP.getFreeHeap() < 15000) {
        Serial.printf("[WS] CRITICAL: Heap=%d, dropping message\n", ESP.getFreeHeap());
        if (_wsFreeAfter && _wsReassemblyBuf) { free(_wsReassemblyBuf); _wsReassemblyBuf = nullptr; }
        return;
      }
      
      // SAFE null-terminate: copy to buffer with extra byte instead of data[len]=0
      if (_wsFreeAfter) {
        // Already in our reassembly buffer which has +2 bytes — already null-terminated above
        safeData = (char*)data;
      } else {
        // Non-fragmented message: copy to safe buffer
        safeData = (char*)malloc(len + 1);
        if (!safeData) {
          Serial.println("[WS] ALLOC FAIL safe buffer");
          return;
        }
        memcpy(safeData, data, len);
        safeData[len] = 0;
        safeFreeNeeded = true;
      }
      // Point data to safe null-terminated buffer for JSON parsing
      data = (uint8_t*)safeData;
        
        // Check for bulk pattern command (needs larger JSON doc)
        if (len > 400 && strstr((char*)data, "\"setBulk\"") != nullptr) {
          DynamicJsonDocument bulkDoc(16384);
          DeserializationError bulkErr = deserializeJson(bulkDoc, (char*)data);
          if (!bulkErr) {
            int pattern = bulkDoc["p"].as<int>();
            // p = -1 means current pattern (single bar import)
            if (pattern < 0) pattern = sequencer.getCurrentPattern();
            if (pattern >= 0 && pattern < MAX_PATTERNS) {
              bool stepsData[16][16] = {};
              uint8_t velsData[16][16];
              memset(velsData, 127, sizeof(velsData));
              
              JsonArray sArr = bulkDoc["s"].as<JsonArray>();
              JsonArray vArr = bulkDoc["v"].as<JsonArray>();
              
              for (int t = 0; t < 16 && t < (int)sArr.size(); t++) {
                JsonArray trackSteps = sArr[t].as<JsonArray>();
                for (int s = 0; s < 16 && s < (int)trackSteps.size(); s++) {
                  stepsData[t][s] = trackSteps[s].as<int>() != 0;
                }
                if (vArr && t < (int)vArr.size()) {
                  JsonArray trackVels = vArr[t].as<JsonArray>();
                  for (int s = 0; s < 16 && s < (int)trackVels.size(); s++) {
                    int v = trackVels[s].as<int>();
                    velsData[t][s] = (v > 0 && v <= 127) ? v : 127;
                  }
                }
              }
              
              sequencer.setPatternBulk(pattern, stepsData, velsData);
              yield();
              
              // Send ACK back to client
              StaticJsonDocument<64> ack;
              ack["type"] = "bulkAck";
              ack["p"] = pattern;
              String ackStr;
              serializeJson(ack, ackStr);
              if (isClientReady(client)) {
                client->text(ackStr);
              }
            }
          } else {
            Serial.printf("[WS] setBulk parse error: %s (len=%d)\n", bulkErr.c_str(), len);
          }
          // Cleanup reassembly buffer before early return
          if (_wsFreeAfter && _wsReassemblyBuf) {
            free(_wsReassemblyBuf);
            _wsReassemblyBuf = nullptr;
          } else if (safeFreeNeeded) {
            free(safeData);
          }
          return; // Already handled
        }

        StaticJsonDocument<512> doc;
        DeserializationError error = deserializeJson(doc, (char*)data);
        
        if (!error) {
          // Procesar comandos comunes primero (start, stop, tempo, etc.)
          processCommand(doc);
          
          // Comandos específicos del WebSocket que requieren respuesta
          String cmd = doc["cmd"];
          
          if (cmd == "getPattern") {
            int pattern = sequencer.getCurrentPattern();
            // 6 data structures × 16 tracks × 16 steps — needs ~13-15KB ArduinoJson pool
            if (ESP.getFreeHeap() < 35000) {
              Serial.printf("[getPattern] Low heap %d, skipping\n", ESP.getFreeHeap());
              StaticJsonDocument<64> err; err["type"] = "error"; err["msg"] = "low_heap";
              String errStr; serializeJson(err, errStr);
              if (isClientReady(client)) client->text(errStr);
              if (safeFreeNeeded) free(safeData);
              if (_wsFreeAfter && _wsReassemblyBuf) { free(_wsReassemblyBuf); _wsReassemblyBuf = nullptr; }
              return;
            }
            yield();
            DynamicJsonDocument responseDoc(16384);  // 6 structures × 16×16 values needs 13-15KB
            responseDoc["type"] = "pattern";
            responseDoc["index"] = pattern;
            
            // Send steps (active/inactive) - 16 tracks activos
            for (int track = 0; track < 16; track++) {
              JsonArray trackSteps = responseDoc.createNestedArray(String(track));
              for (int step = 0; step < 16; step++) {
                trackSteps.add(sequencer.getStep(track, step));
              }
            }
            
            // Send velocities
            JsonObject velocitiesObj = responseDoc.createNestedObject("velocities");
            for (int track = 0; track < 16; track++) {
              JsonArray trackVels = velocitiesObj.createNestedArray(String(track));
              for (int step = 0; step < 16; step++) {
                trackVels.add(sequencer.getStepVelocity(track, step));
              }
            }
            
            // Send note lengths (1=full, 2=half, 4=quarter, 8=eighth)
            JsonObject noteLensObj = responseDoc.createNestedObject("noteLens");
            for (int track = 0; track < 16; track++) {
              JsonArray trackNL = noteLensObj.createNestedArray(String(track));
              for (int step = 0; step < 16; step++) {
                trackNL.add(sequencer.getStepNoteLen(track, step));
              }
            }

            JsonObject volumeLocksObj = responseDoc.createNestedObject("volumeLocks");
            for (int track = 0; track < 16; track++) {
              JsonArray trackLocks = volumeLocksObj.createNestedArray(String(track));
              for (int step = 0; step < 16; step++) {
                if (sequencer.hasStepVolumeLock(track, step)) {
                  trackLocks.add(sequencer.getStepVolumeLock(track, step));
                } else {
                  trackLocks.add(-1);
                }
              }
            }

            JsonObject probabilitiesObj = responseDoc.createNestedObject("probabilities");
            for (int track = 0; track < 16; track++) {
              JsonArray trackProb = probabilitiesObj.createNestedArray(String(track));
              for (int step = 0; step < 16; step++) {
                trackProb.add(sequencer.getStepProbability(track, step));
              }
            }

            JsonObject ratchetsObj = responseDoc.createNestedObject("ratchets");
            for (int track = 0; track < 16; track++) {
              JsonArray trackRat = ratchetsObj.createNestedArray(String(track));
              for (int step = 0; step < 16; step++) {
                trackRat.add(sequencer.getStepRatchet(track, step));
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
            // Cliente solicita inicialización completa
            Serial.printf("[init] Client %u | Heap: %d\n", client->id(), ESP.getFreeHeap());
            
            // Only send state if we have enough heap
            if (ESP.getFreeHeap() > 30000 && isClientReady(client)) {
              sendSequencerStateToClient(client);
            } else {
              Serial.println("[init] Low heap, sending minimal state");
              // Send minimal state
              StaticJsonDocument<256> mini;
              mini["type"] = "state";
              mini["playing"] = sequencer.isPlaying();
              mini["tempo"] = sequencer.getTempo();
              mini["pattern"] = sequencer.getCurrentPattern();
              mini["samplesLoaded"] = sampleManager.getLoadedSamplesCount();
              String miniOut;
              serializeJson(mini, miniOut);
              if (isClientReady(client)) client->text(miniOut);
            }

            // Enviar estado de MIDI scan
            if (midiController && isClientReady(client)) {
              StaticJsonDocument<128> midiScanDoc;
              midiScanDoc["type"] = "midiScan";
              midiScanDoc["enabled"] = midiController->isScanEnabled();
              String midiOut;
              serializeJson(midiScanDoc, midiOut);
              if (isClientReady(client)) client->text(midiOut);
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
            
            // Verificar que LittleFS está montado
            if (!LittleFS.begin(false)) {
              Serial.println("[getSamples] ERROR: LittleFS not mounted!");
              if (safeFreeNeeded) free(safeData);
              if (_wsFreeAfter && _wsReassemblyBuf) { free(_wsReassemblyBuf); _wsReassemblyBuf = nullptr; }
              return;
            }
            
            DynamicJsonDocument responseDoc(4096);  // Heap-allocated, flexible size
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
    // Cleanup safe buffer if we allocated one (non-fragmented path)
    if (safeFreeNeeded) {
      free(safeData);
    }
    // Cleanup reassembly buffer after processing
    if (_wsFreeAfter && _wsReassemblyBuf) {
      free(_wsReassemblyBuf);
      _wsReassemblyBuf = nullptr;
    }
  }
}

void WebInterface::broadcastSequencerState() {
  if (!initialized || !ws || ws->count() == 0) return;
  
  // Rate limiting - max 2 per second to reduce UI flickering
  static unsigned long lastBroadcast = 0;
  unsigned long now = millis();
  if (now - lastBroadcast < 500) return;
  lastBroadcast = now;
  
  // Protect against low heap — state doc + string need ~12KB total
  if (ESP.getFreeHeap() < 30000) {
    Serial.println("[WS] Low heap, skipping broadcast");
    return;
  }
  
  DynamicJsonDocument doc(8192);
  populateStateDocument(doc);
  
  String output;
  serializeJson(doc, output);
  ws->textAll(output);
}

void WebInterface::sendSequencerStateToClient(AsyncWebSocketClient* client) {
  if (!initialized || !ws || !isClientReady(client)) return;
  
  // Protect against low heap — state doc needs ~6KB
  if (ESP.getFreeHeap() < 30000) {
    Serial.println("[WS] Low heap, skipping state send");
    return;
  }
  
  DynamicJsonDocument doc(8192);
  populateStateDocument(doc);
  
  String output;
  serializeJson(doc, output);
  client->text(output);
}

void WebInterface::broadcastPadTrigger(int pad) {
  if (!initialized || !ws || ws->count() == 0) return;
  
  // Skip broadcast if heap is low
  if (ESP.getFreeHeap() < 20000) return;
  
  StaticJsonDocument<128> doc;
  doc["type"] = "pad";
  doc["pad"] = pad;
  
  String output;
  serializeJson(doc, output);
  ws->textAll(output);
}

void WebInterface::broadcastStep(int step) {
  if (!initialized || !ws || ws->count() == 0) return;
  // Rate-limit step broadcasts to max ~16/sec (every 60ms)
  static unsigned long lastStepBroadcast = 0;
  static int lastStep = -1;
  unsigned long now = millis();
  if (now - lastStepBroadcast < 60 && step != 0) {
    lastStep = step; // Remember for next broadcast
    return;
  }
  lastStepBroadcast = now;
  lastStep = step;
  // Ultra-compact step message for minimum latency
  char buf[32];
  int len = snprintf(buf, sizeof(buf), "{\"type\":\"step\",\"step\":%d}", step);
  ws->textAll(buf, len);
}

void WebInterface::broadcastSongPattern(int pattern, int songLength) {
  if (!initialized || !ws || ws->count() == 0) return;
  // Notify clients about pattern change in song mode
  char buf[80];
  int len = snprintf(buf, sizeof(buf), 
    "{\"type\":\"songPattern\",\"pattern\":%d,\"songLength\":%d}", 
    pattern, songLength);
  ws->textAll(buf, len);
}

void WebInterface::update() {
  if (!initialized || !ws || !server) return;
  
  unsigned long now = millis();
  
  // Broadcast audio levels cada 100ms (10fps) — protocolo binario ultra-eficiente
  static unsigned long lastAudioLevels = 0;
  if (now - lastAudioLevels >= 100 && ws->count() > 0) {
    lastAudioLevels = now;
    
    // Check heap health before broadcasting — skip if low memory
    if (ESP.getFreeHeap() < 20000) {
      // Low memory — skip this broadcast cycle
    } else {
      // Binary protocol: [0xAA][16 track peaks 0-255][master peak 0-255] = 18 bytes
      uint8_t levelBuf[18];
      levelBuf[0] = 0xAA;  // Magic byte: audio levels message
      
      float peaks[16];
      spiMaster.getTrackPeaks(peaks, 16);
      for (int i = 0; i < 16; i++) {
        float p = peaks[i];
        if (p < 0.0f) p = 0.0f;
        if (p > 1.0f) p = 1.0f;
        levelBuf[i + 1] = (uint8_t)(p * 255.0f);
      }
      float mp = spiMaster.getMasterPeak();
      if (mp < 0.0f) mp = 0.0f;
      if (mp > 1.0f) mp = 1.0f;
      levelBuf[17] = (uint8_t)(mp * 255.0f);
      
      ws->binaryAll(levelBuf, 18);
    }
  }
  
  // Limpiar WebSocket clients desconectados cada 2 segundos
  static unsigned long lastWsCleanup = 0;
  if (now - lastWsCleanup > 2000) {
    ws->cleanupClients(3);  // Match max client limit
    lastWsCleanup = now;
  }
  
  // Limpiar clientes UDP inactivos cada 30 segundos
  static unsigned long lastCleanup = 0;
  if (now - lastCleanup > 30000) {
    cleanupStaleUdpClients();
    lastCleanup = now;
  }
  
  // WiFi health check cada 30 segundos (no-blocking)
  static unsigned long lastWifiCheck = 0;
  if (now - lastWifiCheck > 30000) {
    lastWifiCheck = now;
    
    if (_staConnected) {
      // ── Modo STA: verificar conexión al router ──
      if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[WiFi] STA desconectado! Reconectando...");
        WiFi.reconnect();
      } else {
        static int lastRSSI = 0;
        int rssi = WiFi.RSSI();
        if (abs(rssi - lastRSSI) > 5) {
          Serial.printf("[WiFi] STA OK | IP: %s | RSSI: %d dBm\n",
                        WiFi.localIP().toString().c_str(), rssi);
          lastRSSI = rssi;
        }
      }
    } else {
      // ── Modo AP: verificar que siga activo ──
      if (WiFi.getMode() != WIFI_AP) {
        Serial.println("[WiFi] AP mode lost! Restarting...");
        WiFi.mode(WIFI_AP);
        WiFi.softAP("RED808", "red808esp32", 1, 0, 4);
        WiFi.setSleep(false);
      }
      int stations = WiFi.softAPgetStationNum();
      static int lastStationCount = 0;
      if (stations != lastStationCount) {
        Serial.printf("[WiFi] AP Stations: %d\n", stations);
        lastStationCount = stations;
      }
    }
  }
}

String WebInterface::getIP() {
  if (_staConnected && WiFi.status() == WL_CONNECTED) {
    return WiFi.localIP().toString();
  }
  return WiFi.softAPIP().toString();
}

// Procesar comandos JSON (compartido entre WebSocket y UDP)
void WebInterface::processCommand(const JsonDocument& doc) {
  String cmd = doc["cmd"];
  
  if (cmd == "trigger") {
    int pad = doc["pad"];
    if (pad < 0 || pad >= 24) return;  // 16 sequencer + 8 XTRA
    int velocity = doc.containsKey("vel") ? doc["vel"].as<int>() : 127;
    triggerPadWithLED(pad, velocity);
    broadcastPadTrigger(pad);
  }
  else if (cmd == "setStep") {
    int track = doc["track"];
    int step = doc["step"];
    if (track < 0 || track >= 16 || step < 0 || step >= 16) return;
    bool active = doc["active"];
    bool silent = doc.containsKey("silent") && doc["silent"].as<bool>();
    uint8_t noteLen = doc.containsKey("noteLen") ? (uint8_t)doc["noteLen"].as<int>() : 1;
    if (noteLen == 0 || (noteLen != 1 && noteLen != 2 && noteLen != 4 && noteLen != 8)) noteLen = 1;
    // Support writing to a specific pattern (for multi-pattern MIDI import)
    if (doc.containsKey("pattern")) {
      int pattern = doc["pattern"].as<int>();
      if (pattern >= 0 && pattern < MAX_PATTERNS) {
        int savedPattern = sequencer.getCurrentPattern();
        sequencer.selectPattern(pattern);
        sequencer.setStep(track, step, active);
        sequencer.selectPattern(savedPattern);
        yield(); // Prevent watchdog reset during bulk import
      }
    } else {
      sequencer.setStep(track, step, active);
      sequencer.setStepNoteLen(track, step, noteLen);
      // Only broadcast if not in silent/bulk mode
      if (!silent) {
        StaticJsonDocument<160> resp;
        resp["type"] = "stepSet";
        resp["track"] = track;
        resp["step"] = step;
        resp["active"] = active;
        resp["noteLen"] = noteLen;
        String out; serializeJson(resp, out);
        if (ws) ws->textAll(out);
      }
      yield();
    }
  }
  else if (cmd == "start") {
    sequencer.start();
    StaticJsonDocument<96> resp;
    resp["type"] = "playState";
    resp["playing"] = true;
    String out; serializeJson(resp, out);
    if (ws) ws->textAll(out);
  }
  else if (cmd == "stop") {
    sequencer.stop();
    StaticJsonDocument<96> resp;
    resp["type"] = "playState";
    resp["playing"] = false;
    String out; serializeJson(resp, out);
    if (ws) ws->textAll(out);
  }
  else if (cmd == "clearPattern") {
    int pattern = doc.containsKey("pattern") ? doc["pattern"].as<int>() : sequencer.getCurrentPattern();
    sequencer.clearPattern(pattern);
    yield();
    Serial.printf("[WS] Pattern %d cleared\n", pattern);
    StaticJsonDocument<96> resp;
    resp["type"] = "patternCleared";
    resp["pattern"] = pattern;
    String out; serializeJson(resp, out);
    if (ws) ws->textAll(out);
  }
  else if (cmd == "setSongMode") {
    bool enabled = doc["enabled"];
    int length = doc.containsKey("length") ? doc["length"].as<int>() : 1;
    sequencer.setSongLength(length);
    sequencer.setSongMode(enabled);
    broadcastSequencerState();
  }
  else if (cmd == "tempo") {
    float tempo = doc["value"];
    sequencer.setTempo(tempo);
    StaticJsonDocument<96> resp;
    resp["type"] = "tempoChange";
    resp["tempo"] = tempo;
    String out; serializeJson(resp, out);
    if (ws) ws->textAll(out);
  }
  else if (cmd == "selectPattern") {
    int pattern = doc["index"];
    sequencer.selectPattern(pattern);
    
    // Only broadcast state if heap is comfortably above the combined allocation spike
    // (broadcastSequencerState ~8-13KB) + (patternDoc ~14KB) = ~22-27KB peak
    if (ESP.getFreeHeap() > 50000) {
      broadcastSequencerState();
      yield(); // Let broadcastSequencerState String free before patternDoc alloc
    } else if (ESP.getFreeHeap() > 35000) {
      // Heap tight — send only a compact state notification, skip full broadcast
      StaticJsonDocument<128> minState;
      minState["type"] = "patternSelected";
      minState["pattern"] = pattern;
      String ms; serializeJson(minState, ms);
      if (ws) ws->textAll(ms);
    }
    
    if (ESP.getFreeHeap() < 30000) {
      Serial.printf("[selectPattern] Heap too low (%d), skipping pattern data\n", ESP.getFreeHeap());
      return;
    }
    yield();
    // 5 data structures × 16 tracks × 16 steps — needs ~11-13KB ArduinoJson pool
    DynamicJsonDocument patternDoc(14336);
    patternDoc["type"] = "pattern";
    patternDoc["index"] = pattern;
    
    for (int track = 0; track < 16; track++) {
      JsonArray trackSteps = patternDoc.createNestedArray(String(track));
      for (int step = 0; step < 16; step++) {
        trackSteps.add(sequencer.getStep(track, step));
      }
    }
    
    JsonObject velocitiesObj = patternDoc.createNestedObject("velocities");
    for (int track = 0; track < 16; track++) {
      JsonArray trackVels = velocitiesObj.createNestedArray(String(track));
      for (int step = 0; step < 16; step++) {
        trackVels.add(sequencer.getStepVelocity(track, step));
      }
    }

    JsonObject volumeLocksObj = patternDoc.createNestedObject("volumeLocks");
    for (int track = 0; track < 16; track++) {
      JsonArray trackLocks = volumeLocksObj.createNestedArray(String(track));
      for (int step = 0; step < 16; step++) {
        if (sequencer.hasStepVolumeLock(track, step)) {
          trackLocks.add(sequencer.getStepVolumeLock(track, step));
        } else {
          trackLocks.add(-1);
        }
      }
    }

    JsonObject probabilitiesObj = patternDoc.createNestedObject("probabilities");
    for (int track = 0; track < 16; track++) {
      JsonArray trackProb = probabilitiesObj.createNestedArray(String(track));
      for (int step = 0; step < 16; step++) {
        trackProb.add(sequencer.getStepProbability(track, step));
      }
    }

    JsonObject ratchetsObj = patternDoc.createNestedObject("ratchets");
    for (int track = 0; track < 16; track++) {
      JsonArray trackRat = ratchetsObj.createNestedArray(String(track));
      for (int step = 0; step < 16; step++) {
        trackRat.add(sequencer.getStepRatchet(track, step));
      }
    }
    
    String patternOutput;
    serializeJson(patternDoc, patternOutput);
    if (ws && ws->count() > 0) ws->textAll(patternOutput);
  }
  else if (cmd == "loadSample") {
    const char* family = doc["family"];
    const char* filename = doc["filename"];
    int padIndex = doc["pad"];
    if (padIndex < 0 || padIndex >= 16) return;
    
    float trimStart = doc.containsKey("trimStart") ? (float)doc["trimStart"] : 0.0f;
    float trimEnd = doc.containsKey("trimEnd") ? (float)doc["trimEnd"] : 1.0f;
    float fadeIn  = doc.containsKey("fadeIn")  ? (float)doc["fadeIn"]  / 1000.0f : 0.0f;  // ms → sec
    float fadeOut = doc.containsKey("fadeOut") ? (float)doc["fadeOut"] / 1000.0f : 0.0f;  // ms → sec
    
    String fullPath = String("/") + String(family) + String("/") + String(filename);
    
    yield();
    
    if (sampleManager.loadSample(fullPath.c_str(), padIndex)) {
      // Apply trim if specified (not default 0-1)
      if (trimStart > 0.001f || trimEnd < 0.999f) {
        sampleManager.trimSample(padIndex, trimStart, trimEnd);
      }
      // Apply fade in/out if specified
      if (fadeIn > 0.001f || fadeOut > 0.001f) {
        sampleManager.applyFade(padIndex, fadeIn, fadeOut);
      }
      
      StaticJsonDocument<256> responseDoc;
      responseDoc["type"] = "sampleLoaded";
      responseDoc["pad"] = padIndex;
      responseDoc["filename"] = filename;
      responseDoc["size"] = sampleManager.getSampleLength(padIndex) * 2;
      responseDoc["format"] = detectSampleFormat(filename);
      
      String output;
      serializeJson(responseDoc, output);
      if (ws) ws->textAll(output);
    }
  }
  // === Trim already-loaded sample ===
  else if (cmd == "trimSample") {
    int padIndex = doc["pad"];
    float trimStart = doc.containsKey("trimStart") ? (float)doc["trimStart"] : 0.0f;
    float trimEnd = doc.containsKey("trimEnd") ? (float)doc["trimEnd"] : 1.0f;
    if (padIndex >= 0 && padIndex < MAX_SAMPLES && sampleManager.isSampleLoaded(padIndex)) {
      if (sampleManager.trimSample(padIndex, trimStart, trimEnd)) {
        StaticJsonDocument<256> responseDoc;
        responseDoc["type"] = "sampleTrimmed";
        responseDoc["pad"] = padIndex;
        responseDoc["size"] = sampleManager.getSampleLength(padIndex) * 2;
        responseDoc["samples"] = sampleManager.getSampleLength(padIndex);
        String output;
        serializeJson(responseDoc, output);
        if (ws) ws->textAll(output);
      }
    }
  }
  // === XTRA PADS: list samples from /xtra folder ===
  else if (cmd == "getXtraSamples") {
    StaticJsonDocument<2048> responseDoc;
    responseDoc["type"] = "xtraSampleList";
    
    File dir = LittleFS.open("/xtra", "r");
    if (dir && dir.isDirectory()) {
      JsonArray samples = responseDoc.createNestedArray("samples");
      File file = dir.openNextFile();
      int count = 0;
      while (file) {
        if (!file.isDirectory()) {
          String filename = file.name();
          int lastSlash = filename.lastIndexOf('/');
          if (lastSlash >= 0) filename = filename.substring(lastSlash + 1);
          
          if (isSupportedSampleFile(filename)) {
            JsonObject sampleObj = samples.createNestedObject();
            sampleObj["name"] = filename;
            sampleObj["size"] = file.size();
            count++;
            if (count % 3 == 0) yield();
          }
        }
        file.close();
        file = dir.openNextFile();
      }
      dir.close();
      Serial.printf("[getXtraSamples] Found %d samples in /xtra\n", count);
    } else {
      // Create /xtra if it doesn't exist
      LittleFS.mkdir("/xtra");
      responseDoc.createNestedArray("samples");
      Serial.println("[getXtraSamples] /xtra folder created (empty)");
    }
    
    String output;
    serializeJson(responseDoc, output);
    if (ws) ws->textAll(output);
  }
  // === XTRA PADS: load sample from /xtra to a pad ===
  else if (cmd == "loadXtraSample") {
    const char* filename = doc["filename"];
    int padIndex = doc["pad"];
    if (padIndex < 16 || padIndex >= 24) return;
    
    String fullPath = String("/xtra/") + String(filename);
    yield();
    
    if (sampleManager.loadSample(fullPath.c_str(), padIndex)) {
      StaticJsonDocument<256> responseDoc;
      responseDoc["type"] = "sampleLoaded";
      responseDoc["pad"] = padIndex;
      responseDoc["filename"] = filename;
      responseDoc["size"] = sampleManager.getSampleLength(padIndex) * 2;
      
      String output;
      serializeJson(responseDoc, output);
      if (ws) ws->textAll(output);
      Serial.printf("[loadXtraSample] Loaded %s -> pad %d\n", fullPath.c_str(), padIndex);
    } else {
      Serial.printf("[loadXtraSample] FAILED: %s -> pad %d\n", fullPath.c_str(), padIndex);
    }
  }
  else if (cmd == "mute") {
    int track = doc["track"];
    if (track < 0 || track >= 16) return;
    yield();
    bool muted = doc["value"];
    sequencer.muteTrack(track, muted);
    StaticJsonDocument<128> muteDoc;
    muteDoc["type"] = "trackMuted";
    muteDoc["track"] = track;
    muteDoc["muted"] = muted;
    String muteOutput;
    serializeJson(muteDoc, muteOutput);
    if (ws) ws->textAll(muteOutput);
  }
  else if (cmd == "toggleLoop") {
    int track = doc["track"];
    if (track < 0 || track >= 24) return;
    yield();
    
    if (track >= 16) {
      // XTRA pads (16-23): continuous audio loop via SPIMaster → STM32
      bool newState = !spiMaster.isPadLooping(track);
      spiMaster.setPadLoop(track, newState);
      // If enabling loop, also trigger the sample so it starts playing
      if (newState) {
        spiMaster.triggerSampleLive(track, 127);
      }
      
      StaticJsonDocument<192> responseDoc;
      responseDoc["type"] = "loopState";
      responseDoc["track"] = track;
      responseDoc["active"] = newState;
      responseDoc["paused"] = false;
      responseDoc["loopType"] = 0;
      
      String output;
      serializeJson(responseDoc, output);
      if (ws) ws->textAll(output);
    } else {
      // Sequencer tracks (0-15): step-based loop via Sequencer
      if (doc.containsKey("loopType")) {
        int lt = doc["loopType"];
        sequencer.setLoopType(track, (LoopType)constrain(lt, 0, 3));
      }
      
      sequencer.toggleLoop(track);
      
      StaticJsonDocument<192> responseDoc;
      responseDoc["type"] = "loopState";
      responseDoc["track"] = track;
      responseDoc["active"] = sequencer.isLooping(track);
      responseDoc["paused"] = sequencer.isLoopPaused(track);
      responseDoc["loopType"] = (int)sequencer.getLoopType(track);
      
      String output;
      serializeJson(responseDoc, output);
      if (ws) ws->textAll(output);
    }
    yield();
  }
  else if (cmd == "setLoopType") {
    int track = doc["track"];
    int lt = doc["loopType"];
    if (track < 0 || track >= 16) return;
    yield();
    sequencer.setLoopType(track, (LoopType)constrain(lt, 0, 3));
    
    StaticJsonDocument<192> responseDoc;
    responseDoc["type"] = "loopState";
    responseDoc["track"] = track;
    responseDoc["active"] = sequencer.isLooping(track);
    responseDoc["paused"] = sequencer.isLoopPaused(track);
    responseDoc["loopType"] = lt;
    
    String output;
    serializeJson(responseDoc, output);
    if (ws) ws->textAll(output);
    yield();
  }
  else if (cmd == "pauseLoop") {
    int track = doc["track"];
    if (track < 0 || track >= 16) return;
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
    StaticJsonDocument<96> resp;
    resp["type"] = "ledMode"; resp["mono"] = monoMode;
    String out; serializeJson(resp, out);
    if (ws) ws->textAll(out);
  }
  else if (cmd == "setFilter") {
    int type = doc["type"];
    spiMaster.setFilterType((FilterType)type);
    StaticJsonDocument<96> resp;
    resp["type"] = "masterFx"; resp["param"] = "filterType"; resp["value"] = type;
    String out; serializeJson(resp, out);
    if (ws) ws->textAll(out);
  }
  else if (cmd == "setFilterCutoff") {
    float cutoff = doc["value"];
    spiMaster.setFilterCutoff(cutoff);
    StaticJsonDocument<96> resp;
    resp["type"] = "masterFx"; resp["param"] = "filterCutoff"; resp["value"] = cutoff;
    String out; serializeJson(resp, out);
    if (ws) ws->textAll(out);
  }
  else if (cmd == "setFilterResonance") {
    float resonance = doc["value"];
    spiMaster.setFilterResonance(resonance);
    StaticJsonDocument<96> resp;
    resp["type"] = "masterFx"; resp["param"] = "filterResonance"; resp["value"] = resonance;
    String out; serializeJson(resp, out);
    if (ws) ws->textAll(out);
  }
  else if (cmd == "setBitCrush") {
    int bits = doc["value"];
    spiMaster.setBitDepth(bits);
    StaticJsonDocument<96> resp;
    resp["type"] = "masterFx"; resp["param"] = "bitCrush"; resp["value"] = bits;
    String out; serializeJson(resp, out);
    if (ws) ws->textAll(out);
  }
  else if (cmd == "setDistortion") {
    float amount = doc["value"];
    spiMaster.setDistortion(amount);
    StaticJsonDocument<96> resp;
    resp["type"] = "masterFx"; resp["param"] = "distortion"; resp["value"] = amount;
    String out; serializeJson(resp, out);
    if (ws) ws->textAll(out);
  }
  else if (cmd == "setDistortionMode") {
    int mode = doc["value"];
    spiMaster.setDistortionMode((DistortionMode)mode);
    StaticJsonDocument<96> resp;
    resp["type"] = "masterFx"; resp["param"] = "distortionMode"; resp["value"] = mode;
    String out; serializeJson(resp, out);
    if (ws) ws->textAll(out);
  }
  else if (cmd == "setSampleRate") {
    int rate = doc["value"];
    spiMaster.setSampleRateReduction(rate);
    StaticJsonDocument<96> resp;
    resp["type"] = "masterFx"; resp["param"] = "sampleRate"; resp["value"] = rate;
    String out; serializeJson(resp, out);
    if (ws) ws->textAll(out);
  }
  // ============= NEW: Master Effects Commands =============
  else if (cmd == "setDelayActive") {
    bool active = doc["value"];
    spiMaster.setDelayActive(active);
    StaticJsonDocument<96> resp;
    resp["type"] = "masterFx"; resp["param"] = "delayActive"; resp["value"] = active;
    String out; serializeJson(resp, out);
    if (ws) ws->textAll(out);
  }
  else if (cmd == "setDelayTime") {
    float ms = doc["value"];
    spiMaster.setDelayTime(ms);
    StaticJsonDocument<96> resp;
    resp["type"] = "masterFx"; resp["param"] = "delayTime"; resp["value"] = ms;
    String out; serializeJson(resp, out);
    if (ws) ws->textAll(out);
  }
  else if (cmd == "setDelayFeedback") {
    float fb = doc["value"];
    spiMaster.setDelayFeedback(fb / 100.0f);
    StaticJsonDocument<96> resp;
    resp["type"] = "masterFx"; resp["param"] = "delayFeedback"; resp["value"] = fb;
    String out; serializeJson(resp, out);
    if (ws) ws->textAll(out);
  }
  else if (cmd == "setDelayMix") {
    float mix = doc["value"];
    spiMaster.setDelayMix(mix / 100.0f);
    StaticJsonDocument<96> resp;
    resp["type"] = "masterFx"; resp["param"] = "delayMix"; resp["value"] = mix;
    String out; serializeJson(resp, out);
    if (ws) ws->textAll(out);
  }
  else if (cmd == "setPhaserActive") {
    bool active = doc["value"];
    spiMaster.setPhaserActive(active);
    StaticJsonDocument<96> resp;
    resp["type"] = "masterFx"; resp["param"] = "phaserActive"; resp["value"] = active;
    String out; serializeJson(resp, out);
    if (ws) ws->textAll(out);
  }
  else if (cmd == "setPhaserRate") {
    float rate = doc["value"];
    spiMaster.setPhaserRate(rate / 100.0f);
    StaticJsonDocument<96> resp;
    resp["type"] = "masterFx"; resp["param"] = "phaserRate"; resp["value"] = rate;
    String out; serializeJson(resp, out);
    if (ws) ws->textAll(out);
  }
  else if (cmd == "setPhaserDepth") {
    float depth = doc["value"];
    spiMaster.setPhaserDepth(depth / 100.0f);
    StaticJsonDocument<96> resp;
    resp["type"] = "masterFx"; resp["param"] = "phaserDepth"; resp["value"] = depth;
    String out; serializeJson(resp, out);
    if (ws) ws->textAll(out);
  }
  else if (cmd == "setPhaserFeedback") {
    float fb = doc["value"];
    spiMaster.setPhaserFeedback(fb / 100.0f);
    StaticJsonDocument<96> resp;
    resp["type"] = "masterFx"; resp["param"] = "phaserFeedback"; resp["value"] = fb;
    String out; serializeJson(resp, out);
    if (ws) ws->textAll(out);
  }
  else if (cmd == "setFlangerActive") {
    bool active = doc["value"];
    spiMaster.setFlangerActive(active);
    StaticJsonDocument<96> resp;
    resp["type"] = "masterFx"; resp["param"] = "flangerActive"; resp["value"] = active;
    String out; serializeJson(resp, out);
    if (ws) ws->textAll(out);
  }
  else if (cmd == "setFlangerRate") {
    float rate = doc["value"];
    spiMaster.setFlangerRate(rate / 100.0f);
    StaticJsonDocument<96> resp;
    resp["type"] = "masterFx"; resp["param"] = "flangerRate"; resp["value"] = rate;
    String out; serializeJson(resp, out);
    if (ws) ws->textAll(out);
  }
  else if (cmd == "setFlangerDepth") {
    float depth = doc["value"];
    spiMaster.setFlangerDepth(depth / 100.0f);
    StaticJsonDocument<96> resp;
    resp["type"] = "masterFx"; resp["param"] = "flangerDepth"; resp["value"] = depth;
    String out; serializeJson(resp, out);
    if (ws) ws->textAll(out);
  }
  else if (cmd == "setFlangerFeedback") {
    float fb = doc["value"];
    spiMaster.setFlangerFeedback(fb / 100.0f);
    StaticJsonDocument<96> resp;
    resp["type"] = "masterFx"; resp["param"] = "flangerFeedback"; resp["value"] = fb;
    String out; serializeJson(resp, out);
    if (ws) ws->textAll(out);
  }
  else if (cmd == "setFlangerMix") {
    float mix = doc["value"];
    spiMaster.setFlangerMix(mix / 100.0f);
    StaticJsonDocument<96> resp;
    resp["type"] = "masterFx"; resp["param"] = "flangerMix"; resp["value"] = mix;
    String out; serializeJson(resp, out);
    if (ws) ws->textAll(out);
  }
  else if (cmd == "setCompressorActive") {
    bool active = doc["value"];
    spiMaster.setCompressorActive(active);
    StaticJsonDocument<96> resp;
    resp["type"] = "masterFx"; resp["param"] = "compressorActive"; resp["value"] = active;
    String out; serializeJson(resp, out);
    if (ws) ws->textAll(out);
  }
  else if (cmd == "setCompressorThreshold") {
    float thresh = doc["value"];
    spiMaster.setCompressorThreshold(thresh);
    StaticJsonDocument<96> resp;
    resp["type"] = "masterFx"; resp["param"] = "compressorThreshold"; resp["value"] = thresh;
    String out; serializeJson(resp, out);
    if (ws) ws->textAll(out);
  }
  else if (cmd == "setCompressorRatio") {
    float ratio = doc["value"];
    spiMaster.setCompressorRatio(ratio);
    StaticJsonDocument<96> resp;
    resp["type"] = "masterFx"; resp["param"] = "compressorRatio"; resp["value"] = ratio;
    String out; serializeJson(resp, out);
    if (ws) ws->textAll(out);
  }
  else if (cmd == "setCompressorAttack") {
    float attack = doc["value"];
    spiMaster.setCompressorAttack(attack);
    StaticJsonDocument<96> resp;
    resp["type"] = "masterFx"; resp["param"] = "compressorAttack"; resp["value"] = attack;
    String out; serializeJson(resp, out);
    if (ws) ws->textAll(out);
  }
  else if (cmd == "setCompressorRelease") {
    float release = doc["value"];
    spiMaster.setCompressorRelease(release);
    StaticJsonDocument<96> resp;
    resp["type"] = "masterFx"; resp["param"] = "compressorRelease"; resp["value"] = release;
    String out; serializeJson(resp, out);
    if (ws) ws->textAll(out);
  }
  else if (cmd == "setCompressorMakeupGain") {
    float gain = doc["value"];
    spiMaster.setCompressorMakeupGain(gain);
    StaticJsonDocument<96> resp;
    resp["type"] = "masterFx"; resp["param"] = "compressorMakeupGain"; resp["value"] = gain;
    String out; serializeJson(resp, out);
    if (ws) ws->textAll(out);
  }
  // ============= Per-Pad / Per-Track FX Commands =============
  else if (cmd == "setPadDistortion") {
    int pad = doc["pad"];
    float amount = doc["amount"];
    int mode = doc.containsKey("mode") ? (int)doc["mode"] : 0;
    if (pad >= 0 && pad < 24) {
      spiMaster.setPadDistortion(pad, amount, (DistortionMode)mode);
      StaticJsonDocument<128> resp;
      resp["type"] = "padFxSet";
      resp["pad"] = pad;
      resp["fx"] = "distortion";
      resp["amount"] = amount;
      resp["mode"] = mode;
      String out; serializeJson(resp, out);
      if (ws) ws->textAll(out);
    }
  }
  else if (cmd == "setPadBitCrush") {
    int pad = doc["pad"];
    int bits = doc["value"];
    if (pad >= 0 && pad < 24) {
      spiMaster.setPadBitCrush(pad, bits);
      StaticJsonDocument<128> resp;
      resp["type"] = "padFxSet";
      resp["pad"] = pad;
      resp["fx"] = "bitcrush";
      resp["value"] = bits;
      String out; serializeJson(resp, out);
      if (ws) ws->textAll(out);
    }
  }
  else if (cmd == "clearPadFX") {
    int pad = doc["pad"];
    if (pad >= 0 && pad < 24) {
      spiMaster.clearPadFX(pad);
      StaticJsonDocument<96> resp;
      resp["type"] = "padFxCleared";
      resp["pad"] = pad;
      String out; serializeJson(resp, out);
      if (ws) ws->textAll(out);
    }
  }
  else if (cmd == "setTrackDistortion") {
    int track = doc["track"];
    float amount = doc["amount"];
    int mode = doc.containsKey("mode") ? (int)doc["mode"] : 0;
    if (track >= 0 && track < 16) {
      spiMaster.setTrackDistortion(track, amount, (DistortionMode)mode);
      StaticJsonDocument<128> resp;
      resp["type"] = "trackFxSet";
      resp["track"] = track;
      resp["fx"] = "distortion";
      resp["amount"] = amount;
      resp["mode"] = mode;
      String out; serializeJson(resp, out);
      if (ws) ws->textAll(out);
    }
  }
  else if (cmd == "setTrackBitCrush") {
    int track = doc["track"];
    int bits = doc["value"];
    if (track >= 0 && track < 16) {
      spiMaster.setTrackBitCrush(track, bits);
      StaticJsonDocument<128> resp;
      resp["type"] = "trackFxSet";
      resp["track"] = track;
      resp["fx"] = "bitcrush";
      resp["value"] = bits;
      String out; serializeJson(resp, out);
      if (ws) ws->textAll(out);
    }
  }
  else if (cmd == "clearTrackFX") {
    int track = doc["track"];
    if (track >= 0 && track < 16) {
      spiMaster.clearTrackFX(track);
      StaticJsonDocument<96> resp;
      resp["type"] = "trackFxCleared";
      resp["track"] = track;
      String out; serializeJson(resp, out);
      if (ws) ws->textAll(out);
    }
  }
  // ============= REVERSE Command =============
  else if (cmd == "setReverse") {
    bool value = doc["value"];
    StaticJsonDocument<128> resp;
    resp["type"] = "trackFxUpdate";
    resp["fx"] = "reverse";
    resp["value"] = value;
    if (doc.containsKey("track")) {
      int track = doc["track"];
      if (track >= 0 && track < 16) {
        spiMaster.setReverseSample(track, value);
        resp["track"] = track;
        String out; serializeJson(resp, out);
        if (ws) ws->textAll(out);
      }
    } else if (doc.containsKey("pad")) {
      int pad = doc["pad"];
      if (pad >= 0 && pad < 24) {
        spiMaster.setReverseSample(pad, value);
        resp["pad"] = pad;
        String out; serializeJson(resp, out);
        if (ws) ws->textAll(out);
      }
    }
  }
  // ============= PITCH SHIFT Command =============
  else if (cmd == "setPitchShift") {
    float value = doc["value"];
    StaticJsonDocument<128> resp;
    resp["type"] = "trackFxUpdate";
    resp["fx"] = "pitch";
    resp["value"] = value;
    if (doc.containsKey("track")) {
      int track = doc["track"];
      if (track >= 0 && track < 16) {
        spiMaster.setTrackPitchShift(track, value);
        resp["track"] = track;
        String out; serializeJson(resp, out);
        if (ws) ws->textAll(out);
      }
    } else if (doc.containsKey("pad")) {
      int pad = doc["pad"];
      if (pad >= 0 && pad < 24) {
        spiMaster.setTrackPitchShift(pad, value);
        resp["pad"] = pad;
        String out; serializeJson(resp, out);
        if (ws) ws->textAll(out);
      }
    }
  }
  // ============= STUTTER Command =============
  else if (cmd == "setStutter") {
    bool value = doc["value"];
    int interval = doc.containsKey("interval") ? (int)doc["interval"] : 100;
    StaticJsonDocument<128> resp;
    resp["type"] = "trackFxUpdate";
    resp["fx"] = "stutter";
    resp["value"] = value;
    resp["interval"] = interval;
    if (doc.containsKey("track")) {
      int track = doc["track"];
      if (track >= 0 && track < 16) {
        spiMaster.setStutter(track, value, interval);
        resp["track"] = track;
        String out; serializeJson(resp, out);
        if (ws) ws->textAll(out);
      }
    } else if (doc.containsKey("pad")) {
      int pad = doc["pad"];
      if (pad >= 0 && pad < 24) {
        spiMaster.setStutter(pad, value, interval);
        resp["pad"] = pad;
        String out; serializeJson(resp, out);
        if (ws) ws->textAll(out);
      }
    }
  }
  // ============= SCRATCH Command (configurable) =============
  else if (cmd == "setScratch") {
    int track = doc["track"];
    bool value = doc["value"];
    if (track >= 0 && track < 24) {
      float rate = doc.containsKey("rate") ? (float)doc["rate"] : 5.0f;
      float depth = doc.containsKey("depth") ? (float)doc["depth"] : 0.85f;
      float filter = doc.containsKey("filter") ? (float)doc["filter"] : 4000.0f;
      float crackle = doc.containsKey("crackle") ? (float)doc["crackle"] : 0.25f;
      spiMaster.setScratchParams(track, value, rate, depth, filter, crackle);
      Serial.printf("[WS] Scratch %s -> Track %d (rate:%.1f depth:%.2f filter:%.0f crackle:%.2f)\n",
                    value ? "ON" : "OFF", track, rate, depth, filter, crackle);
    }
  }
  // ============= TURNTABLISM Command (configurable) =============
  else if (cmd == "setTurntablism") {
    int track = doc["track"];
    bool value = doc["value"];
    if (track >= 0 && track < 24) {
      String control = doc.containsKey("control") ? doc["control"].as<String>() : "auto";
      bool autoMode = (control == "auto");
      int mode = doc.containsKey("mode") ? (int)doc["mode"] : -1;
      int brakeSpeed = doc.containsKey("brakeSpeed") ? (int)doc["brakeSpeed"] : 350;
      int backspinSpeed = doc.containsKey("backspinSpeed") ? (int)doc["backspinSpeed"] : 450;
      float transformRate = doc.containsKey("transformRate") ? (float)doc["transformRate"] : 11.0f;
      float vinylNoise = doc.containsKey("vinylNoise") ? (float)doc["vinylNoise"] : 0.35f;
      spiMaster.setTurntablismParams(track, value, autoMode, mode, brakeSpeed, backspinSpeed, transformRate, vinylNoise);
      Serial.printf("[WS] Turntablism %s -> Track %d (auto:%d mode:%d brake:%d backspin:%d tRate:%.1f noise:%.2f)\n",
                    value ? "ON" : "OFF", track, autoMode, mode, brakeSpeed, backspinSpeed, transformRate, vinylNoise);
    }
  }
  // ============= PER-TRACK LIVE FX (SLAVE Controller) =============
  else if (cmd == "setTrackEcho") {
    int track = doc["track"];
    if (track >= 0 && track < 16) {
      float time, feedback, mix;
      bool active;
      // One-knob mode (pot 0-127) or detailed mode
      if (doc.containsKey("value")) {
        int val = doc["value"].as<int>();
        active = val > 0;
        mix = (float)val / 127.0f * 100.0f;
        time = doc.containsKey("time") ? doc["time"].as<float>() : 100.0f;
        feedback = doc.containsKey("feedback") ? doc["feedback"].as<float>() : 40.0f;
      } else {
        active = doc["active"] | false;
        time = doc.containsKey("time") ? doc["time"].as<float>() : 100.0f;
        feedback = doc.containsKey("feedback") ? doc["feedback"].as<float>() : 40.0f;
        mix = doc.containsKey("mix") ? doc["mix"].as<float>() : 50.0f;
      }
      spiMaster.setTrackEcho(track, active, time, feedback, mix);
      StaticJsonDocument<256> resp;
      resp["type"] = "trackLiveFx";
      resp["track"] = track;
      resp["fx"] = "echo";
      resp["active"] = spiMaster.getTrackEchoActive(track);
      resp["time"] = time;
      resp["feedback"] = feedback;
      resp["mix"] = mix;
      String out; serializeJson(resp, out);
      if (ws) ws->textAll(out);
    }
  }
  else if (cmd == "setTrackFlanger") {
    int track = doc["track"];
    if (track >= 0 && track < 16) {
      float rate, depth, feedback;
      bool active;
      if (doc.containsKey("value")) {
        int val = doc["value"].as<int>();
        active = val > 0;
        depth = (float)val / 127.0f * 100.0f;
        rate = doc.containsKey("rate") ? doc["rate"].as<float>() : 50.0f;
        feedback = doc.containsKey("feedback") ? doc["feedback"].as<float>() : 30.0f;
      } else {
        active = doc["active"] | false;
        rate = doc.containsKey("rate") ? doc["rate"].as<float>() : 50.0f;
        depth = doc.containsKey("depth") ? doc["depth"].as<float>() : 50.0f;
        feedback = doc.containsKey("feedback") ? doc["feedback"].as<float>() : 30.0f;
      }
      spiMaster.setTrackFlanger(track, active, rate, depth, feedback);
      StaticJsonDocument<256> resp;
      resp["type"] = "trackLiveFx";
      resp["track"] = track;
      resp["fx"] = "flanger";
      resp["active"] = spiMaster.getTrackFlangerActive(track);
      resp["rate"] = rate;
      resp["depth"] = depth;
      resp["feedback"] = feedback;
      String out; serializeJson(resp, out);
      if (ws) ws->textAll(out);
    }
  }
  else if (cmd == "setTrackCompressor") {
    int track = doc["track"];
    if (track >= 0 && track < 16) {
      float threshold, ratio;
      bool active;
      if (doc.containsKey("value")) {
        int val = doc["value"].as<int>();
        active = val > 0;
        threshold = -60.0f + (float)val / 127.0f * 60.0f; // 0=-60dB, 127=0dB
        ratio = doc.containsKey("ratio") ? doc["ratio"].as<float>() : 4.0f;
      } else {
        active = doc["active"] | false;
        threshold = doc.containsKey("threshold") ? doc["threshold"].as<float>() : -20.0f;
        ratio = doc.containsKey("ratio") ? doc["ratio"].as<float>() : 4.0f;
      }
      spiMaster.setTrackCompressor(track, active, threshold, ratio);
      StaticJsonDocument<256> resp;
      resp["type"] = "trackLiveFx";
      resp["track"] = track;
      resp["fx"] = "compressor";
      resp["active"] = spiMaster.getTrackCompressorActive(track);
      resp["threshold"] = threshold;
      resp["ratio"] = ratio;
      String out; serializeJson(resp, out);
      if (ws) ws->textAll(out);
    }
  }
  else if (cmd == "setSidechainPro") {
    bool active = doc.containsKey("active") ? doc["active"].as<bool>() : true;
    int source = doc.containsKey("source") ? doc["source"].as<int>() : 0;
    float amountPct = doc.containsKey("amount") ? doc["amount"].as<float>() : 50.0f;
    float attackMs = doc.containsKey("attack") ? doc["attack"].as<float>() : 6.0f;
    float releaseMs = doc.containsKey("release") ? doc["release"].as<float>() : 180.0f;
    float knee = doc.containsKey("knee") ? doc["knee"].as<float>() : 0.4f;

    uint16_t mask = 0;
    if (doc.containsKey("destinations")) {
      JsonArrayConst dest = doc["destinations"].as<JsonArrayConst>();
      for (JsonVariantConst v : dest) {
        int t = v.as<int>();
        if (t >= 0 && t < 16 && t != source) mask |= (1U << t);
      }
    }

    spiMaster.setSidechain(active, source, mask, amountPct / 100.0f, attackMs, releaseMs, knee);

    StaticJsonDocument<256> resp;
    resp["type"] = "sidechainState";
    resp["active"] = active;
    resp["source"] = source;
    resp["mask"] = mask;
    resp["amount"] = amountPct;
    resp["attack"] = attackMs;
    resp["release"] = releaseMs;
    resp["knee"] = knee;
    String out; serializeJson(resp, out);
    if (ws) ws->textAll(out);
  }
  else if (cmd == "clearTrackLiveFX") {
    int track = doc["track"];
    if (track >= 0 && track < 16) {
      spiMaster.clearTrackLiveFX(track);
      StaticJsonDocument<128> resp;
      resp["type"] = "trackLiveFx";
      resp["track"] = track;
      resp["fx"] = "cleared";
      resp["active"] = false;
      String out; serializeJson(resp, out);
      if (ws) ws->textAll(out);
    }
  }
  else if (cmd == "setSequencerVolume") {
    int volume = doc["value"];
    spiMaster.setSequencerVolume(volume);
    
    // Broadcast volume change to all clients
    StaticJsonDocument<128> responseDoc;
    responseDoc["type"] = "state";
    responseDoc["sequencerVolume"] = volume;
    
    String output;
    serializeJson(responseDoc, output);
    if (ws) ws->textAll(output);
  }
  else if (cmd == "setLiveVolume") {
    int volume = doc["value"];
    spiMaster.setLiveVolume(volume);
    
    // Broadcast volume change to all clients
    StaticJsonDocument<128> responseDoc;
    responseDoc["type"] = "state";
    responseDoc["liveVolume"] = volume;
    
    String output;
    serializeJson(responseDoc, output);
    if (ws) ws->textAll(output);
  }
  else if (cmd == "setVolume") {
    int volume = doc["value"];
    spiMaster.setMasterVolume(volume);
    StaticJsonDocument<96> resp;
    resp["type"] = "masterFx"; resp["param"] = "volume"; resp["value"] = volume;
    String out; serializeJson(resp, out);
    if (ws) ws->textAll(out);
  }
  else if (cmd == "stopAllSounds") {
    spiMaster.stopAll();
    Serial.println("[WS] KILL ALL - All sounds stopped");
    StaticJsonDocument<64> resp;
    resp["type"] = "allStopped";
    String out; serializeJson(resp, out);
    if (ws) ws->textAll(out);
  }
  else if (cmd == "setLivePitch") {
    float pitch = doc["pitch"].as<float>();
    pitch = constrain(pitch, 0.25f, 3.0f);
    spiMaster.setLivePitchShift(pitch);
    StaticJsonDocument<96> resp;
    resp["type"] = "masterFx"; resp["param"] = "livePitch"; resp["value"] = pitch;
    String out; serializeJson(resp, out);
    if (ws) ws->textAll(out);
  }
  // ============= NEW: Per-Track Filter Commands =============
  else if (cmd == "setTrackFilter") {
    int track = doc["track"];
    if (track < 0 || track >= 16) {
      Serial.printf("[WS] Invalid track %d (must be 0-15)\n", track);
      return;
    }
    int filterType = doc.containsKey("type") ? doc["type"].as<int>() : doc["filterType"].as<int>();
    float cutoff = doc.containsKey("cutoff") ? doc["cutoff"].as<float>() : 1000.0f;
    float resonance = doc.containsKey("resonance") ? doc["resonance"].as<float>() : 1.0f;
    float gain = doc.containsKey("gain") ? doc["gain"].as<float>() : 0.0f;
    
    bool success = spiMaster.setTrackFilter(track, (FilterType)filterType, cutoff, resonance, gain);
    
    // Send response with filter parameters for UI badge
    StaticJsonDocument<256> responseDoc;
    responseDoc["type"] = "trackFilterSet";
    responseDoc["track"] = track;
    responseDoc["success"] = success;
    responseDoc["activeFilters"] = spiMaster.getActiveTrackFiltersCount();
    responseDoc["filterType"] = filterType;
    responseDoc["cutoff"] = (int)cutoff;
    responseDoc["resonance"] = resonance;
    
    String output;
    serializeJson(responseDoc, output);
    if (ws) ws->textAll(output);
  }
  else if (cmd == "clearTrackFilter") {
    int track = doc["track"];
    if (track < 0 || track >= 16) {
      Serial.printf("[WS] Invalid track %d (must be 0-15)\n", track);
      return;
    }
    spiMaster.clearTrackFilter(track);
    
    // Send response
    StaticJsonDocument<128> responseDoc;
    responseDoc["type"] = "trackFilterCleared";
    responseDoc["track"] = track;
    responseDoc["activeFilters"] = spiMaster.getActiveTrackFiltersCount();
    
    String output;
    serializeJson(responseDoc, output);
    if (ws) ws->textAll(output);
  }
  // ============= NEW: Per-Pad Filter Commands =============
  else if (cmd == "setPadFilter") {
    int pad = doc["pad"];
    if (pad < 0 || pad >= 24) {
      Serial.printf("[WS] Invalid pad %d (must be 0-23)\n", pad);
      return;
    }
    int filterType = doc.containsKey("type") ? doc["type"].as<int>() : doc["filterType"].as<int>();
    float cutoff = doc.containsKey("cutoff") ? doc["cutoff"].as<float>() : 1000.0f;
    float resonance = doc.containsKey("resonance") ? doc["resonance"].as<float>() : 1.0f;
    float gain = doc.containsKey("gain") ? doc["gain"].as<float>() : 0.0f;
    
    bool success = spiMaster.setPadFilter(pad, (FilterType)filterType, cutoff, resonance, gain);
    
    // Send response
    StaticJsonDocument<128> responseDoc;
    responseDoc["type"] = "padFilterSet";
    responseDoc["pad"] = pad;
    responseDoc["success"] = success;
    responseDoc["activeFilters"] = spiMaster.getActivePadFiltersCount();
    
    String output;
    serializeJson(responseDoc, output);
    if (ws) ws->textAll(output);
  }
  else if (cmd == "clearPadFilter") {
    int pad = doc["pad"];
    if (pad < 0 || pad >= 24) {
      Serial.printf("[WS] Invalid pad %d (must be 0-23)\n", pad);
      return;
    }
    spiMaster.clearPadFilter(pad);
    
    // Send response
    StaticJsonDocument<128> responseDoc;
    responseDoc["type"] = "padFilterCleared";
    responseDoc["pad"] = pad;
    responseDoc["activeFilters"] = spiMaster.getActivePadFiltersCount();
    
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
      const FilterPreset* fp = SPIMaster::getFilterPreset((FilterType)i);
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
    if (track < 0 || track >= 16 || step < 0 || step >= 16) {
      Serial.printf("[WS] Invalid track %d or step %d\n", track, step);
      return;
    }
    
    bool silent = doc.containsKey("silent") && doc["silent"].as<bool>();
    
    // Support writing to a specific pattern
    bool isBulkImport = doc.containsKey("pattern");
    if (isBulkImport) {
      int pattern = doc["pattern"].as<int>();
      if (pattern >= 0 && pattern < MAX_PATTERNS) {
        sequencer.setStepVelocity(pattern, track, step, velocity);
        yield(); // Prevent watchdog reset during bulk import
      }
      return;
    } else {
      sequencer.setStepVelocity(track, step, velocity);
      yield();
    }
    
    // Broadcast to all clients (skip in silent/bulk mode)
    if (!silent) {
      StaticJsonDocument<128> responseDoc;
      responseDoc["type"] = "stepVelocitySet";
      responseDoc["track"] = track;
      responseDoc["step"] = step;
      responseDoc["velocity"] = velocity;
      
      String output;
      serializeJson(responseDoc, output);
      if (ws) ws->textAll(output);
    }
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
  else if (cmd == "setStepVolumeLock") {
    int track = doc["track"];
    int step = doc["step"];
    bool enabled = doc.containsKey("enabled") ? doc["enabled"].as<bool>() : true;
    int volume = doc.containsKey("volume") ? doc["volume"].as<int>() : 100;
    if (track < 0 || track >= 16 || step < 0 || step >= 16) return;

    if (doc.containsKey("pattern")) {
      int pattern = doc["pattern"].as<int>();
      if (pattern >= 0 && pattern < MAX_PATTERNS) {
        sequencer.setStepVolumeLock(pattern, track, step, enabled, volume);
      }
    } else {
      sequencer.setStepVolumeLock(track, step, enabled, volume);
    }

    StaticJsonDocument<160> responseDoc;
    responseDoc["type"] = "stepVolumeLockSet";
    responseDoc["track"] = track;
    responseDoc["step"] = step;
    responseDoc["enabled"] = enabled;
    responseDoc["volume"] = constrain(volume, 0, 150);
    String output;
    serializeJson(responseDoc, output);
    if (ws) ws->textAll(output);
  }
  else if (cmd == "setStepProbability") {
    int track = doc["track"];
    int step = doc["step"];
    int probability = doc.containsKey("probability") ? doc["probability"].as<int>() : 100;
    if (track < 0 || track >= 16 || step < 0 || step >= 16) return;

    if (doc.containsKey("pattern")) {
      int pattern = doc["pattern"].as<int>();
      if (pattern >= 0 && pattern < MAX_PATTERNS) {
        sequencer.setStepProbability(pattern, track, step, probability);
      }
    } else {
      sequencer.setStepProbability(track, step, probability);
    }

    StaticJsonDocument<160> responseDoc;
    responseDoc["type"] = "stepProbabilitySet";
    responseDoc["track"] = track;
    responseDoc["step"] = step;
    responseDoc["probability"] = constrain(probability, 0, 100);
    String output;
    serializeJson(responseDoc, output);
    if (ws) ws->textAll(output);
  }
  else if (cmd == "setStepRatchet") {
    int track = doc["track"];
    int step = doc["step"];
    int ratchet = doc.containsKey("ratchet") ? doc["ratchet"].as<int>() : 1;
    if (track < 0 || track >= 16 || step < 0 || step >= 16) return;

    if (doc.containsKey("pattern")) {
      int pattern = doc["pattern"].as<int>();
      if (pattern >= 0 && pattern < MAX_PATTERNS) {
        sequencer.setStepRatchet(pattern, track, step, ratchet);
      }
    } else {
      sequencer.setStepRatchet(track, step, ratchet);
    }

    StaticJsonDocument<160> responseDoc;
    responseDoc["type"] = "stepRatchetSet";
    responseDoc["track"] = track;
    responseDoc["step"] = step;
    responseDoc["ratchet"] = constrain(ratchet, 1, 4);
    String output;
    serializeJson(responseDoc, output);
    if (ws) ws->textAll(output);
  }
  else if (cmd == "setHumanize") {
    int timing = doc.containsKey("timing") ? doc["timing"].as<int>() : 0;
    int velocity = doc.containsKey("velocity") ? doc["velocity"].as<int>() : 0;
    sequencer.setHumanize(timing, velocity);

    StaticJsonDocument<160> responseDoc;
    responseDoc["type"] = "humanizeSet";
    responseDoc["timing"] = sequencer.getHumanizeTimingMs();
    responseDoc["velocity"] = sequencer.getHumanizeVelocityAmount();
    String output;
    serializeJson(responseDoc, output);
    if (ws) ws->textAll(output);
  }
  else if (cmd == "getStepVolumeLock") {
    int track = doc["track"];
    int step = doc["step"];
    if (track < 0 || track >= 16 || step < 0 || step >= 16) return;

    bool enabled = sequencer.hasStepVolumeLock(track, step);
    int volume = enabled ? sequencer.getStepVolumeLock(track, step) : 0;

    StaticJsonDocument<160> responseDoc;
    responseDoc["type"] = "stepVolumeLock";
    responseDoc["track"] = track;
    responseDoc["step"] = step;
    responseDoc["enabled"] = enabled;
    responseDoc["volume"] = volume;
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
  // ============= NEW: Track Volume Commands =============
  else if (cmd == "setTrackVolume") {
    int track = doc["track"];
    int volume = doc["volume"];
    if (track < 0 || track >= 16) {
      Serial.printf("[WS] Invalid track %d (must be 0-15)\n", track);
      return;
    }
    
    sequencer.setTrackVolume(track, volume);
    
    // Broadcast to all clients
    StaticJsonDocument<128> responseDoc;
    responseDoc["type"] = "trackVolumeSet";
    responseDoc["track"] = track;
    responseDoc["volume"] = volume;
    
    String output;
    serializeJson(responseDoc, output);
    if (ws) ws->textAll(output);
  }
  else if (cmd == "getTrackVolume") {
    int track = doc["track"];
    
    uint8_t volume = sequencer.getTrackVolume(track);
    
    // Send response
    StaticJsonDocument<128> responseDoc;
    responseDoc["type"] = "trackVolume";
    responseDoc["track"] = track;
    responseDoc["volume"] = volume;
    
    String output;
    serializeJson(responseDoc, output);
    if (ws) ws->textAll(output);
  }
  else if (cmd == "getTrackVolumes") {
    // Send all track volumes
    StaticJsonDocument<256> responseDoc;
    responseDoc["type"] = "trackVolumes";
    
    JsonArray volumes = responseDoc.createNestedArray("volumes");
    for (int track = 0; track < 16; track++) {
      volumes.add(sequencer.getTrackVolume(track));
    }
    
    String output;
    serializeJson(responseDoc, output);
    if (ws) ws->textAll(output);
  }
  else if (cmd == "setMidiScan") {
    bool enabled = doc["enabled"];
    if (midiController) {
      midiController->setScanEnabled(enabled);
      Serial.printf("[MIDI] Scan %s\n", enabled ? "ENABLED" : "DISABLED");
      // Broadcast state to all clients
      StaticJsonDocument<128> responseDoc;
      responseDoc["type"] = "midiScan";
      responseDoc["enabled"] = enabled;
      String output;
      serializeJson(responseDoc, output);
      if (ws) ws->textAll(output);
    }
  }
}

// Actualizar o registrar cliente UDP
void WebInterface::updateUdpClient(IPAddress ip, uint16_t port) {
  String key = ip.toString();
  
  if (udpClients.find(key) != udpClients.end()) {
    // Cliente existente, solo actualizar timestamp y contador (sin Serial.printf)
    udpClients[key].lastSeen = millis();
    udpClients[key].packetCount++;
  } else {
    // Nuevo cliente - solo loguear nuevos
    UdpClient client;
    client.ip = ip;
    client.port = port;
    client.lastSeen = millis();
    client.packetCount = 1;
    udpClients[key] = client;
    Serial.printf("[UDP] New client: %s:%d (total: %d)\n", 
                  ip.toString().c_str(), port, udpClients.size());
  }
}

// Limpiar clientes UDP inactivos
void WebInterface::cleanupStaleUdpClients() {
  unsigned long now = millis();
  auto it = udpClients.begin();
  
  int cleaned = 0;
  while (it != udpClients.end()) {
    if (now - it->second.lastSeen > UDP_CLIENT_TIMEOUT) {
      // Serial.printf("[UDP] Client timeout: %s\n", it->first.c_str()); // Comentado
      it = udpClients.erase(it);
      cleaned++;
      if (cleaned % 2 == 0) yield(); // Yield cada 2 eliminaciones
    } else {
      ++it;
    }
  }
}

// Manejar paquetes UDP entrantes
void WebInterface::handleUdp() {
  int packetSize = udp.parsePacket();
  if (packetSize <= 0) return;
  
  char incomingPacket[512];
  int len = udp.read(incomingPacket, 511);
  if (len <= 0) return;
  
  incomingPacket[len] = 0;
  
  // Registrar cliente UDP
  updateUdpClient(udp.remoteIP(), udp.remotePort());
  
  // Parsear JSON
  StaticJsonDocument<512> doc;
  DeserializationError error = deserializeJson(doc, incomingPacket);
  
  if (!error) {
    processCommand(doc);
    
    // Respuesta compacta
    udp.beginPacket(udp.remoteIP(), udp.remotePort());
    udp.print("{\"s\":\"ok\"}");
    udp.endPacket();
  } else {
    udp.beginPacket(udp.remoteIP(), udp.remotePort());
    udp.print("{\"s\":\"err\"}");
    udp.endPacket();
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
  if (!initialized || !ws || ws->count() == 0) return;
  
  // Throttling: solo broadcast cada 100ms para baja latencia
  static uint32_t lastBroadcastTime = 0;
  static uint32_t messageCount = 0;
  uint32_t now = millis();
  
  messageCount++;
  
  // Solo enviar actualizaciones cada 100ms (máximo 10 por segundo)
  if (now - lastBroadcastTime < 100) {
    return; // Skip este broadcast
  }
  
  lastBroadcastTime = now;
  
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
  doc["totalMessages"] = messageCount; // Contador acumulado
  
  String output;
  serializeJson(doc, output);
  ws->textAll(output);
}

void WebInterface::broadcastMIDIDeviceStatus(bool connected, const MIDIDeviceInfo& info) {
  if (!initialized || !ws || ws->count() == 0) return;
  
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
                              "CB", "MA", "HC", "HT", "MC", "MT", "LC", "LT",
                              "X0", "X1", "X2", "X3", "X4", "X5", "X6", "X7"};
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
      const char* allFamilies[] = {"BD", "SD", "CH", "OH", "CP", "RS", "CL", "CY",
                                    "CB", "MA", "HC", "HT", "MC", "MT", "LC", "LT",
                                    "X0", "X1", "X2", "X3", "X4", "X5", "X6", "X7"};
      String filePath = "/" + String(allFamilies[uploadPad]) + "/" + uploadFilename;
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

