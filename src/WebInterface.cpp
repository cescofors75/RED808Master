/*
 * WebInterface.cpp
 * RED808 Web Interface con WebSockets
 */

#include "WebInterface.h"
#include "SPIMaster.h"
#include "Sequencer.h"
#include "SampleManager.h"
#include <esp_wifi.h>
#include <esp_heap_caps.h>

#ifndef ENABLE_PHYSICAL_BUTTONS
#define ENABLE_PHYSICAL_BUTTONS 1
#endif

// Timeout para clientes UDP (30 segundos sin actividad)
#define UDP_CLIENT_TIMEOUT 30000

// Hardening de estabilidad WS bajo estrés
static constexpr size_t kWsMaxTextBytes = 24576;
static constexpr size_t kWsMaxBinaryBytes = 256;
static constexpr unsigned long kFastMasterCmdMinMs = 8;
static constexpr unsigned long kFastTrackCmdMinMs = 8;
static constexpr unsigned long kFastPadCmdMinMs = 8;
static constexpr unsigned long kFastVolumeCmdMinMs = 8;

// ── Pre-allocated broadcast buffer in PSRAM to avoid heap fragmentation ──
// broadcastSequencerState() was the #1 cause of heap fragmentation:
// DynamicJsonDocument(8192) + String serialization = ~13KB alloc/free every cycle.
// Using static PSRAM buffers eliminates this churn entirely.
static char* _stateBuf = nullptr;
static constexpr size_t kStateBufSize = 8192;  // serialized JSON fits in ~5-6KB

// PSRAM allocator for ArduinoJson — documents allocated here never touch internal heap
struct PsramAllocator {
  void* allocate(size_t size) {
    return ps_malloc(size);
  }
  void deallocate(void* ptr) {
    free(ptr);
  }
  void* reallocate(void* ptr, size_t new_size) {
    return ps_realloc(ptr, new_size);
  }
};
using PsramJsonDocument = BasicJsonDocument<PsramAllocator>;

// Persistent JSON document in PSRAM — reused for every state broadcast.
// Allocated once in begin(), never freed. clear() between uses.
static PsramJsonDocument* _stateDoc = nullptr;

extern SPIMaster spiMaster;
extern Sequencer sequencer;

// Page‐transition broadcast pause: set when '/' is served, cleared after 2s
static volatile unsigned long pageTransitionMs = 0;
extern void dsqUploadPattern(int pattern);  // upload one pattern to Daisy sequencer

// Helper: lee todos los param locks de un step del Sequencer y los envía a Daisy
static void dsqSyncParamLock(int pat, int track, int step) {
    uint16_t ch = sequencer.getStepCutoffLock(pat, track, step);
    bool ce = (ch != 0 && ch != 1000);  // 1000 = valor default "sin lock"
    uint8_t rv = sequencer.getStepReverbSendLock(pat, track, step);
    bool re = (rv != 0);
    uint8_t vl = sequencer.getStepVolumeLock(pat, track, step);
    bool ve = (vl != 0);
    spiMaster.dsqSetParamLock((uint8_t)pat, (uint8_t)track, (uint8_t)step,
        ce, ch, re, rv, ve, vl);
}
extern SampleManager sampleManager;
extern void triggerPadWithLED(int track, uint8_t velocity);  // Función que enciende LED
extern void setLedMonoMode(bool enabled);
extern int8_t gTrackSynthEngine[16];
extern void setTrackSynthEngine(int track, int8_t engine);
extern void setAllTrackSynthEngines(int8_t engine);
static String gDaisyPadFiles[MAX_PADS];

static void clearTrackLoopStateForSynth(int track, AsyncWebSocket* ws) {
  if (track < 0 || track >= 16) {
    return;
  }

  const bool loopWasActive = sequencer.isLooping(track) || sequencer.isLoopPaused(track);
  if (sequencer.isLooping(track)) {
    sequencer.toggleLoop(track);
  }

  spiMaster.setPadLoop(track, false);
  spiMaster.stopSample(track);

  if (loopWasActive && ws) {
    StaticJsonDocument<160> responseDoc;
    responseDoc["type"] = "loopState";
    responseDoc["track"] = track;
    responseDoc["active"] = false;
    responseDoc["paused"] = false;
    responseDoc["loopType"] = (int)sequencer.getLoopType(track);
    String output;
    serializeJson(responseDoc, output);
    ws->textAll(output);
  }
}

static void setDaisyPadFile(int padIndex, const String& fileName) {
  if (padIndex < 0 || padIndex >= MAX_PADS) return;
  gDaisyPadFiles[padIndex] = fileName;
}

static void clearDaisyPadFiles() {
  for (int i = 0; i < MAX_PADS; i++) {
    gDaisyPadFiles[i] = "";
  }
}

static void clearDaisyPadFilesNotInMask(uint32_t loadedMask) {
  for (int i = 0; i < MAX_PADS; i++) {
    if ((loadedMask & (1UL << i)) == 0) {
      gDaisyPadFiles[i] = "";
    }
  }
}

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

static void sendWebAsset(AsyncWebServerRequest *request,
                         const char* routePath,
                         const char* contentType,
                         const char* cacheControl = "no-cache") {
  String fsPath = "/web";
  fsPath += routePath;

  // Pasamos la ruta SIN .gz para que AsyncFileResponse use su detección automática:
  // si fsPath no existe pero fsPath+".gz" sí (caso normal en data_gz/web), la librería
  // abre el .gz y establece Content-Encoding: gzip, _sendContentLength=true, _chunked=false,
  // garantizando un Content-Length correcto y el header Content-Disposition sin extensión .gz.
  if (!LittleFS.exists(fsPath) && !LittleFS.exists(fsPath + ".gz")) {
    request->send(404, "text/plain", "Not found");
    return;
  }

  AsyncWebServerResponse *response = request->beginResponse(LittleFS, fsPath, contentType);
  response->addHeader("Cache-Control", cacheControl);
  response->addHeader("Vary", "Accept-Encoding");
  request->send(response);
}

