/*
 * WebInterface.h
 * RED808 Web Interface Header
 */

#ifndef WEBINTERFACE_H
#define WEBINTERFACE_H

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <ESPAsyncWebServer.h>
#include <AsyncWebSocket.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <map>
#include <functional>
#include "MIDIController.h"

#define UDP_PORT 8888  // Puerto para recibir comandos UDP

// Estructura para trackear clientes UDP
struct UdpClient {
  IPAddress ip;
  uint16_t port;
  unsigned long lastSeen;
  uint32_t packetCount;
};

class WebInterface {
public:
  WebInterface();
  ~WebInterface();
  
  // STA mode (connect to home WiFi) with AP fallback
  bool begin(const char* apSsid, const char* apPassword,
             const char* staSSID = nullptr, const char* staPassword = nullptr,
             unsigned long staTimeoutMs = 12000);
  void update();
  void handleUdp();  // Nueva función para procesar paquetes UDP
  bool isSTAMode() const { return _staConnected; }
  
  void broadcastSequencerState();
  void sendSequencerStateToClient(AsyncWebSocketClient* client);
  void broadcastPadTrigger(int pad);
  void broadcastStep(int step);
  void broadcastSongPattern(int pattern, int songLength);
  
  // MIDI functions
  void setMIDIController(MIDIController* controller);
  void broadcastMIDIMessage(const MIDIMessage& msg);
  void broadcastMIDIDeviceStatus(bool connected, const MIDIDeviceInfo& info);
  
  // Sample upload functions
  void broadcastUploadProgress(int pad, int percent);
  void broadcastUploadComplete(int pad, bool success, const String& message);
  
  // Generic text broadcast (for event bridge)
  void broadcastRaw(const char* json);
  
  // Callback: llamado cuando se recibe POST /api/buttons con la config JSON
  void setBtnConfigCallback(std::function<void(const String&)> cb) { _btnConfigCb = cb; }
  
  String getIP();
  
private:
  AsyncWebServer* server;
  AsyncWebSocket* ws;
  WiFiUDP udp;  // Servidor UDP
  bool initialized;
  
  // Rate limiting para múltiples clientes
  unsigned long lastTriggerTime;
  unsigned long lastStepChangeTime;
  unsigned long lastBroadcastTime;
  
  // WiFi mode tracking
  bool _staConnected;  // true = connected to home WiFi, false = AP mode
  
  // MIDI Controller reference
  MIDIController* midiController;
  
  // Callback para config de botones físicos
  std::function<void(const String&)> _btnConfigCb;
  
  // Tracking de clientes UDP
  std::map<String, UdpClient> udpClients;
  void updateUdpClient(IPAddress ip, uint16_t port);
  void cleanupStaleUdpClients();
  
  void onWebSocketEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, 
                       AwsEventType type, void *arg, uint8_t *data, size_t len);
  struct WsReassemblySlot {
    uint32_t clientId;
    uint8_t* buffer;
    size_t size;
  };
  WsReassemblySlot* findWsReassemblySlot(uint32_t clientId, bool create);
  void releaseWsReassemblySlot(uint32_t clientId);
  void releaseWsReassemblySlot(WsReassemblySlot* slot);
  WsReassemblySlot wsReassemblySlots[4];
  void processCommand(const JsonDocument& doc);  // Función común para procesar comandos
  void sendUdpStateSync(IPAddress ip, uint16_t port);
  void broadcastUdpStateSync();
  bool shouldSendUdpStateSync(const char* cmd) const;
  /* v2.6 — Push pattern + selected index to all UDP slaves (P4/S3).
   * Fixes bug where slaves displayed stale pattern after web changed it. */
  void broadcastUdpPatternSync(int patternNum);

  /* v2.9 — Melody (P4 piano + S3 melody screen) authoritative state.
   * Master holds the currently-edited grid + per-pad bindings, broadcasts
   * `melody_sync` to every UDP slave so P4 piano and S3 melody screen stay
   * locked in lockstep just like the drum pattern. */
  uint8_t  melodyEngine     = 3;
  uint8_t  melodyOctave     = 4;
  bool     melodyRecActive  = false;
  uint8_t  melodyStep       = 0;
  uint8_t  melodyPad        = 0;
  bool     melodyGrid[16][12] = {{false}};
  // per-pad binding (set on melodyAssign)
  bool     melodyPadAssigned[16]      = {false};
  uint8_t  melodyPadEngine[16]        = {3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3};
  uint8_t  melodyPadOctave[16]        = {4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4};
  bool     melodyPadGrid[16][16][12]  = {};
  void     broadcastMelodySync();
  void     sendMelodySyncTo(IPAddress ip, uint16_t port);
  void     melodyClearGrid();
  
  // File upload handlers
  void handleUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final);
  bool validateWavFile(File& file, uint32_t& sampleRate, uint16_t& channels, uint16_t& bitsPerSample);
};

#endif
