/*
 * WebInterface.h
 * RED808 Web Interface Header
 */

#ifndef WEBINTERFACE_H
#define WEBINTERFACE_H

#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <AsyncWebSocket.h>
#include <LittleFS.h>
#include <ArduinoJson.h>

class WebInterface {
public:
  WebInterface();
  ~WebInterface();
  
  bool begin(const char* ssid, const char* password);
  void update();
  
  void broadcastSequencerState();
  void broadcastPadTrigger(int pad);
  void broadcastStep(int step);
  void broadcastVisualizationData();
  
  String getIP();
  
private:
  AsyncWebServer* server;
  AsyncWebSocket* ws;
  
  void onWebSocketEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, 
                       AwsEventType type, void *arg, uint8_t *data, size_t len);
};

#endif