static void populateStateDocument(JsonDocument& doc) {
  SdStatusResponse sdStat = {};
  bool sdOk = spiMaster.getCachedSdStatus(sdStat);  // Non-blocking: reads cache updated by Core1
  uint32_t sdLoadedMask = sdOk ? sdStat.samplesLoaded : 0;
  if (sdOk) {
    clearDaisyPadFilesNotInMask(sdLoadedMask);
  }

  int sdLoadedCount = 0;
  for (int i = 0; i < MAX_PADS; i++) {
    if (sdLoadedMask & (1UL << i)) sdLoadedCount++;
  }

  int localLoadedCount = sampleManager.getLoadedSamplesCount();
  doc["type"] = "state";
  doc["playing"] = sequencer.isPlaying();
  doc["tempo"] = sequencer.getTempo();
  doc["pattern"] = sequencer.getCurrentPattern();
  doc["step"] = sequencer.getCurrentStep();
  doc["sequencerVolume"] = spiMaster.getSequencerVolume();
  doc["liveVolume"] = spiMaster.getLiveVolume();
  doc["samplesLoaded"] = max(localLoadedCount, sdLoadedCount);
  doc["memoryUsed"] = sampleManager.getTotalMemoryUsed();
  doc["psramFree"] = sampleManager.getFreePSRAM();
  doc["songMode"] = sequencer.isSongMode();
  doc["songLength"] = sequencer.getSongLength();
  doc["stepCount"] = sequencer.getPatternLength();
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

  JsonArray trackSynthEngines = doc.createNestedArray("trackSynthEngines");
  for (int track = 0; track < MAX_TRACKS; track++) {
    trackSynthEngines.add((int)gTrackSynthEngine[track]);
  }

  // Compact samples: only send loaded sample info (not empty pads)
  JsonArray sampleArray = doc.createNestedArray("samples");
  for (int pad = 0; pad < MAX_SAMPLES; pad++) {
    bool loadedLocal = sampleManager.isSampleLoaded(pad);
    bool loadedDaisy = (sdLoadedMask & (1UL << pad)) != 0;
    bool loaded = loadedLocal || loadedDaisy;
    if (loaded) {
      JsonObject sampleObj = sampleArray.createNestedObject();
      sampleObj["pad"] = pad;
      sampleObj["loaded"] = true;
      const char* localName = sampleManager.getSampleName(pad);
      String daisyName = gDaisyPadFiles[pad];
      const char* finalName = (loadedLocal && localName) ? localName : daisyName.c_str();
      sampleObj["name"] = finalName ? finalName : "";
      sampleObj["size"] = loadedLocal ? (sampleManager.getSampleLength(pad) * 2) : 0;
      sampleObj["format"] = detectSampleFormat(finalName);
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
  
  // Daisy SD card info (explicit SD status query in strict boundary mode)
  if (sdOk) {
    doc["sdPresent"] = (bool)sdStat.present;
    doc["sdKit"] = String(sdStat.currentKit);
    doc["sdPadsLoaded"] = sdLoadedCount;
    doc["sdLoadedMask"] = sdLoadedMask;
  } else {
    doc["sdPresent"] = false;
    doc["sdKit"] = "";
    doc["sdPadsLoaded"] = 0;
    doc["sdLoadedMask"] = 0;
  }
  
  doc["lfoActive"] = 0;

  // New state: synth engine mask (16-bit for 9 engines)
  doc["synthActiveMask"] = spiMaster.getSynthActiveMask16();

  // Song Chain state
  doc["songChainActive"] = sequencer.isSongChainActive();
  doc["songChainIdx"] = sequencer.getSongChainIdx();
  doc["songChainRepeat"] = sequencer.getSongChainRepeatCnt();
  doc["songChainCount"] = sequencer.getSongChainCount();

  // Choke groups
  JsonArray chokeArr = doc.createNestedArray("chokeGroups");
  for (int i = 0; i < 16; i++) {
    chokeArr.add(spiMaster.getChokeGroup(i));
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
}

static void sendSampleCounts(AsyncWebSocketClient* client) {
  if (!client || !isClientReady(client)) {
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
                         const char* /* staSSID */, const char* /* staPassword */,
                         unsigned long /* staTimeoutMs */) {
  _staConnected = false;

  // ── Modo AP exclusivo — máxima estabilidad ──
  WiFi.setSleep(false);
  WiFi.persistent(false);          // no guarda credenciales en NVS (evita flash corrupto)
  WiFi.mode(WIFI_OFF);
  delay(100);

  WiFi.mode(WIFI_AP);
  delay(50);

  IPAddress local_IP(192, 168, 4, 1);
  IPAddress gateway(192, 168, 4, 1);
  IPAddress subnet(255, 255, 255, 0);
  WiFi.softAPConfig(local_IP, gateway, subnet);
  WiFi.softAP(apSsid, apPassword, 1, 0, 4);   // canal 1, visible, max 4 clientes
  delay(200);

  // Protocolo b/g/n y beacon 100ms
  WiFi.setTxPower(WIFI_POWER_19_5dBm);
  esp_wifi_set_protocol(WIFI_IF_AP, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N);
  wifi_config_t conf;
  esp_wifi_get_config(WIFI_IF_AP, &conf);
  conf.ap.beacon_interval = 100;
  esp_wifi_set_config(WIFI_IF_AP, &conf);

  WiFi.setSleep(false);
  
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
  server->on("/", HTTP_GET, [this](AsyncWebServerRequest *request){
    // Pause periodic broadcasts during page transition to free TCP/Core0
    pageTransitionMs = millis();
    if (ws) ws->cleanupClients(0);  // close all old WS clients immediately
    sendWebAsset(request, "/index.html", "text/html", "no-cache, no-store, must-revalidate");
  });

  
  server->on("/index.html", HTTP_GET, [](AsyncWebServerRequest *request){
    sendWebAsset(request, "/index.html", "text/html", "no-cache, no-store, must-revalidate");
  });
  
  server->on("/app.js", HTTP_GET, [](AsyncWebServerRequest *request){
    sendWebAsset(request, "/app.js", "application/javascript", "max-age=86400, immutable");
  });
  
  server->on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request){
    sendWebAsset(request, "/style.css", "text/css", "max-age=86400, immutable");
  });
  
  server->on("/keyboard-controls.js", HTTP_GET, [](AsyncWebServerRequest *request){
    sendWebAsset(request, "/keyboard-controls.js", "application/javascript", "max-age=86400, immutable");
  });
  
  server->on("/keyboard-styles.css", HTTP_GET, [](AsyncWebServerRequest *request){
    sendWebAsset(request, "/keyboard-styles.css", "text/css", "max-age=86400, immutable");
  });
  
  server->on("/midi-import.js", HTTP_GET, [](AsyncWebServerRequest *request){
    sendWebAsset(request, "/midi-import.js", "application/javascript", "max-age=86400, immutable");
  });
  
  server->on("/chat-agent.js", HTTP_GET, [](AsyncWebServerRequest *request){
    sendWebAsset(request, "/chat-agent.js", "application/javascript", "max-age=86400, immutable");
  });
  
  server->on("/waveform-visualizer.js", HTTP_GET, [](AsyncWebServerRequest *request){
    sendWebAsset(request, "/waveform-visualizer.js", "application/javascript", "max-age=86400, immutable");
  });

  server->on("/synth-editor.js", HTTP_GET, [](AsyncWebServerRequest *request){
    sendWebAsset(request, "/synth-editor.js", "application/javascript", "max-age=86400, immutable");
  });

  server->on("/export-pattern.js", HTTP_GET, [](AsyncWebServerRequest *request){
    sendWebAsset(request, "/export-pattern.js", "application/javascript", "max-age=86400, immutable");
  });
  
  // Patchbay page
  server->on("/patchbay", HTTP_GET, [this](AsyncWebServerRequest *request){
    pageTransitionMs = millis();
    if (ws) ws->cleanupClients(0);
    sendWebAsset(request, "/patchbay.html", "text/html");
  });
  
  server->on("/patchbay.css", HTTP_GET, [](AsyncWebServerRequest *request){
    sendWebAsset(request, "/patchbay.css", "text/css", "max-age=86400, immutable");
  });
  
  server->on("/patchbay.js", HTTP_GET, [](AsyncWebServerRequest *request){
    sendWebAsset(request, "/patchbay.js", "application/javascript", "max-age=86400, immutable");
  });

  // Multiview page — redirect to .html served by serveStatic (avoids AsyncFileResponse 500 edge case)
  server->on("/multiview", HTTP_GET, [](AsyncWebServerRequest *request){
    request->redirect("/multiview.html");
  });

  server->on("/multiview.html", HTTP_GET, [](AsyncWebServerRequest *request){
    sendWebAsset(request, "/multiview.html", "text/html");
  });

  server->on("/multiview.css", HTTP_GET, [](AsyncWebServerRequest *request){
    sendWebAsset(request, "/multiview.css", "text/css", "max-age=86400, immutable");
  });
  
  server->on("/multiview.js", HTTP_GET, [](AsyncWebServerRequest *request){
    sendWebAsset(request, "/multiview.js", "application/javascript", "max-age=86400, immutable");
  });

  // Admin page
  server->on("/adm", HTTP_GET, [](AsyncWebServerRequest *request){
    sendWebAsset(request, "/admin.html", "text/html", "max-age=600");
  });

  server->on("/admin.css", HTTP_GET, [](AsyncWebServerRequest *request){
    sendWebAsset(request, "/admin.css", "text/css", "max-age=86400, immutable");
  });

  server->on("/admin.js", HTTP_GET, [](AsyncWebServerRequest *request){
    sendWebAsset(request, "/admin.js", "application/javascript", "max-age=86400, immutable");
  });

  server->on("/favicon.ico", HTTP_GET, [](AsyncWebServerRequest *request){
    sendWebAsset(request, "/favicon.ico", "image/x-icon", "max-age=86400, immutable");
  });

  // 404 para rutas desconocidas: evita que el navegador quede bloqueado esperando respuesta
  server->onNotFound([](AsyncWebServerRequest *request){
    request->send(404, "text/plain", "Not found");
  });
  
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

  server->on("/api/trigger", HTTP_GET, [](AsyncWebServerRequest *request){
    if (request->hasParam("pad")) {
      int pad = request->getParam("pad")->value().toInt();
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
      spiMaster.setTempo(tempo);
      request->send(200, "text/plain", "OK");
    }
  });
  
  server->on("/api/pattern", HTTP_POST, [](AsyncWebServerRequest *request){
    if (request->hasParam("index", true)) {
      int pattern = request->getParam("index", true)->value().toInt();
      sequencer.selectPattern(pattern);
      dsqUploadPattern(pattern);
      spiMaster.dsqSelectPattern((uint8_t)pattern);
      request->send(200, "text/plain", "OK");
    }
  });
  
  server->on("/api/sequencer", HTTP_POST, [](AsyncWebServerRequest *request){
    if (request->hasParam("action", true)) {
      String action = request->getParam("action", true)->value();
      if (action == "start") { sequencer.start(); dsqUploadPattern(sequencer.getCurrentPattern()); spiMaster.dsqControl(1); }
      else if (action == "stop") { sequencer.stop(); spiMaster.dsqControl(0); }
      request->send(200, "text/plain", "OK");
    }
  });
  
  server->on("/api/getPattern", HTTP_GET, [](AsyncWebServerRequest *request){
    int pattern = sequencer.getCurrentPattern();
    DynamicJsonDocument doc(4096);
    
    for (int track = 0; track < 16; track++) {
      JsonArray trackSteps = doc.createNestedArray(String(track));
      for (int step = 0; step < sequencer.getPatternLength(); step++) {
        trackSteps.add(sequencer.getStep(track, step));
      }
    }
    
    String output;
    serializeJson(doc, output);
    request->send(200, "application/json", output);
  });
  
  // Endpoint para info del sistema (para dashboard /adm)
  // ZERO SPI calls here — only reads cached data.
  // SPI polling (status/peaks/ping) runs on Core1 via SPIMaster::process().
  server->on("/api/sysinfo", HTTP_GET, [this](AsyncWebServerRequest *request){
    StaticJsonDocument<3072> doc;
    
    // Info de memoria
    doc["heapFree"] = ESP.getFreeHeap();
    doc["heapSize"] = ESP.getHeapSize();
    doc["psramFree"] = ESP.getFreePsram();
    doc["psramSize"] = ESP.getPsramSize();
    doc["flashSize"] = ESP.getFlashChipSize();
    
    // Info de WiFi (AP-only)
    doc["wifiMode"] = "AP";
    doc["ssid"] = WiFi.softAPSSID();
    doc["ip"] = WiFi.softAPIP().toString();
    doc["channel"] = WiFi.channel();
    doc["txPower"] = "19.5dBm";
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

    // Daisy realtime telemetry — uses cached data only (no SPI calls)
    doc["daisyConnected"] = spiMaster.isConnected();
    doc["daisyPingOk"] = spiMaster.isConnected();
    doc["daisyRttMs"] = spiMaster.getLastPingMs();
    doc["daisyVoices"] = spiMaster.getActiveVoices();
    doc["daisyCpu"] = spiMaster.getCpuLoad();
    doc["daisyMasterPeak"] = spiMaster.getMasterPeak();
    doc["daisyKit"] = String(spiMaster.getCurrentKitName());
    // Diagnostics
    StatusResponse daisyStat = {};
    spiMaster.getStatusSnapshot(daisyStat);
    doc["daisyPadsLoaded"]  = daisyStat.totalPadsLoaded;
    doc["daisyPadsMask"]    = daisyStat.padsLoadedMask;
    doc["daisySpiErrCnt"]   = (int)daisyStat.spiErrCnt;

    float peaks[16];
    spiMaster.getTrackPeaks(peaks, 16);
    JsonArray peaksArray = doc.createNestedArray("daisyTrackPeaks");
    for (int i = 0; i < 16; i++) {
      peaksArray.add(peaks[i]);
    }
    
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
  
  #if ENABLE_PHYSICAL_BUTTONS
  // ── GET /api/buttons — devuelve configuración guardada ─────────────────
  server->on("/api/buttons", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (LittleFS.exists("/buttons.json")) {
      AsyncWebServerResponse *resp = request->beginResponse(
          LittleFS, "/buttons.json", "application/json");
      resp->addHeader("Cache-Control", "no-cache");
      request->send(resp);
    } else {
      // Devolver config por defecto si no hay archivo guardado
      request->send(200, "application/json",
        "[{\"funcId\":1,\"colorOff\":16711680,\"colorOn\":65280,\"label\":\"PLAY/PAUSE\"},"
        "{\"funcId\":7,\"colorOff\":16711680,\"colorOn\":34816,\"label\":\"PREV+PLAY\"},"
        "{\"funcId\":6,\"colorOff\":16711680,\"colorOn\":34816,\"label\":\"NEXT+PLAY\"},"
        "{\"funcId\":2,\"colorOff\":16711680,\"colorOn\":16711680,\"label\":\"STOP\"}]");
    }
  });

  // ── POST /api/buttons — guarda y aplica nueva configuración ────────────
  server->on("/api/buttons", HTTP_POST,
    [](AsyncWebServerRequest *request) {},
    NULL,
    [this](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
      String body = String((char*)data, len);
      // Validar JSON básico
      StaticJsonDocument<1024> doc;
      if (deserializeJson(doc, body)) {
        request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
        return;
      }
      // Extraer array — acepta tanto [{...}] como {"buttons":[{...}]}
      JsonArray arr = doc.is<JsonArray>() ? doc.as<JsonArray>() : doc["buttons"].as<JsonArray>();
      if (arr.isNull()) {
        request->send(400, "application/json", "{\"error\":\"Missing buttons array\"}");
        return;
      }
      // Guardar SOLO el array plano en LittleFS
      String arrStr;
      serializeJson(arr, arrStr);
      File f = LittleFS.open("/buttons.json", "w");
      if (f) { f.print(arrStr); f.close(); }
      // Aplicar en tiempo real vía callback (pasa el array plano)
      if (_btnConfigCb) _btnConfigCb(arrStr);
      // Broadcast a todos los clientes web
      String bcast = "{\"type\":\"btnConfig\",\"buttons\":" + arrStr + "}";
      ws->textAll(bcast);
      request->send(200, "application/json", "{\"success\":true}");
    }
  );
  #endif

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

  // Pre-allocate state broadcast buffer in PSRAM (one-time, never freed)
  if (!_stateBuf) {
    _stateBuf = (char*)ps_malloc(kStateBufSize);
  }
  // Pre-allocate JSON document in PSRAM (one-time, reused via clear())
  if (!_stateDoc) {
    _stateDoc = new PsramJsonDocument(8192);
  }

  // Iniciar servidor UDP
  if (udp.begin(UDP_PORT)) {
  } else {
  }
  
  initialized = true;
  return true;
}

void WebInterface::onWebSocketEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, 
                                     AwsEventType type, void *arg, uint8_t *data, size_t len) {
  // Static reassembly buffer — declared at function level so both
  // WS_EVT_DISCONNECT and WS_EVT_DATA can access them.
  static uint8_t* _wsReassemblyBuf = nullptr;
  static size_t   _wsReassemblySize = 0;
  static uint32_t _wsReassemblyClientId = 0xFFFFFFFF;

  if (type == WS_EVT_CONNECT) {
    // ⚠️ LÍMITE DE 3 CLIENTES para estabilidad
    if (ws->count() > 3) {
      client->close(1008, "Max clients reached");
      return;
    }
    
    
    StaticJsonDocument<512> basicState;
    basicState["type"] = "connected";
    basicState["playing"] = sequencer.isPlaying();
    basicState["tempo"] = sequencer.getTempo();
    basicState["pattern"] = sequencer.getCurrentPattern();
    basicState["clientId"] = client->id();
    
    // Serialize to stack buffer — avoid heap String
    char connectBuf[256];
    size_t cLen = serializeJson(basicState, connectBuf, sizeof(connectBuf));
    if (isClientReady(client) && cLen > 0) {
      client->text(connectBuf, cLen);
    }
  } else if (type == WS_EVT_DISCONNECT) {
    // Free any orphaned reassembly buffer owned by this client
    // (prevents 24KB leak if client disconnects mid-fragment)
    if (_wsReassemblyBuf && _wsReassemblyClientId == client->id()) {
      free(_wsReassemblyBuf);
      _wsReassemblyBuf = nullptr;
      _wsReassemblyClientId = 0xFFFFFFFF;
    }
  } else if (type == WS_EVT_DATA) {
    AwsFrameInfo *info = (AwsFrameInfo*)arg;

    // Guard rails de payload para evitar OOM/fragmentación bajo cargas anómalas
    if (info->opcode == WS_TEXT && info->len > kWsMaxTextBytes) {
      return;
    }
    if (info->opcode == WS_BINARY && info->len > kWsMaxBinaryBytes) {
      return;
    }
    
    // --- Frame reassembly for fragmented WebSocket messages ---
    // NOTE: static vars are at function level (see top of onWebSocketEvent).
    // We track client id to detect cross-client fragment interleaving.

    bool _wsFreeAfter = false;
    
    // Safe buffer variables (used for WS_TEXT to avoid data[len]=0 overflow)
    char* safeData = nullptr;
    bool safeFreeNeeded = false;
    
    if (!(info->final && info->index == 0 && info->len == len)) {
      // Fragmented frame - reassemble chunks into complete message
      if (info->index == 0) {
        if (info->opcode == WS_TEXT && info->len > kWsMaxTextBytes) {
          return;
        }
        // If a different client already owns the buffer, discard it to avoid corruption
        if (_wsReassemblyBuf && _wsReassemblyClientId != client->id()) {
          free(_wsReassemblyBuf);
          _wsReassemblyBuf = nullptr;
        } else if (_wsReassemblyBuf) {
          free(_wsReassemblyBuf); _wsReassemblyBuf = nullptr;
        }
        if (ESP.getFreeHeap() < (uint32_t)(info->len + 4096)) {
          return;
        }
        _wsReassemblyBuf = (uint8_t*)malloc(info->len + 2); // +2 for null terminator safety
        if (!_wsReassemblyBuf) {
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
      
    // 1. MANEJO DE BINARIO (Baja latencia para Triggers)
    if (info->opcode == WS_BINARY) {
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
          if (ESP.getFreeHeap() < 35000) {
            StaticJsonDocument<64> errDoc;
            errDoc["type"] = "error"; errDoc["msg"] = "low_heap";
            String errStr; serializeJson(errDoc, errStr);
            if (isClientReady(client)) client->text(errStr);
            if (safeFreeNeeded) free(safeData);
            if (_wsFreeAfter && _wsReassemblyBuf) { free(_wsReassemblyBuf); _wsReassemblyBuf = nullptr; }
            return;
          }
          DynamicJsonDocument bulkDoc(32768);  // Larger to support 64-step patterns
          DeserializationError bulkErr = deserializeJson(bulkDoc, (char*)data);
          if (!bulkErr) {
            int pattern = bulkDoc["p"].as<int>();
            // p = -1 means current pattern (single bar import)
            if (pattern < 0) pattern = sequencer.getCurrentPattern();
            if (pattern >= 0 && pattern < MAX_PATTERNS) {
              bool stepsData[MAX_TRACKS][STEPS_PER_PATTERN] = {};
              uint8_t velsData[MAX_TRACKS][STEPS_PER_PATTERN];
              memset(velsData, 127, sizeof(velsData));
              memset(velsData, 127, sizeof(velsData));
              
              JsonArray sArr = bulkDoc["s"].as<JsonArray>();
              JsonArray vArr = bulkDoc["v"].as<JsonArray>();
              
              for (int t = 0; t < 16 && t < (int)sArr.size(); t++) {
                JsonArray trackSteps = sArr[t].as<JsonArray>();
                for (int s = 0; s < STEPS_PER_PATTERN && s < (int)trackSteps.size(); s++) {
                  stepsData[t][s] = trackSteps[s].as<int>() != 0;
                }
                if (vArr && t < (int)vArr.size()) {
                  JsonArray trackVels = vArr[t].as<JsonArray>();
                  for (int s = 0; s < STEPS_PER_PATTERN && s < (int)trackVels.size(); s++) {
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
              StaticJsonDocument<64> err; err["type"] = "error"; err["msg"] = "low_heap";
              String errStr; serializeJson(err, errStr);
              if (isClientReady(client)) client->text(errStr);
              if (safeFreeNeeded) free(safeData);
              if (_wsFreeAfter && _wsReassemblyBuf) { free(_wsReassemblyBuf); _wsReassemblyBuf = nullptr; }
              return;
            }
            yield();
            int stepCount = sequencer.getPatternLength();
            DynamicJsonDocument responseDoc(stepCount > 16 ? 32768 : 16384);  // Dynamic size for step count
            responseDoc["stepCount"] = stepCount;  // 6 structures × 16×16 values needs 13-15KB
            responseDoc["type"] = "pattern";
            responseDoc["index"] = pattern;
            
            // Send steps (active/inactive) - 16 tracks activos
            for (int track = 0; track < 16; track++) {
              JsonArray trackSteps = responseDoc.createNestedArray(String(track));
              for (int step = 0; step < stepCount; step++) {
                trackSteps.add(sequencer.getStep(track, step));
              }
            }
            
            // Send velocities
            JsonObject velocitiesObj = responseDoc.createNestedObject("velocities");
            for (int track = 0; track < 16; track++) {
              JsonArray trackVels = velocitiesObj.createNestedArray(String(track));
              for (int step = 0; step < stepCount; step++) {
                trackVels.add(sequencer.getStepVelocity(track, step));
              }
            }
            
            // Send note lengths (1=full, 2=half, 4=quarter, 8=eighth)
            JsonObject noteLensObj = responseDoc.createNestedObject("noteLens");
            for (int track = 0; track < 16; track++) {
              JsonArray trackNL = noteLensObj.createNestedArray(String(track));
              for (int step = 0; step < stepCount; step++) {
                trackNL.add(sequencer.getStepNoteLen(track, step));
              }
            }

            JsonObject volumeLocksObj = responseDoc.createNestedObject("volumeLocks");
            for (int track = 0; track < 16; track++) {
              JsonArray trackLocks = volumeLocksObj.createNestedArray(String(track));
              for (int step = 0; step < stepCount; step++) {
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
              for (int step = 0; step < stepCount; step++) {
                trackProb.add(sequencer.getStepProbability(track, step));
              }
            }

            JsonObject ratchetsObj = responseDoc.createNestedObject("ratchets");
            for (int track = 0; track < 16; track++) {
              JsonArray trackRat = ratchetsObj.createNestedArray(String(track));
              for (int step = 0; step < stepCount; step++) {
                trackRat.add(sequencer.getStepRatchet(track, step));
              }
            }

            JsonObject cutoffLocksObj = responseDoc.createNestedObject("cutoffLocks");
            for (int track = 0; track < 16; track++) {
              JsonArray trackLocks = cutoffLocksObj.createNestedArray(String(track));
              for (int step = 0; step < stepCount; step++) {
                if (sequencer.hasStepCutoffLock(track, step)) {
                  trackLocks.add((int)sequencer.getStepCutoffLock(track, step));
                } else {
                  trackLocks.add(-1);
                }
              }
            }

            JsonObject reverbLocksObj = responseDoc.createNestedObject("reverbLocks");
            for (int track = 0; track < 16; track++) {
              JsonArray trackLocks = reverbLocksObj.createNestedArray(String(track));
              for (int step = 0; step < stepCount; step++) {
                if (sequencer.hasStepReverbSendLock(track, step)) {
                  trackLocks.add((int)sequencer.getStepReverbSendLock(track, step));
                } else {
                  trackLocks.add(-1);
                }
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
            // State doc lives in PSRAM — safe to send regardless of heap
            if (isClientReady(client)) {
              sendSequencerStateToClient(client);
            }

            // Enviar estado de MIDI scan (stack buffer, no heap)
            if (midiController && isClientReady(client)) {
              StaticJsonDocument<128> midiScanDoc;
              midiScanDoc["type"] = "midiScan";
              midiScanDoc["enabled"] = midiController->isScanEnabled();
              char midiBuf[64];
              size_t mLen = serializeJson(midiScanDoc, midiBuf, sizeof(midiBuf));
              if (mLen > 0) client->text(midiBuf, mLen);
            }
          }
          else if (cmd == "getSampleCounts") {
            // Nuevo comando para obtener conteos de samples
            sendSampleCounts(client);
          }
          else if (cmd == "getSamples") {
            // Obtener lista de samples de una familia desde LittleFS
            const char* family = doc["family"];
            int padIndex = doc["pad"];

            DynamicJsonDocument responseDoc(4096);  // Heap-allocated, flexible size
            responseDoc["type"] = "sampleList";
            responseDoc["family"] = family;
            responseDoc["pad"] = padIndex;

            if (!family) {
              responseDoc.createNestedArray("samples");
              String emptyOut;
              serializeJson(responseDoc, emptyOut);
              if (isClientReady(client)) client->text(emptyOut);
              else ws->textAll(emptyOut);
              if (safeFreeNeeded) free(safeData);
              if (_wsFreeAfter && _wsReassemblyBuf) { free(_wsReassemblyBuf); _wsReassemblyBuf = nullptr; }
              return;
            }

            String path = String("/") + String(family);

            File dir = LittleFS.open(path, "r");

            if (dir && dir.isDirectory()) {
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
            } else {
              responseDoc.createNestedArray("samples");  // folder not found → empty list
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
  
  // All memory lives in PSRAM — zero heap allocation in this path
  if (_stateDoc && _stateBuf) {
    _stateDoc->clear();
    populateStateDocument(*_stateDoc);
    size_t len = serializeJson(*_stateDoc, _stateBuf, kStateBufSize);
    if (len > 0 && len < kStateBufSize) {
      ws->textAll(_stateBuf, len);
    }
  }
}

void WebInterface::sendSequencerStateToClient(AsyncWebSocketClient* client) {
  if (!initialized || !ws || !isClientReady(client)) return;
  
  // All memory in PSRAM — zero heap allocation
  if (_stateDoc && _stateBuf) {
    _stateDoc->clear();
    populateStateDocument(*_stateDoc);
    size_t len = serializeJson(*_stateDoc, _stateBuf, kStateBufSize);
    if (len > 0 && len < kStateBufSize) {
      client->text(_stateBuf, len);
    }
  }
}

void WebInterface::broadcastPadTrigger(int pad) {
  if (!initialized || !ws || ws->count() == 0) return;
  if (ESP.getFreeHeap() < 20000) return;
  
  // Stack buffer — zero heap allocation
  char buf[48];
  int len = snprintf(buf, sizeof(buf), "{\"type\":\"pad\",\"pad\":%d}", pad);
  ws->textAll(buf, len);
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

  // Skip periodic broadcasts during page transitions (2s window)
  bool pageLoading = (pageTransitionMs != 0 && (now - pageTransitionMs) < 2000);
  if (pageTransitionMs != 0 && (now - pageTransitionMs) >= 2000) {
    pageTransitionMs = 0;  // clear flag
  }
  
  // Broadcast audio levels for all WS clients (main UI + /adm)
  static unsigned long lastAudioLevels = 0;
  if (!pageLoading && now - lastAudioLevels >= 100 && ws->count() > 0) {
    lastAudioLevels = now;

    // Peak data is polled by SPIMaster::process() on Core1 —
    // we just read the cached values here (zero SPI blocking).

    if (ESP.getFreeHeap() >= 20000) {
      uint8_t levelBuf[18];
      levelBuf[0] = 0xAA;

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
  // Limpiar WebSocket clients desconectados cada segundo
  static unsigned long lastWsCleanup = 0;
  if (now - lastWsCleanup > 1000) {
    ws->cleanupClients(3);  // máx 3 clientes, cerrar el resto
    lastWsCleanup = now;
  }

  // Broadcast estado periódico (15s) — red de seguridad para sample info.
  // Reducido de 5s a 15s para minimizar heap churn (~13KB por llamada).
  static unsigned long lastPeriodicState = 0;
  if (!pageLoading && ws->count() > 0 && now - lastPeriodicState >= 15000) {
    lastPeriodicState = now;
    broadcastSequencerState();
  }

  // ── Heap monitor: log cada 10s para diagnosticar fugas ──
  static unsigned long lastHeapLog = 0;
  static uint32_t minHeapSeen = 0xFFFFFFFF;
  static uint8_t lowHeapStrikes = 0;
  if (now - lastHeapLog > 10000) {
    lastHeapLog = now;
    uint32_t h = ESP.getFreeHeap();
    size_t maxBlock = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    if (h < minHeapSeen) minHeapSeen = h;
    
    // Safety net: if largest contiguous block stays critically low for
    // 3 consecutive checks (30s), gracefully restart to avoid total lockup
    if (maxBlock < 8000) {
      lowHeapStrikes++;
      if (lowHeapStrikes >= 3) {
        ESP.restart();
      }
    } else {
      lowHeapStrikes = 0;
    }
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

    // ── Modo AP: verificar que siga activo ──
    if (WiFi.getMode() != WIFI_AP) {
      WiFi.mode(WIFI_AP);
      WiFi.softAP("RED808", "red808esp32", 1, 0, 4);
      WiFi.setSleep(false);
    }
  }
}

String WebInterface::getIP() {
  return WiFi.softAPIP().toString();
}

// Procesar comandos JSON (compartido entre WebSocket y UDP)
void WebInterface::processCommand(const JsonDocument& doc) {
  // ── Heap guard: si queda poca memoria, descartamos el comando ──
  if (ESP.getFreeHeap() < 20000) {
    return;
  }

  String cmd = doc["cmd"];

  static unsigned long lastMasterFxCmdMs = 0;
  static unsigned long lastTrackFxCmdMs[24] = {};
  static unsigned long lastPadFxCmdMs = 0;
  static unsigned long lastVolumeCmdMs = 0;
  const unsigned long nowCmdMs = millis();

  bool isMasterFxFast =
    (cmd == "setFilter") ||
    (cmd == "setFilterCutoff") ||
    (cmd == "setFilterResonance") ||
    (cmd == "setBitCrush") ||
    (cmd == "setDistortion") ||
    (cmd == "setSampleRate") ||
    (cmd == "setDelayTime") ||
    (cmd == "setDelayFeedback") ||
    (cmd == "setDelayMix") ||
    (cmd == "setPhaserRate") ||
    (cmd == "setPhaserDepth") ||
    (cmd == "setPhaserFeedback") ||
    (cmd == "setFlangerRate") ||
    (cmd == "setFlangerDepth") ||
    (cmd == "setFlangerFeedback") ||
    (cmd == "setFlangerMix") ||
    (cmd == "setCompressorThreshold") ||
    (cmd == "setCompressorRatio") ||
    (cmd == "setCompressorAttack") ||
    (cmd == "setCompressorRelease") ||
    (cmd == "setCompressorMakeupGain");

  bool isTrackFxFast =
    (cmd == "setTrackFilter") ||
    (cmd == "setTrackDistortion") ||
    (cmd == "setTrackBitCrush") ||
    (cmd == "setTrackEcho") ||
    (cmd == "setTrackFlanger") ||
    (cmd == "setTrackCompressor");

  bool isPadFxFast =
    (cmd == "setPadFilter") ||
    (cmd == "setPadDistortion") ||
    (cmd == "setPadBitCrush");

  bool isVolumeFast =
    (cmd == "setSequencerVolume") ||
    (cmd == "setLiveVolume") ||
    (cmd == "setTrackVolume") ||
    (cmd == "setVolume") ||
    (cmd == "setLivePitch");

  if (isMasterFxFast) {
    if (nowCmdMs - lastMasterFxCmdMs < kFastMasterCmdMinMs) return;
    lastMasterFxCmdMs = nowCmdMs;
  } else if (isTrackFxFast) {
    int tIdx = doc.containsKey("track") ? (int)doc["track"] : -1;
    if (tIdx >= 0 && tIdx < 24) {
      if (nowCmdMs - lastTrackFxCmdMs[tIdx] < kFastTrackCmdMinMs) return;
      lastTrackFxCmdMs[tIdx] = nowCmdMs;
    }
  } else if (isPadFxFast) {
    if (nowCmdMs - lastPadFxCmdMs < kFastPadCmdMinMs) return;
    lastPadFxCmdMs = nowCmdMs;
  } else if (isVolumeFast) {
    if (nowCmdMs - lastVolumeCmdMs < kFastVolumeCmdMinMs) return;
    lastVolumeCmdMs = nowCmdMs;
  }
  
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
    if (track < 0 || track >= MAX_TRACKS || step < 0 || step >= STEPS_PER_PATTERN) return;
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
        spiMaster.dsqSetStep((uint8_t)pattern, (uint8_t)track, (uint8_t)step,
            active ? 1 : 0, 100, 1, 100);
        sequencer.selectPattern(savedPattern);
        yield(); // Prevent watchdog reset during bulk import
      }
    } else {
      sequencer.setStep(track, step, active);
      sequencer.setStepNoteLen(track, step, noteLen);
      spiMaster.dsqSetStep((uint8_t)sequencer.getCurrentPattern(), (uint8_t)track, (uint8_t)step,
          active ? 1 : 0,
          sequencer.getStepVelocity(sequencer.getCurrentPattern(), track, step),
          noteLen,
          sequencer.getStepProbability(sequencer.getCurrentPattern(), track, step));
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
    dsqUploadPattern(sequencer.getCurrentPattern());
    spiMaster.dsqControl(1);
    StaticJsonDocument<96> resp;
    resp["type"] = "playState";
    resp["playing"] = true;
    String out; serializeJson(resp, out);
    if (ws) ws->textAll(out);
  }
  else if (cmd == "stop") {
    sequencer.stop();
    spiMaster.dsqControl(0);
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
    spiMaster.setTempo(tempo);
    StaticJsonDocument<96> resp;
    resp["type"] = "tempoChange";
    resp["tempo"] = tempo;
    String out; serializeJson(resp, out);
    if (ws) ws->textAll(out);
  }
  else if (cmd == "setStepCount") {
    int count = doc["count"];
    if (count == 16 || count == 32 || count == 64) {
      sequencer.setPatternLength(count);
      spiMaster.dsqSetLength((uint8_t)count);
      StaticJsonDocument<96> resp;
      resp["type"] = "stepCount";
      resp["count"] = count;
      String out; serializeJson(resp, out);
      if (ws) ws->textAll(out);
    }
  }
  else if (cmd == "selectPattern") {
    int pattern = doc["index"];
    sequencer.selectPattern(pattern);
    dsqUploadPattern(pattern);
    spiMaster.dsqSelectPattern((uint8_t)pattern);
    
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
      return;
    }
    yield();
    int stepCount = sequencer.getPatternLength();
    // 5 data structures × 16 tracks × 16 steps — needs ~11-13KB ArduinoJson pool
    DynamicJsonDocument patternDoc(stepCount > 16 ? 32768 : 14336);
    patternDoc["type"] = "pattern";
    patternDoc["index"] = pattern;
    patternDoc["stepCount"] = stepCount;
    
    for (int track = 0; track < 16; track++) {
      JsonArray trackSteps = patternDoc.createNestedArray(String(track));
      for (int step = 0; step < stepCount; step++) {
        trackSteps.add(sequencer.getStep(track, step));
      }
    }
    
    JsonObject velocitiesObj = patternDoc.createNestedObject("velocities");
    for (int track = 0; track < 16; track++) {
      JsonArray trackVels = velocitiesObj.createNestedArray(String(track));
      for (int step = 0; step < stepCount; step++) {
        trackVels.add(sequencer.getStepVelocity(track, step));
      }
    }

    JsonObject volumeLocksObj = patternDoc.createNestedObject("volumeLocks");
    for (int track = 0; track < 16; track++) {
      JsonArray trackLocks = volumeLocksObj.createNestedArray(String(track));
      for (int step = 0; step < stepCount; step++) {
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
      for (int step = 0; step < stepCount; step++) {
        trackProb.add(sequencer.getStepProbability(track, step));
      }
    }

    JsonObject ratchetsObj = patternDoc.createNestedObject("ratchets");
    for (int track = 0; track < 16; track++) {
      JsonArray trackRat = ratchetsObj.createNestedArray(String(track));
      for (int step = 0; step < stepCount; step++) {
        trackRat.add(sequencer.getStepRatchet(track, step));
      }
    }

    JsonObject cutoffLocksObj = patternDoc.createNestedObject("cutoffLocks");
    for (int track = 0; track < 16; track++) {
      JsonArray trackLocks = cutoffLocksObj.createNestedArray(String(track));
      for (int step = 0; step < stepCount; step++) {
        if (sequencer.hasStepCutoffLock(track, step)) {
          trackLocks.add((int)sequencer.getStepCutoffLock(track, step));
        } else {
          trackLocks.add(-1);
        }
      }
    }

    JsonObject reverbLocksObj = patternDoc.createNestedObject("reverbLocks");
    for (int track = 0; track < 16; track++) {
      JsonArray trackLocks = reverbLocksObj.createNestedArray(String(track));
      for (int step = 0; step < stepCount; step++) {
        if (sequencer.hasStepReverbSendLock(track, step)) {
          trackLocks.add((int)sequencer.getStepReverbSendLock(track, step));
        } else {
          trackLocks.add(-1);
        }
      }
    }
    
    String patternOutput;
    serializeJson(patternDoc, patternOutput);
    if (ws && ws->count() > 0) ws->textAll(patternOutput);
  }
  else if (cmd == "loadSample") {
    const char* family   = doc["family"];
    const char* filename = doc["filename"];
    int padIndex = doc["pad"];
    if (padIndex < 0 || padIndex >= 16) return;
    if (!family || !filename) return;

    yield();

    // Build LittleFS path: /<family>/<filename>
    // We transfer the audio data directly from ESP32 LittleFS → Daisy RAM via SPI.
    // This does NOT touch the Daisy QSPI flash, so the original boot sample is
    // preserved and reloads on reboot ("sin machacar el antiguo").
    String fullPath = String("/") + String(family) + "/" + String(filename);
    bool ok = sampleManager.loadSample(fullPath.c_str(), padIndex);

    if (ok) {
      // Apply trim markers if the user dragged the waveform start/end
      float trimStart = doc.containsKey("trimStart") ? (float)doc["trimStart"] : 0.0f;
      float trimEnd   = doc.containsKey("trimEnd")   ? (float)doc["trimEnd"]   : 1.0f;
      if (trimStart > 0.001f || trimEnd < 0.999f) {
        sampleManager.trimSample(padIndex, trimStart, trimEnd);
      }

      uint32_t sampleLen = sampleManager.getSampleLength(padIndex);

      StaticJsonDocument<256> responseDoc;
      responseDoc["type"]     = "sampleLoaded";
      responseDoc["pad"]      = padIndex;
      responseDoc["filename"] = filename;
      responseDoc["size"]     = sampleLen * 2;           // bytes
      responseDoc["format"]   = detectSampleFormat(filename);
      responseDoc["quality"]  = "LittleFS";

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
    } else {
      // Create /xtra if it doesn't exist
      LittleFS.mkdir("/xtra");
      responseDoc.createNestedArray("samples");
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

    // Notificar al cliente que empieza la transferencia SPI a la Daisy
    {
      StaticJsonDocument<128> tDoc;
      tDoc["type"] = "xtraTransferring";
      tDoc["pad"]  = padIndex;
      String tOut; serializeJson(tDoc, tOut);
      if (ws) ws->textAll(tOut);
    }

    if (sampleManager.loadSample(fullPath.c_str(), padIndex)) {
      // Transferencia completada (ya incluye delay post-CMD_SAMPLE_END)
      StaticJsonDocument<256> responseDoc;
      responseDoc["type"]     = "xtraReady";
      responseDoc["pad"]      = padIndex;
      responseDoc["filename"] = filename;
      responseDoc["size"]     = sampleManager.getSampleLength(padIndex) * 2;
      String output; serializeJson(responseDoc, output);
      if (ws) ws->textAll(output);

      // También enviamos sampleLoaded para compatibilidad con waveform cache etc.
      responseDoc["type"] = "sampleLoaded";
      output = ""; serializeJson(responseDoc, output);
      if (ws) ws->textAll(output);
    }
  }
  else if (cmd == "mute") {
    int track = doc["track"];
    if (track < 0 || track >= 16) return;
    yield();
    bool muted = doc["value"];
    sequencer.muteTrack(track, muted);
    spiMaster.dsqSetMute((uint8_t)track, muted);
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
      bool loopNow = sequencer.isLooping(track);

      // ALSO set audio-level loop on Daisy so sample loops continuously
      // even when sequencer is stopped
      spiMaster.setPadLoop(track, loopNow);
      if (loopNow) {
        // Trigger the sample so it starts playing immediately
        spiMaster.triggerSampleLive(track, 127);
      }
      
      StaticJsonDocument<192> responseDoc;
      responseDoc["type"] = "loopState";
      responseDoc["track"] = track;
      responseDoc["active"] = loopNow;
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
  else if (cmd == "setReverbActive") {
    bool active = doc["value"];
    spiMaster.setReverbActive(active);
    StaticJsonDocument<96> resp;
    resp["type"] = "masterFx"; resp["param"] = "reverbActive"; resp["value"] = active;
    String out; serializeJson(resp, out);
    if (ws) ws->textAll(out);
  }
  else if (cmd == "setReverbFeedback") {
    float v = doc["value"];
    float feedback = (v > 1.0f) ? (v / 100.0f) : v;
    spiMaster.setReverbFeedback(feedback);
    StaticJsonDocument<96> resp;
    resp["type"] = "masterFx"; resp["param"] = "reverbFeedback"; resp["value"] = feedback;
    String out; serializeJson(resp, out);
    if (ws) ws->textAll(out);
  }
  else if (cmd == "setReverbLpFreq") {
    float hz = doc["value"];
    spiMaster.setReverbLpFreq(hz);
    StaticJsonDocument<96> resp;
    resp["type"] = "masterFx"; resp["param"] = "reverbLpFreq"; resp["value"] = hz;
    String out; serializeJson(resp, out);
    if (ws) ws->textAll(out);
  }
  else if (cmd == "setReverbMix") {
    float v = doc["value"];
    float mix = (v > 1.0f) ? (v / 100.0f) : v;
    spiMaster.setReverbMix(mix);
    StaticJsonDocument<96> resp;
    resp["type"] = "masterFx"; resp["param"] = "reverbMix"; resp["value"] = mix;
    String out; serializeJson(resp, out);
    if (ws) ws->textAll(out);
  }
  else if (cmd == "setChorusActive") {
    bool active = doc["value"];
    spiMaster.setChorusActive(active);
    StaticJsonDocument<96> resp;
    resp["type"] = "masterFx"; resp["param"] = "chorusActive"; resp["value"] = active;
    String out; serializeJson(resp, out);
    if (ws) ws->textAll(out);
  }
  else if (cmd == "setChorusRate") {
    float rate = doc["value"];
    spiMaster.setChorusRate(rate);
    StaticJsonDocument<96> resp;
    resp["type"] = "masterFx"; resp["param"] = "chorusRate"; resp["value"] = rate;
    String out; serializeJson(resp, out);
    if (ws) ws->textAll(out);
  }
  else if (cmd == "setChorusDepth") {
    float v = doc["value"];
    float depth = (v > 1.0f) ? (v / 100.0f) : v;
    spiMaster.setChorusDepth(depth);
    StaticJsonDocument<96> resp;
    resp["type"] = "masterFx"; resp["param"] = "chorusDepth"; resp["value"] = depth;
    String out; serializeJson(resp, out);
    if (ws) ws->textAll(out);
  }
  else if (cmd == "setChorusMix") {
    float v = doc["value"];
    float mix = (v > 1.0f) ? (v / 100.0f) : v;
    spiMaster.setChorusMix(mix);
    StaticJsonDocument<96> resp;
    resp["type"] = "masterFx"; resp["param"] = "chorusMix"; resp["value"] = mix;
    String out; serializeJson(resp, out);
    if (ws) ws->textAll(out);
  }
  else if (cmd == "setTremoloActive") {
    bool active = doc["value"];
    spiMaster.setTremoloActive(active);
    StaticJsonDocument<96> resp;
    resp["type"] = "masterFx"; resp["param"] = "tremoloActive"; resp["value"] = active;
    String out; serializeJson(resp, out);
    if (ws) ws->textAll(out);
  }
  else if (cmd == "setTremoloRate") {
    float rate = doc["value"];
    spiMaster.setTremoloRate(rate);
    StaticJsonDocument<96> resp;
    resp["type"] = "masterFx"; resp["param"] = "tremoloRate"; resp["value"] = rate;
    String out; serializeJson(resp, out);
    if (ws) ws->textAll(out);
  }
  else if (cmd == "setTremoloDepth") {
    float v = doc["value"];
    float depth = (v > 1.0f) ? (v / 100.0f) : v;
    spiMaster.setTremoloDepth(depth);
    StaticJsonDocument<96> resp;
    resp["type"] = "masterFx"; resp["param"] = "tremoloDepth"; resp["value"] = depth;
    String out; serializeJson(resp, out);
    if (ws) ws->textAll(out);
  }
  else if (cmd == "setWavefolderGain") {
    float gain = doc["value"];
    spiMaster.setWaveFolderGain(gain);
    StaticJsonDocument<96> resp;
    resp["type"] = "masterFx"; resp["param"] = "wavefolderGain"; resp["value"] = gain;
    String out; serializeJson(resp, out);
    if (ws) ws->textAll(out);
  }
  else if (cmd == "setLimiterActive") {
    bool active = doc["value"];
    spiMaster.setLimiterActive(active);
    StaticJsonDocument<96> resp;
    resp["type"] = "masterFx"; resp["param"] = "limiterActive"; resp["value"] = active;
    String out; serializeJson(resp, out);
    if (ws) ws->textAll(out);
  }
  else if (cmd == "setTrackReverbSend") {
    int track = doc["track"];
    int level = doc["value"];
    if (track >= 0 && track < 16) {
      spiMaster.setTrackReverbSend(track, (uint8_t)constrain(level, 0, 100));
      StaticJsonDocument<128> resp;
      resp["type"] = "trackFxSet";
      resp["track"] = track;
      resp["fx"] = "reverbSend";
      resp["value"] = constrain(level, 0, 100);
      String out; serializeJson(resp, out);
      if (ws) ws->textAll(out);
    }
  }
  else if (cmd == "setTrackDelaySend") {
    int track = doc["track"];
    int level = doc["value"];
    if (track >= 0 && track < 16) {
      spiMaster.setTrackDelaySend(track, (uint8_t)constrain(level, 0, 100));
      StaticJsonDocument<128> resp;
      resp["type"] = "trackFxSet";
      resp["track"] = track;
      resp["fx"] = "delaySend";
      resp["value"] = constrain(level, 0, 100);
      String out; serializeJson(resp, out);
      if (ws) ws->textAll(out);
    }
  }
  else if (cmd == "setTrackChorusSend") {
    int track = doc["track"];
    int level = doc["value"];
    if (track >= 0 && track < 16) {
      spiMaster.setTrackChorusSend(track, (uint8_t)constrain(level, 0, 100));
      StaticJsonDocument<128> resp;
      resp["type"] = "trackFxSet";
      resp["track"] = track;
      resp["fx"] = "chorusSend";
      resp["value"] = constrain(level, 0, 100);
      String out; serializeJson(resp, out);
      if (ws) ws->textAll(out);
    }
  }
  else if (cmd == "setTrackPan") {
    int track = doc["track"];
    int pan = doc["value"];
    if (track >= 0 && track < 16) {
      spiMaster.setTrackPan(track, (int8_t)constrain(pan, -100, 100));
      StaticJsonDocument<128> resp;
      resp["type"] = "trackFxSet";
      resp["track"] = track;
      resp["fx"] = "pan";
      resp["value"] = constrain(pan, -100, 100);
      String out; serializeJson(resp, out);
      if (ws) ws->textAll(out);
    }
  }
  else if (cmd == "setTrackDspMute") {
    int track = doc["track"];
    bool mute = doc["value"];
    if (track >= 0 && track < 16) {
      spiMaster.setTrackMute(track, mute);
      spiMaster.dsqSetMute((uint8_t)track, mute);
      StaticJsonDocument<128> resp;
      resp["type"] = "trackFxSet";
      resp["track"] = track;
      resp["fx"] = "dspMute";
      resp["value"] = mute;
      String out; serializeJson(resp, out);
      if (ws) ws->textAll(out);
    }
  }
  else if (cmd == "setTrackSolo") {
    int track = doc["track"];
    bool solo = doc["value"];
    if (track >= 0 && track < 16) {
      spiMaster.setTrackSolo(track, solo);
      StaticJsonDocument<128> resp;
      resp["type"] = "trackFxSet";
      resp["track"] = track;
      resp["fx"] = "solo";
      resp["value"] = solo;
      String out; serializeJson(resp, out);
      if (ws) ws->textAll(out);
    }
  }
  else if (cmd == "setTrackPhaser") {
    int track = doc["track"];
    if (track >= 0 && track < 16) {
      bool active = doc.containsKey("active") ? doc["active"].as<bool>() : true;
      float rate = doc.containsKey("rate") ? doc["rate"].as<float>() : 1.0f;
      float depth = doc.containsKey("depth") ? doc["depth"].as<float>() : 50.0f;
      float feedback = doc.containsKey("feedback") ? doc["feedback"].as<float>() : 50.0f;
      spiMaster.setTrackPhaser(track, active, rate, depth, feedback);
      StaticJsonDocument<192> resp;
      resp["type"] = "trackFxSet";
      resp["track"] = track;
      resp["fx"] = "phaser";
      resp["active"] = active;
      resp["rate"] = rate;
      resp["depth"] = depth;
      resp["feedback"] = feedback;
      String out; serializeJson(resp, out);
      if (ws) ws->textAll(out);
    }
  }
  else if (cmd == "setTrackTremolo") {
    int track = doc["track"];
    if (track >= 0 && track < 16) {
      bool active = doc.containsKey("active") ? doc["active"].as<bool>() : true;
      float rate = doc.containsKey("rate") ? doc["rate"].as<float>() : 4.0f;
      float depth = doc.containsKey("depth") ? doc["depth"].as<float>() : 50.0f;
      uint8_t wave = doc.containsKey("wave") ? doc["wave"].as<uint8_t>() : 0;
      uint8_t target = doc.containsKey("target") ? doc["target"].as<uint8_t>() : 0;
      spiMaster.setTrackTremolo(track, active, rate, depth, wave, target);
      StaticJsonDocument<192> resp;
      resp["type"] = "trackFxSet";
      resp["track"] = track;
      resp["fx"] = "tremolo";
      resp["active"] = active;
      resp["rate"] = rate;
      resp["depth"] = depth;
      resp["wave"] = wave;
      resp["target"] = target;
      String out; serializeJson(resp, out);
      if (ws) ws->textAll(out);
    }
  }
  else if (cmd == "setTrackPitch") {
    int track = doc["track"];
    int cents = doc["value"];
    if (track >= 0 && track < 16) {
      spiMaster.setTrackPitch(track, (int16_t)constrain(cents, -1200, 1200));
      StaticJsonDocument<128> resp;
      resp["type"] = "trackFxSet";
      resp["track"] = track;
      resp["fx"] = "pitchCents";
      resp["value"] = constrain(cents, -1200, 1200);
      String out; serializeJson(resp, out);
      if (ws) ws->textAll(out);
    }
  }
  else if (cmd == "setTrackGate") {
    int track = doc["track"];
    if (track >= 0 && track < 16) {
      bool active = doc.containsKey("active") ? doc["active"].as<bool>() : true;
      float threshold = doc.containsKey("threshold") ? doc["threshold"].as<float>() : -40.0f;
      float attack = doc.containsKey("attack") ? doc["attack"].as<float>() : 1.0f;
      float release = doc.containsKey("release") ? doc["release"].as<float>() : 50.0f;
      spiMaster.setTrackGate(track, active, threshold, attack, release);
      StaticJsonDocument<192> resp;
      resp["type"] = "trackFxSet";
      resp["track"] = track;
      resp["fx"] = "gate";
      resp["active"] = active;
      resp["threshold"] = threshold;
      resp["attack"] = attack;
      resp["release"] = release;
      String out; serializeJson(resp, out);
      if (ws) ws->textAll(out);
    }
  }
  else if (cmd == "setTrackEq") {
    int track = doc["track"];
    if (track >= 0 && track < 16) {
      int low = doc.containsKey("low") ? doc["low"].as<int>() : 0;
      int mid = doc.containsKey("mid") ? doc["mid"].as<int>() : 0;
      int high = doc.containsKey("high") ? doc["high"].as<int>() : 0;
      spiMaster.setTrackEq(track, (int8_t)constrain(low, -12, 12), (int8_t)constrain(mid, -12, 12), (int8_t)constrain(high, -12, 12));
      StaticJsonDocument<192> resp;
      resp["type"] = "trackFxSet";
      resp["track"] = track;
      resp["fx"] = "eq";
      resp["low"] = constrain(low, -12, 12);
      resp["mid"] = constrain(mid, -12, 12);
      resp["high"] = constrain(high, -12, 12);
      String out; serializeJson(resp, out);
      if (ws) ws->textAll(out);
    }
  }
  else if (cmd == "setTrackEqLow") {
    int track = doc["track"];
    int value = doc["value"];
    if (track >= 0 && track < 16) {
      spiMaster.setTrackEqLow(track, (int8_t)constrain(value, -12, 12));
    }
  }
  else if (cmd == "setTrackEqMid") {
    int track = doc["track"];
    int value = doc["value"];
    if (track >= 0 && track < 16) {
      spiMaster.setTrackEqMid(track, (int8_t)constrain(value, -12, 12));
    }
  }
  else if (cmd == "setTrackEqHigh") {
    int track = doc["track"];
    int value = doc["value"];
    if (track >= 0 && track < 16) {
      spiMaster.setTrackEqHigh(track, (int8_t)constrain(value, -12, 12));
    }
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
    bool value = false;
    if (doc.containsKey("value")) {
      value = doc["value"].as<bool>();
    } else if (doc.containsKey("active")) {
      value = doc["active"].as<bool>();
    }
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
    int volume = constrain((int)doc["value"], 0, 150);
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
    int volume = constrain((int)doc["value"], 0, 150);
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
      return;
    }
    
    bool silent = doc.containsKey("silent") && doc["silent"].as<bool>();
    
    // Support writing to a specific pattern
    bool isBulkImport = doc.containsKey("pattern");
    if (isBulkImport) {
      int pattern = doc["pattern"].as<int>();
      if (pattern >= 0 && pattern < MAX_PATTERNS) {
        sequencer.setStepVelocity(pattern, track, step, velocity);
        spiMaster.dsqSetStep((uint8_t)pattern, (uint8_t)track, (uint8_t)step,
            sequencer.getStep(pattern, track, step) ? 1 : 0, (uint8_t)velocity,
            sequencer.getStepNoteLen(pattern, track, step),
            sequencer.getStepProbability(pattern, track, step));
        yield(); // Prevent watchdog reset during bulk import
      }
      return;
    } else {
      int curPat = sequencer.getCurrentPattern();
      sequencer.setStepVelocity(track, step, velocity);
      spiMaster.dsqSetStep((uint8_t)curPat, (uint8_t)track, (uint8_t)step,
          sequencer.getStep(curPat, track, step) ? 1 : 0, (uint8_t)velocity,
          sequencer.getStepNoteLen(curPat, track, step),
          sequencer.getStepProbability(curPat, track, step));
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
    if (track < 0 || track >= MAX_TRACKS || step < 0 || step >= STEPS_PER_PATTERN) return;

    if (doc.containsKey("pattern")) {
      int pattern = doc["pattern"].as<int>();
      if (pattern >= 0 && pattern < MAX_PATTERNS) {
        sequencer.setStepVolumeLock(pattern, track, step, enabled, volume);
        dsqSyncParamLock(pattern, track, step);
      }
    } else {
      int curPat = sequencer.getCurrentPattern();
      sequencer.setStepVolumeLock(track, step, enabled, volume);
      dsqSyncParamLock(curPat, track, step);
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
    if (track < 0 || track >= MAX_TRACKS || step < 0 || step >= STEPS_PER_PATTERN) return;

    if (doc.containsKey("pattern")) {
      int pattern = doc["pattern"].as<int>();
      if (pattern >= 0 && pattern < MAX_PATTERNS) {
        sequencer.setStepProbability(pattern, track, step, probability);
        spiMaster.dsqSetStep((uint8_t)pattern, (uint8_t)track, (uint8_t)step,
            sequencer.getStep(pattern, track, step) ? 1 : 0,
            sequencer.getStepVelocity(pattern, track, step),
            sequencer.getStepNoteLen(pattern, track, step),
            (uint8_t)constrain(probability, 0, 100));
      }
    } else {
      int curPat = sequencer.getCurrentPattern();
      sequencer.setStepProbability(track, step, probability);
      spiMaster.dsqSetStep((uint8_t)curPat, (uint8_t)track, (uint8_t)step,
          sequencer.getStep(curPat, track, step) ? 1 : 0,
          sequencer.getStepVelocity(curPat, track, step),
          sequencer.getStepNoteLen(curPat, track, step),
          (uint8_t)constrain(probability, 0, 100));
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
  else if (cmd == "setStepCutoffLock") {
    int track = doc["track"];
    int step = doc["step"];
    bool enabled = doc.containsKey("enabled") ? doc["enabled"].as<bool>() : true;
    int cutoff = doc.containsKey("cutoff") ? doc["cutoff"].as<int>() : 1000;
    if (track < 0 || track >= MAX_TRACKS || step < 0 || step >= STEPS_PER_PATTERN) return;

    if (doc.containsKey("pattern")) {
      int pattern = doc["pattern"].as<int>();
      if (pattern >= 0 && pattern < MAX_PATTERNS) {
        sequencer.setStepCutoffLock(pattern, track, step, enabled, (uint16_t)constrain(cutoff, 20, 20000));
        dsqSyncParamLock(pattern, track, step);
      }
    } else {
      int curPat = sequencer.getCurrentPattern();
      sequencer.setStepCutoffLock(track, step, enabled, (uint16_t)constrain(cutoff, 20, 20000));
      dsqSyncParamLock(curPat, track, step);
    }

    StaticJsonDocument<160> responseDoc;
    responseDoc["type"] = "stepCutoffLockSet";
    responseDoc["track"] = track;
    responseDoc["step"] = step;
    responseDoc["enabled"] = enabled;
    responseDoc["cutoff"] = constrain(cutoff, 20, 20000);
    String output;
    serializeJson(responseDoc, output);
    if (ws) ws->textAll(output);
  }
  else if (cmd == "setStepReverbSendLock") {
    int track = doc["track"];
    int step = doc["step"];
    bool enabled = doc.containsKey("enabled") ? doc["enabled"].as<bool>() : true;
    int level = doc.containsKey("value") ? doc["value"].as<int>() : 0;
    if (track < 0 || track >= MAX_TRACKS || step < 0 || step >= STEPS_PER_PATTERN) return;

    if (doc.containsKey("pattern")) {
      int pattern = doc["pattern"].as<int>();
      if (pattern >= 0 && pattern < MAX_PATTERNS) {
        sequencer.setStepReverbSendLock(pattern, track, step, enabled, (uint8_t)constrain(level, 0, 100));
        dsqSyncParamLock(pattern, track, step);
      }
    } else {
      int curPat = sequencer.getCurrentPattern();
      sequencer.setStepReverbSendLock(track, step, enabled, (uint8_t)constrain(level, 0, 100));
      dsqSyncParamLock(curPat, track, step);
    }

    StaticJsonDocument<160> responseDoc;
    responseDoc["type"] = "stepReverbLockSet";
    responseDoc["track"] = track;
    responseDoc["step"] = step;
    responseDoc["enabled"] = enabled;
    responseDoc["value"] = constrain(level, 0, 100);
    String output;
    serializeJson(responseDoc, output);
    if (ws) ws->textAll(output);
  }
  else if (cmd == "setStepRatchet") {
    int track = doc["track"];
    int step = doc["step"];
    int ratchet = doc.containsKey("ratchet") ? doc["ratchet"].as<int>() : 1;
    if (track < 0 || track >= MAX_TRACKS || step < 0 || step >= STEPS_PER_PATTERN) return;

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
    if (track < 0 || track >= MAX_TRACKS || step < 0 || step >= STEPS_PER_PATTERN) return;

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
  else if (cmd == "getStepCutoffLock") {
    int track = doc["track"];
    int step = doc["step"];
    if (track < 0 || track >= MAX_TRACKS || step < 0 || step >= STEPS_PER_PATTERN) return;

    bool enabled = sequencer.hasStepCutoffLock(track, step);
    int cutoff = enabled ? (int)sequencer.getStepCutoffLock(track, step) : 0;

    StaticJsonDocument<160> responseDoc;
    responseDoc["type"] = "stepCutoffLock";
    responseDoc["track"] = track;
    responseDoc["step"] = step;
    responseDoc["enabled"] = enabled;
    responseDoc["cutoff"] = cutoff;
    String output;
    serializeJson(responseDoc, output);
    if (ws) ws->textAll(output);
  }
  else if (cmd == "getStepReverbSendLock") {
    int track = doc["track"];
    int step = doc["step"];
    if (track < 0 || track >= MAX_TRACKS || step < 0 || step >= STEPS_PER_PATTERN) return;

    bool enabled = sequencer.hasStepReverbSendLock(track, step);
    int level = enabled ? (int)sequencer.getStepReverbSendLock(track, step) : 0;

    StaticJsonDocument<160> responseDoc;
    responseDoc["type"] = "stepReverbLock";
    responseDoc["track"] = track;
    responseDoc["step"] = step;
    responseDoc["enabled"] = enabled;
    responseDoc["value"] = level;
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
      
    }
  }
  // ============= NEW: Track Volume Commands =============
  else if (cmd == "setTrackVolume") {
    int track = doc["track"];
    int volume = constrain((int)doc["volume"], 0, 150);
    if (track < 0 || track >= 16) {
      return;
    }
    
    sequencer.setTrackVolume(track, volume);
    spiMaster.setTrackVolume(track, (uint8_t)constrain(volume, 0, 150));
    
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
  else if (cmd == "setTrackSynthEngine") {
    int track = doc["track"];
    int engine = doc["engine"];
    if (track < 0 || track >= 16 || engine < -1 || engine > 6) {
      return;
    }

    // Si el pad estaba en 303 y cambia a otro motor, detener la nota
    if (gTrackSynthEngine[track] == 3 && engine != 3) {
      spiMaster.synth303NoteOff();
    }

    if (engine >= 0) {
      clearTrackLoopStateForSynth(track, ws);
    }

    setTrackSynthEngine(track, (int8_t)engine);
    spiMaster.dsqSetTrackEngine((uint8_t)track, (int8_t)engine);

    StaticJsonDocument<128> responseDoc;
    responseDoc["type"] = "trackSynthEngineSet";
    responseDoc["track"] = track;
    responseDoc["engine"] = engine;
    String output;
    serializeJson(responseDoc, output);
    if (ws) ws->textAll(output);
  }
  else if (cmd == "applyKitToAllPads") {
    int engine = doc["engine"];
    if (engine < -1 || engine > 6) {
      return;
    }

    // Si algún track estaba en 303 y cambia a otro motor, detener la nota
    if (engine != 3) {
      for (int i = 0; i < 16; i++) {
        if (gTrackSynthEngine[i] == 3) {
          spiMaster.synth303NoteOff();
          break;
        }
      }
    }

    if (engine >= 0) {
      for (int i = 0; i < 16; i++) {
        clearTrackLoopStateForSynth(i, ws);
      }
    }

    setAllTrackSynthEngines((int8_t)engine);
    for (int i = 0; i < 16; i++) spiMaster.dsqSetTrackEngine((uint8_t)i, (int8_t)engine);

    StaticJsonDocument<256> responseDoc;
    responseDoc["type"] = "trackSynthEngines";
    JsonArray engines = responseDoc.createNestedArray("engines");
    for (int track = 0; track < 16; track++) {
      engines.add((int)gTrackSynthEngine[track]);
    }

    String output;
    serializeJson(responseDoc, output);
    if (ws) ws->textAll(output);
  }
  else if (cmd == "setMidiScan") {
    bool enabled = doc["enabled"];
    if (midiController) {
      midiController->setScanEnabled(enabled);
      // Broadcast state to all clients
      StaticJsonDocument<128> responseDoc;
      responseDoc["type"] = "midiScan";
      responseDoc["enabled"] = enabled;
      String output;
      serializeJson(responseDoc, output);
      if (ws) ws->textAll(output);
    }
  }
  // ══════════════════════════════════════════════════════
  // DAISY SD CARD COMMANDS
  // ══════════════════════════════════════════════════════
  else if (cmd == "sdListKits") {
    SdKitListResponse kitList;
    if (spiMaster.sdGetKitList(kitList)) {
      DynamicJsonDocument resp(2048);
      resp["type"] = "sdKitList";
      JsonArray kits = resp.createNestedArray("kits");
      for (int i = 0; i < kitList.count && i < 16; i++) {
        kits.add(String(kitList.kits[i]));
      }
      String output;
      serializeJson(resp, output);
      if (ws) ws->textAll(output);
    } else {
      StaticJsonDocument<128> resp;
      resp["type"] = "sdKitList";
      resp["error"] = "SPI timeout";
      String output;
      serializeJson(resp, output);
      if (ws) ws->textAll(output);
    }
  }
  else if (cmd == "sdLoadKit") {
    const char* kit = doc["kit"];
    if (kit) {
      uint8_t startPad = doc.containsKey("startPad") ? doc["startPad"].as<uint8_t>() : 0;
      uint8_t maxPads  = doc.containsKey("maxPads")  ? doc["maxPads"].as<uint8_t>()  : 16;
      bool ok = spiMaster.sdLoadKit(kit, startPad, maxPads);
      if (ok) {
        clearDaisyPadFiles();
      }
      StaticJsonDocument<192> resp;
      resp["type"] = "sdLoadKitAck";
      resp["kit"] = kit;
      resp["ok"] = ok;
      String output;
      serializeJson(resp, output);
      if (ws) ws->textAll(output);
    }
  }
  else if (cmd == "sdUnloadKit") {
    spiMaster.sdUnloadKit();
    clearDaisyPadFiles();
    StaticJsonDocument<64> resp;
    resp["type"] = "sdUnloadKitAck";
    resp["ok"] = true;
    String output;
    serializeJson(resp, output);
    if (ws) ws->textAll(output);
  }
  else if (cmd == "sdLoadSample") {
    int pad = doc["pad"];
    const char* folder = doc["folder"];
    const char* file   = doc["file"];
    if (folder && file && pad >= 0 && pad < 24) {
      SdFileInfoResponse info = {};
      spiMaster.sdGetFileInfo(folder, file, info);
      bool ok = spiMaster.sdLoadSample(pad, folder, file);
      if (ok) {
        setDaisyPadFile(pad, String(file));
      }
      StaticJsonDocument<192> resp;
      resp["type"] = "sdLoadSampleAck";
      resp["pad"]  = pad;
      resp["folder"] = folder;
      resp["file"] = file;
      resp["size"] = info.sizeBytes;
      resp["ok"]   = ok;
      String output;
      serializeJson(resp, output);
      if (ws) ws->textAll(output);
    }
  }
  else if (cmd == "sdListFolders") {
    SdFolderListResponse folders;
    if (spiMaster.sdListFolders(folders)) {
      DynamicJsonDocument resp(2048);
      resp["type"] = "sdFolderList";
      JsonArray arr = resp.createNestedArray("folders");
      for (int i = 0; i < folders.count && i < 16; i++) {
        arr.add(String(folders.names[i]));
      }
      String output;
      serializeJson(resp, output);
      if (ws) ws->textAll(output);
    }
  }
  else if (cmd == "sdListFiles") {
    const char* folder = doc["folder"];
    if (folder) {
      SdFileListResponse files;
      if (spiMaster.sdListFiles(folder, files)) {
        DynamicJsonDocument resp(4096);
        resp["type"] = "sdFileList";
        resp["folder"] = folder;
        JsonArray arr = resp.createNestedArray("files");
        for (int i = 0; i < files.count && i < 24; i++) {
          JsonObject f = arr.createNestedObject();
          f["name"] = String(files.files[i].name);
          f["size"] = files.files[i].sizeBytes;
        }
        String output;
        serializeJson(resp, output);
        if (ws) ws->textAll(output);
      }
    }
  }
  else if (cmd == "sdGetStatus") {
    SdStatusResponse sdStat;
    if (spiMaster.getCachedSdStatus(sdStat)) {
      clearDaisyPadFilesNotInMask(sdStat.samplesLoaded);
      int loadedCount = 0;
      for (int i = 0; i < MAX_PADS; i++) {
        if (sdStat.samplesLoaded & (1UL << i)) loadedCount++;
      }
      StaticJsonDocument<256> resp;
      resp["type"]      = "sdStatus";
      resp["present"]   = (bool)sdStat.present;
      resp["totalMB"]   = sdStat.totalMB;
      resp["freeMB"]    = sdStat.freeMB;
      resp["loaded"]    = loadedCount;
      resp["loadedMask"] = sdStat.samplesLoaded;
      resp["kit"]       = String(sdStat.currentKit);
      String output;
      serializeJson(resp, output);
      if (ws) ws->textAll(output);
    }
  }
  else if (cmd == "sdAbort") {
    spiMaster.sdAbortLoad();
    StaticJsonDocument<64> resp;
    resp["type"] = "sdAbortAck";
    resp["ok"]   = true;
    String output;
    serializeJson(resp, output);
    if (ws) ws->textAll(output);
  }
  // ══════════════════════════════════════════════════════
  // SYNTH ENGINES — TR-808/909/505 + TB-303 + WTOSC + SH-101 + FM2Op
  // ══════════════════════════════════════════════════════

  // {"cmd":"synthTrigger","engine":0,"instrument":0,"velocity":100}
  else if (cmd == "synthTrigger") {
    uint8_t engine     = doc["engine"]     | 0;
    uint8_t instrument = doc["instrument"] | 0;
    uint8_t velocity   = doc["velocity"]   | 100;
    float scaled = (velocity / 127.0f) * (spiMaster.getLiveVolume() / 100.0f);
    uint8_t outVelocity = (uint8_t)constrain((int)roundf(scaled * 127.0f), 1, 127);
    spiMaster.synthTrigger(engine, instrument, outVelocity);
  }

  // {"cmd":"synthParam","engine":0,"instrument":0,"paramId":0,"value":0.5}
  else if (cmd == "synthParam") {
    uint8_t engine     = doc["engine"]     | 0;
    uint8_t instrument = doc["instrument"] | 0;
    uint8_t paramId    = doc["paramId"]    | 0;
    float   value      = doc["value"]      | 0.5f;
    spiMaster.synthParam(engine, instrument, paramId, value);
  }

  // {"cmd":"setWtNote","track":0,"note":60}
  else if (cmd == "setWtNote") {
    int track = doc["track"] | 0;
    int note  = doc["note"]  | 60;
    if (track >= 0 && track < 16)
      spiMaster.synthParam(4, (uint8_t)track, 8, (float)note);
  }

  // {"cmd":"synth303NoteOn","note":48,"accent":false,"slide":false}
  else if (cmd == "synth303NoteOn") {
    uint8_t note   = doc["note"]   | 36;
    bool    accent = doc["accent"] | false;
    bool    slide  = doc["slide"]  | false;
    spiMaster.synth303NoteOn(note, accent, slide);
  }

  // {"cmd":"synth303NoteOff"}
  else if (cmd == "synth303NoteOff") {
    spiMaster.synth303NoteOff();
  }

  // {"cmd":"synth303Param","paramId":0,"value":800.0}
  else if (cmd == "synth303Param") {
    uint8_t paramId = doc["paramId"] | 0;
    float   value   = doc["value"]   | 0.5f;
    spiMaster.synth303Param(paramId, value);
  }

  // {"cmd":"synthActive","mask":123}   (bit0=808, bit1=909, bit2=505, bit3=303, bit4=WT, bit5=SH101, bit6=FM2Op)
  // {"cmd":"synthActive","mask":511}   (9 engines, 16-bit)
  else if (cmd == "synthActive") {
    uint16_t mask = doc["mask"] | 0x01FF;
    if (mask > 0xFF) {
      spiMaster.synthSetActive16(mask);
    } else {
      spiMaster.synthSetActive((uint8_t)mask);
    }
    StaticJsonDocument<64> resp;
    resp["type"] = "synthActiveAck";
    resp["mask"] = mask;
    String output;
    serializeJson(resp, output);
    if (ws) ws->textAll(output);
  }

  // {"cmd":"synthPreset","engine":5,"preset":2}
  else if (cmd == "synthPreset") {
    uint8_t engine = doc["engine"] | 0;
    uint8_t preset = doc["preset"] | 0;
    spiMaster.synthPreset(engine, preset);

    StaticJsonDocument<96> resp;
    resp["type"] = "synthPresetAck";
    resp["engine"] = engine;
    resp["preset"] = preset;
    String output;
    serializeJson(resp, output);
    if (ws) ws->textAll(output);
  }

  // ═══════════════════════════════════════════════════
  // NEW MASTER FX
  // ═══════════════════════════════════════════════════

  // {"cmd":"setMasterFxRoute","fxId":10,"connected":true}
  else if (cmd == "setMasterFxRoute") {
    uint8_t fxId = doc["fxId"] | 0;
    bool connected = doc["connected"] | false;
    spiMaster.setMasterFxRoute(fxId, connected);
  }

  // {"cmd":"setAutoWahActive","active":true}
  else if (cmd == "setAutoWahActive") {
    bool active = doc["active"] | false;
    spiMaster.setAutoWahActive(active);
  }
  // {"cmd":"setAutoWahLevel","level":80}
  else if (cmd == "setAutoWahLevel") {
    uint8_t level = doc["level"] | 80;
    spiMaster.setAutoWahLevel(level);
  }
  // {"cmd":"setAutoWahMix","mix":50}
  else if (cmd == "setAutoWahMix") {
    uint8_t mix = doc["mix"] | 50;
    spiMaster.setAutoWahMix(mix);
  }

  // {"cmd":"setStereoWidth","width":100}
  else if (cmd == "setStereoWidth") {
    uint8_t width = doc["width"] | 100;
    spiMaster.setStereoWidth(width);
  }

  // {"cmd":"setTapeStop","mode":1}
  else if (cmd == "setTapeStop") {
    uint8_t mode = doc["mode"] | 0;
    spiMaster.setTapeStop(mode);
  }

  // {"cmd":"setBeatRepeat","division":8}
  else if (cmd == "setBeatRepeat") {
    uint8_t division = doc["division"] | 0;
    spiMaster.setBeatRepeat(division);
  }

  // {"cmd":"setDelayStereo","mode":1}
  else if (cmd == "setDelayStereo") {
    uint8_t mode = doc["mode"] | 0;
    spiMaster.setDelayStereo(mode);
  }

  // {"cmd":"setChorusStereo","mode":1}
  else if (cmd == "setChorusStereo") {
    uint8_t mode = doc["mode"] | 0;
    spiMaster.setChorusStereo(mode);
  }

  // {"cmd":"setEarlyRefActive","active":true}
  else if (cmd == "setEarlyRefActive") {
    bool active = doc["active"] | false;
    spiMaster.setEarlyRefActive(active);
  }
  // {"cmd":"setEarlyRefMix","mix":30}
  else if (cmd == "setEarlyRefMix") {
    uint8_t mix = doc["mix"] | 30;
    spiMaster.setEarlyRefMix(mix);
  }

  // ═══════════════════════════════════════════════════
  // CHOKE GROUPS
  // ═══════════════════════════════════════════════════

  // {"cmd":"setChokeGroup","pad":6,"group":1}
  else if (cmd == "setChokeGroup") {
    uint8_t pad = doc["pad"] | 0;
    uint8_t group = doc["group"] | 0;
    spiMaster.setChokeGroup(pad, group);
  }

  // ═══════════════════════════════════════════════════
  // SONG CHAIN MODE
  // ═══════════════════════════════════════════════════

  // {"cmd":"songChainUpload","chain":[{"pattern":0,"repeats":4},{"pattern":1,"repeats":2}]}
  else if (cmd == "songChainUpload") {
    JsonArrayConst arr = doc["chain"].as<JsonArrayConst>();
    if (!arr.isNull() && arr.size() > 0) {
      uint8_t count = min((int)arr.size(), (int)Sequencer::SONG_CHAIN_MAX);
      Sequencer::SongChainEntry entries[Sequencer::SONG_CHAIN_MAX];
      for (uint8_t i = 0; i < count; i++) {
        entries[i].pattern = arr[i]["pattern"] | 0;
        entries[i].repeats = arr[i]["repeats"] | 1;
      }
      sequencer.songChainUpload(entries, count);
      // Also upload to Daisy via SPI
      SongEntry spiEntries[SONG_MAX_ENTRIES];
      for (uint8_t i = 0; i < count; i++) {
        spiEntries[i].pattern = entries[i].pattern;
        spiEntries[i].repeats = entries[i].repeats;
      }
      spiMaster.songUpload(spiEntries, count);
    }
  }

  // {"cmd":"songChainControl","action":1}  0=stop, 1=play, 2=reset
  else if (cmd == "songChainControl") {
    uint8_t action = doc["action"] | 0;
    if (action == 1) {
      sequencer.songChainPlay();
      spiMaster.songControl(1);
    } else if (action == 0) {
      sequencer.songChainStop();
      spiMaster.songControl(0);
    } else if (action == 2) {
      sequencer.songChainReset();
      spiMaster.songControl(2);
    }
    broadcastSequencerState();
  }

  // {"cmd":"songGetPos"}
  else if (cmd == "songGetPos") {
    StaticJsonDocument<128> resp;
    resp["type"] = "songPos";
    resp["idx"] = sequencer.getSongChainIdx();
    resp["pattern"] = sequencer.getCurrentPattern();
    resp["repeat"] = sequencer.getSongChainRepeatCnt();
    resp["active"] = sequencer.isSongChainActive();
    String output;
    serializeJson(resp, output);
    if (ws) ws->textAll(output);
  }

  // ═══════════════════════════════════════════════════
  // PER-TRACK LFO CONFIG (Daisy-side)
  // ═══════════════════════════════════════════════════

  // {"cmd":"setTrackLfo","track":0,"wave":0,"target":3,"rate":100,"depth":500}
  else if (cmd == "setTrackLfo") {
    uint8_t track = doc["track"] | 0;
    uint8_t wave = doc["wave"] | 0;
    uint8_t target = doc["target"] | 0;
    uint16_t rate = doc["rate"] | 100;     // centésimas de Hz
    uint16_t depth = doc["depth"] | 500;   // milésimas
    spiMaster.setTrackLfoConfig(track, wave, target, rate, depth);
  }
}

void WebInterface::updateUdpClient(IPAddress ip, uint16_t port) {
  // Use fixed-size key buffer to avoid String heap allocation/fragmentation
  char key[16];
  snprintf(key, sizeof(key), "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
  String sKey(key);

  auto it = udpClients.find(sKey);
  if (it != udpClients.end()) {
    it->second.lastSeen = millis();
    it->second.packetCount++;
  } else {
    if (udpClients.size() >= 10) {
      cleanupStaleUdpClients();
      if (udpClients.size() >= 10) return;
    }
    UdpClient client;
    client.ip = ip;
    client.port = port;
    client.lastSeen = millis();
    client.packetCount = 1;
    udpClients[sKey] = client;
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

// Manejar paquetes UDP entrantes (with crash protection)
void WebInterface::handleUdp() {
  int packetSize = udp.parsePacket();
  if (packetSize <= 0) return;

  // ── Heap guard: skip processing if memory is critical ──
  uint32_t freeHeap = ESP.getFreeHeap();
  if (freeHeap < 20000) {
    char drain[64];
    while (udp.available()) udp.read(drain, sizeof(drain));
    return;
  }

  // ── Rate limit: max 50 packets/second ──
  static unsigned long lastUdpProcess = 0;
  static uint8_t udpBurstCount = 0;
  unsigned long now = millis();
  if (now - lastUdpProcess < 20) {
    udpBurstCount++;
    if (udpBurstCount > 5) {
      char drain[64];
      while (udp.available()) udp.read(drain, sizeof(drain));
      return;
    }
  } else {
    udpBurstCount = 0;
  }
  lastUdpProcess = now;

  // ── Reject oversized packets ──
  if (packetSize > 500) {
    char drain[64];
    while (udp.available()) udp.read(drain, sizeof(drain));
    udp.beginPacket(udp.remoteIP(), udp.remotePort());
    udp.print("{\"s\":\"err\",\"m\":\"too_large\"}");
    udp.endPacket();
    return;
  }

  char incomingPacket[512];
  int len = udp.read(incomingPacket, 511);
  if (len <= 0) return;
  incomingPacket[len] = 0;

  updateUdpClient(udp.remoteIP(), udp.remotePort());

  StaticJsonDocument<512> doc;
  DeserializationError error = deserializeJson(doc, incomingPacket);

  if (!error) {
    processCommand(doc);
    udp.beginPacket(udp.remoteIP(), udp.remotePort());
    udp.print("{\"s\":\"ok\"}");
    udp.endPacket();
  } else {
    udp.beginPacket(udp.remoteIP(), udp.remotePort());
    udp.print("{\"s\":\"err\",\"m\":\"parse\"}");
    udp.endPacket();
  }
  yield();
}

// ========================================
// MIDI Functions
// ========================================

void WebInterface::setMIDIController(MIDIController* controller) {
  midiController = controller;
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
    
    
    // Buscar el parámetro 'pad' en la query string (GET params)
    if (!request->hasParam("pad", false)) {
      for (int i = 0; i < request->params(); i++) {
        AsyncWebParameter* p = request->getParam(i);
      }
      uploadError = true;
      uploadErrorMsg = "Missing pad parameter";
      broadcastUploadComplete(-1, false, uploadErrorMsg);
      return;
    }
    
    uploadPad = request->getParam("pad", false)->value().toInt();
    
    if (uploadPad < 0 || uploadPad >= MAX_SAMPLES) {
      uploadError = true;
      uploadErrorMsg = "Invalid pad number";
      broadcastUploadComplete(uploadPad, false, uploadErrorMsg);
      return;
    }
    
    // Validar extensión
    if (!filename.endsWith(".wav") && !filename.endsWith(".WAV")) {
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
    
    
    // Abrir archivo para escritura
    uploadFile = LittleFS.open(filePath, "w");
    if (!uploadFile) {
      uploadError = true;
      uploadErrorMsg = "Failed to create file on flash";
      broadcastUploadComplete(uploadPad, false, uploadErrorMsg);
      return;
    }
    
    uploadSize = request->contentLength();
    
    // Validar tamaño (max 2MB para seguridad)
    if (uploadSize > 2 * 1024 * 1024) {
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
      broadcastUploadProgress(uploadPad, percent);
      lastPercent = percent;
    }
  }
  
  // Upload completado
  if (final) {
    if (uploadFile) {
      uploadFile.close();
      
      
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
      
      
      // Cargar sample en el pad
      bool loaded = sampleManager.loadSample(filePath.c_str(), uploadPad);
      
      if (loaded) {
        
        // Enviar respuesta HTTP exitosa
        request->send(200, "application/json", "{\"success\":true,\"message\":\"Sample uploaded successfully\"}");
        
        broadcastUploadComplete(uploadPad, true, "Sample uploaded and loaded successfully");
        
        // Broadcast state update
        broadcastSequencerState();
      } else {
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
    return false;
  }
  
  if (channels < 1 || channels > 2) {
    return false;
  }
  
  if (bitsPerSample != 16) {
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

void WebInterface::broadcastRaw(const char* json) {
  if (!initialized || !ws || ws->count() == 0) return;
  if (ESP.getFreeHeap() < 20000) return;   // drop if heap critical
  ws->textAll(json);
}

