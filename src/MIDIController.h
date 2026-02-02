#ifndef MIDI_CONTROLLER_H
#define MIDI_CONTROLLER_H

#include <Arduino.h>
#include "usb/usb_host.h"
#include <functional>

// MIDI Message Types
#define MIDI_NOTE_OFF 0x80
#define MIDI_NOTE_ON 0x90
#define MIDI_AFTERTOUCH 0xA0
#define MIDI_CONTROL_CHANGE 0xB0
#define MIDI_PROGRAM_CHANGE 0xC0
#define MIDI_CHANNEL_PRESSURE 0xD0
#define MIDI_PITCH_BEND 0xE0

struct MIDIMessage {
  uint8_t type;       // Message type (0x80-0xF0)
  uint8_t channel;    // MIDI channel (0-15)
  uint8_t data1;      // Note number, CC number, etc.
  uint8_t data2;      // Velocity, CC value, etc.
  uint32_t timestamp; // millis() when received
};

struct MIDIDeviceInfo {
  bool connected;
  String deviceName;
  uint16_t vendorId;
  uint16_t productId;
  uint32_t connectTime;
};

// Callback types
typedef std::function<void(const MIDIMessage&)> MIDIMessageCallback;
typedef std::function<void(bool connected, const MIDIDeviceInfo&)> MIDIDeviceCallback;

class MIDIController {
public:
  MIDIController();
  ~MIDIController();

  // Initialization
  bool begin();
  void update();

  // Device info
  bool isDeviceConnected() const { return deviceInfo.connected; }
  MIDIDeviceInfo getDeviceInfo() const { return deviceInfo; }

  // Message handling
  void setMessageCallback(MIDIMessageCallback callback) { messageCallback = callback; }
  void setDeviceCallback(MIDIDeviceCallback callback) { deviceCallback = callback; }

  // Get recent messages (for web display)
  void getRecentMessages(MIDIMessage* buffer, size_t& count, size_t maxCount);

  // Statistics
  uint32_t getTotalMessagesReceived() const { return totalMessages; }
  uint32_t getMessagesPerSecond() const { return messagesPerSecond; }

private:
  static void usbHostTask(void* arg);
  static void clientEventCallback(const usb_host_client_event_msg_t* eventMsg, void* arg);
  
  void handleMIDIData(const uint8_t* data, size_t length);
  void processMIDIMessage(uint8_t status, uint8_t data1, uint8_t data2);
  void notifyDeviceChange(bool connected);
  bool openMidiDevice(uint8_t deviceAddress);
  void closeMidiDevice();
  void readMidiData();

  // USB Host variables
  usb_host_client_handle_t clientHandle;
  TaskHandle_t hostTaskHandle;
  bool initialized;
  bool hostInitialized;
  usb_device_handle_t deviceHandle;
  usb_transfer_t* midiTransfer;
  uint8_t midiEndpointAddress;
  uint16_t midiMaxPacketSize;

  // Device state
  MIDIDeviceInfo deviceInfo;

  // Message handling
  MIDIMessageCallback messageCallback;
  MIDIDeviceCallback deviceCallback;

  // Message history (circular buffer for web display)
  static const size_t MAX_HISTORY = 32;
  MIDIMessage messageHistory[MAX_HISTORY];
  size_t historyIndex;
  size_t historyCount;

  // Statistics
  uint32_t totalMessages;
  uint32_t messagesPerSecond;
  uint32_t lastSecondTime;
  uint32_t messagesThisSecond;

  // MIDI parsing state
  uint8_t runningStatus;
  uint8_t dataIndex;
  uint8_t pendingData[2];
};

#endif // MIDI_CONTROLLER_H
