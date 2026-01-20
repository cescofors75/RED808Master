/*
 * WebInterface.cpp
 * RED808 Web Interface con WebSockets
 */

#include "WebInterface.h"
#include "AudioEngine.h"
#include "Sequencer.h"
#include "KitManager.h"

extern AudioEngine audioEngine;
extern Sequencer sequencer;
extern KitManager kitManager;
extern void triggerPadWithLED(int track, uint8_t velocity);  // Función que enciende LED

WebInterface::WebInterface() {
  server = nullptr;
  ws = nullptr;
}

WebInterface::~WebInterface() {
  if (server) delete server;
  if (ws) delete ws;
}

bool WebInterface::begin(const char* ssid, const char* password) {
  // Configurar como Access Point
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssid, password);
  
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
  
  return true;
}

void WebInterface::onWebSocketEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, 
                                     AwsEventType type, void *arg, uint8_t *data, size_t len) {
  if (type == WS_EVT_CONNECT) {
    Serial.printf("WebSocket client #%u connected\n", client->id());
    broadcastSequencerState();
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
          String cmd = doc["cmd"];
          
          if (cmd == "trigger") {
            // Fallback para JSON
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
            // Broadcast nuevo estado
            delay(50);
            broadcastSequencerState();
          }
          else if (cmd == "loadKit") {
            int kit = doc["index"];
            kitManager.loadKit(kit);
            Serial.printf("[WebSocket] Kit changed to: %d\n", kit);
            
            // Enviar configuración del kit a la web
            StaticJsonDocument<512> responseDoc;
            responseDoc["type"] = "kitChanged";
            responseDoc["kit"] = kit;
            responseDoc["name"] = kitManager.getCurrentKitName();
            
            String output;
            serializeJson(responseDoc, output);
            ws->textAll(output);
          }
          else if (cmd == "getPattern") {
            int pattern = sequencer.getCurrentPattern();
            StaticJsonDocument<2048> responseDoc;
            responseDoc["type"] = "pattern";
            responseDoc["index"] = pattern;
            
            for (int track = 0; track < 8; track++) {
              JsonArray trackSteps = responseDoc.createNestedArray(String(track));
              for (int step = 0; step < 16; step++) {
                trackSteps.add(sequencer.getStep(track, step));
              }
            }
            
            String output;
            serializeJson(responseDoc, output);
            ws->textAll(output);
          }
          else if (cmd == "mute") {
            int track = doc["track"];
            bool muted = doc["value"];
            sequencer.muteTrack(track, muted);
          }
          else if (cmd == "toggleLoop") {
            int track = doc["track"];
            sequencer.toggleLoop(track);
            
            // Broadcast loop state
            StaticJsonDocument<128> responseDoc;
            responseDoc["type"] = "loopState";
            responseDoc["track"] = track;
            responseDoc["active"] = sequencer.isLooping(track);
            responseDoc["paused"] = sequencer.isLoopPaused(track);
            
            String output;
            serializeJson(responseDoc, output);
            ws->textAll(output);
          }
          else if (cmd == "pauseLoop") {
            int track = doc["track"];
            sequencer.pauseLoop(track);
            
            // Broadcast loop state
            StaticJsonDocument<128> responseDoc;
            responseDoc["type"] = "loopState";
            responseDoc["track"] = track;
            responseDoc["active"] = sequencer.isLooping(track);
            responseDoc["paused"] = sequencer.isLoopPaused(track);
            
            String output;
            serializeJson(responseDoc, output);
            ws->textAll(output);
          }
          // FX Controls
          else if (cmd == "setFilter") {
            int type = doc["type"];
            audioEngine.setFilterType((FilterType)type);
            Serial.printf("[FX] Filter type: %d\n", type);
          }
          else if (cmd == "setFilterCutoff") {
            float cutoff = doc["value"];
            audioEngine.setFilterCutoff(cutoff);
            Serial.printf("[FX] Cutoff: %.1f Hz\n", cutoff);
          }
          else if (cmd == "setFilterResonance") {
            float resonance = doc["value"];
            audioEngine.setFilterResonance(resonance);
            Serial.printf("[FX] Resonance: %.1f\n", resonance);
          }
          else if (cmd == "setBitCrush") {
            int bits = doc["value"];
            audioEngine.setBitDepth(bits);
            Serial.printf("[FX] Bit depth: %d\n", bits);
          }
          else if (cmd == "setDistortion") {
            float amount = doc["value"];
            audioEngine.setDistortion(amount);
            Serial.printf("[FX] Distortion: %.1f\n", amount);
          }
          else if (cmd == "setSampleRate") {
            int rate = doc["value"];
            audioEngine.setSampleRateReduction(rate);
            Serial.printf("[FX] Sample rate: %d Hz\n", rate);
          }
          else if (cmd == "setVolume") {
            int volume = doc["value"];
            audioEngine.setMasterVolume(volume);
            Serial.printf("[Volume] Master volume: %d%%\n", volume);
          }
        }
      }
    }
  }
}

void WebInterface::broadcastSequencerState() {
  StaticJsonDocument<2048> doc;
  doc["type"] = "state";
  doc["playing"] = sequencer.isPlaying();
  doc["tempo"] = sequencer.getTempo();
  doc["pattern"] = sequencer.getCurrentPattern();
  doc["step"] = sequencer.getCurrentStep();
  
  String output;
  serializeJson(doc, output);
  ws->textAll(output);
}

void WebInterface::broadcastPadTrigger(int pad) {
  StaticJsonDocument<128> doc;
  doc["type"] = "pad";
  doc["pad"] = pad;
  
  String output;
  serializeJson(doc, output);
  ws->textAll(output);
}

void WebInterface::broadcastStep(int step) {
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
  // Solo cleanup, el broadcast de steps se hace via callback
  ws->cleanupClients();
  
  // Broadcast audio visualization data every 50ms (~20fps)
  static uint32_t lastVisUpdate = 0;
  if (millis() - lastVisUpdate > 50) {
    broadcastVisualizationData();
    lastVisUpdate = millis();
  }
}

String WebInterface::getIP() {
  return WiFi.softAPIP().toString();
}

void WebInterface::broadcastVisualizationData() {
  // Capture audio data
  uint8_t spectrum[64];
  uint8_t waveform[128];
  audioEngine.captureAudioData(spectrum, waveform);
  
  // Build JSON message
  StaticJsonDocument<1024> doc;
  doc["type"] = "audioData";
  
  JsonArray spectrumArray = doc.createNestedArray("spectrum");
  for (int i = 0; i < 64; i++) {
    spectrumArray.add(spectrum[i]);
  }
  
  JsonArray waveformArray = doc.createNestedArray("waveform");
  for (int i = 0; i < 128; i++) {
    waveformArray.add(waveform[i]);
  }
  
  String output;
  serializeJson(doc, output);
  ws->textAll(output);
}

