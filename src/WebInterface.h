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
  
  bool begin(const char* ssid, const char* password);
  void update();
  void handleUdp();  // Nueva función para procesar paquetes UDP
  
  void broadcastSequencerState();
  void sendSequencerStateToClient(AsyncWebSocketClient* client);
  void broadcastPadTrigger(int pad);
  void broadcastStep(int step);
  void broadcastVisualizationData();
  
  // MIDI functions
  void setMIDIController(MIDIController* controller);
  void broadcastMIDIMessage(const MIDIMessage& msg);
  void broadcastMIDIDeviceStatus(bool connected, const MIDIDeviceInfo& info);
  
  // Sample upload functions
  void broadcastUploadProgress(int pad, int percent);
  void broadcastUploadComplete(int pad, bool success, const String& message);
  
  String getIP();
  
private:
  AsyncWebServer* server;
  AsyncWebSocket* ws;
  WiFiUDP udp;  // Servidor UDP
  bool initialized;
  
  // MIDI Controller reference
  MIDIController* midiController;
  
  // Tracking de clientes UDP
  std::map<String, UdpClient> udpClients;
  void updateUdpClient(IPAddress ip, uint16_t port);
  void cleanupStaleUdpClients();
  
  void onWebSocketEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, 
                       AwsEventType type, void *arg, uint8_t *data, size_t len);
  void processCommand(const JsonDocument& doc);  // Función común para procesar comandos
  
  // File upload handlers
  void handleUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final);
  bool validateWavFile(File& file, uint32_t& sampleRate, uint16_t& channels, uint16_t& bitsPerSample);
};

#endif
