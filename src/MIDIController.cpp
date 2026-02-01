#include "MIDIController.h"

// Static callback wrapper
static MIDIController* s_midiInstance = nullptr;

MIDIController::MIDIController() 
  : clientHandle(nullptr)
  , hostTaskHandle(nullptr)
  , initialized(false)
  , hostInitialized(false)
  , historyIndex(0)
  , historyCount(0)
  , totalMessages(0)
  , messagesPerSecond(0)
  , lastSecondTime(0)
  , messagesThisSecond(0)
  , runningStatus(0)
  , dataIndex(0)
{
  deviceInfo.connected = false;
  deviceInfo.deviceName = "No device";
  deviceInfo.vendorId = 0;
  deviceInfo.productId = 0;
  deviceInfo.connectTime = 0;
  
  s_midiInstance = this;
}

MIDIController::~MIDIController() {
  if (hostTaskHandle) {
    vTaskDelete(hostTaskHandle);
  }
  
  if (clientHandle) {
    usb_host_client_unblock(clientHandle);
    usb_host_client_deregister(clientHandle);
  }
  
  if (hostInitialized) {
    usb_host_uninstall();
  }
  
  s_midiInstance = nullptr;
}

bool MIDIController::begin() {
  Serial.println("\n========================================");
  Serial.println("[MIDI USB OTG] Initializing USB Host...");
  Serial.println("========================================");
  Serial.println("[INFO] USB OTG port ready (GPIO 19/20)");
  Serial.println("[INFO] Conecta dispositivo MIDI al puerto USB OTG");
  Serial.println("[INFO] El puerto COM (Serial) sigue activo\n");

  // Install USB Host driver
  usb_host_config_t hostConfig = {
    .skip_phy_setup = false,
    .intr_flags = ESP_INTR_FLAG_LEVEL1
  };

  esp_err_t err = usb_host_install(&hostConfig);
  if (err != ESP_OK) {
    Serial.printf("[MIDI] ❌ Failed to install USB Host: %s\n", esp_err_to_name(err));
    Serial.println("[MIDI] Continuando sin soporte MIDI USB\n");
    return false;
  }
  hostInitialized = true;
  Serial.println("[MIDI] ✓ USB Host driver installed");

  // Register client
  usb_host_client_config_t clientConfig = {
    .is_synchronous = false,
    .max_num_event_msg = 5,
    .async = {
      .client_event_callback = clientEventCallback,
      .callback_arg = this
    }
  };

  err = usb_host_client_register(&clientConfig, &clientHandle);
  if (err != ESP_OK) {
    Serial.printf("[MIDI] ❌ Failed to register client: %s\n", esp_err_to_name(err));
    usb_host_uninstall();
    hostInitialized = false;
    return false;
  }
  Serial.println("[MIDI] ✓ USB Host client registered");

  // Create USB host task
  xTaskCreatePinnedToCore(
    usbHostTask,
    "usb_host_task",
    4096,
    this,
    5,
    &hostTaskHandle,
    0  // Core 0 (WiFi/System core)
  );

  initialized = true;
  Serial.println("[MIDI] ✓ MIDI Controller initialized");
  Serial.println("[MIDI] 🎵 Esperando dispositivo MIDI en USB OTG...");
  Serial.println("========================================\n");
  return true;
}

void MIDIController::usbHostTask(void* arg) {
  MIDIController* controller = static_cast<MIDIController*>(arg);
  
  Serial.println("[MIDI Task] ✓ USB Host task started on Core 0");
  Serial.println("[MIDI Task] Monitoring USB OTG port for connections...\n");
  
  while (true) {
    // Handle USB host events
    usb_host_client_handle_events(controller->clientHandle, pdMS_TO_TICKS(10));
    usb_host_lib_handle_events(pdMS_TO_TICKS(10), nullptr);
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void MIDIController::clientEventCallback(const usb_host_client_event_msg_t* eventMsg, void* arg) {
  MIDIController* controller = static_cast<MIDIController*>(arg);
  
  if (eventMsg->event == USB_HOST_CLIENT_EVENT_NEW_DEV) {
    Serial.println("\n╔═══════════════════════════════════════════════╗");
    Serial.println("║   🎹 DISPOSITIVO MIDI USB DETECTADO 🎹      ║");
    Serial.println("╚═══════════════════════════════════════════════╝");
    Serial.printf("[MIDI] Device address: %d\n", eventMsg->new_dev.address);
    Serial.println("[MIDI] Tipo: USB MIDI Controller");
    Serial.println("[MIDI] Puerto: USB OTG (GPIO 19/20)");
    Serial.println("[INFO] Puedes ver los mensajes MIDI en la web\n");
    
    // Update device info
    controller->deviceInfo.connected = true;
    controller->deviceInfo.deviceName = "USB MIDI Device";
    controller->deviceInfo.connectTime = millis();
    
    controller->notifyDeviceChange(true);
    
  } else if (eventMsg->event == USB_HOST_CLIENT_EVENT_DEV_GONE) {
    Serial.println("\n╔═══════════════════════════════════════════════╗");
    Serial.println("║   ⚠️  DISPOSITIVO MIDI DESCONECTADO ⚠️       ║");
    Serial.println("╚═══════════════════════════════════════════════╝");
    Serial.println("[MIDI] Device removed from USB OTG");
    Serial.println("[MIDI] Esperando nueva conexión...\n");
    
    controller->deviceInfo.connected = false;
    controller->notifyDeviceChange(false);
  }
}

void MIDIController::update() {
  if (!initialized) return;

  // Update messages per second counter
  uint32_t now = millis();
  if (now - lastSecondTime >= 1000) {
    messagesPerSecond = messagesThisSecond;
    messagesThisSecond = 0;
    lastSecondTime = now;
  }
  
  // TODO: Poll MIDI data from USB device
  // This would require USB MIDI class driver implementation
  // For now, we support basic USB host detection
}

void MIDIController::handleMIDIData(const uint8_t* data, size_t length) {
  for (size_t i = 0; i < length; i++) {
    uint8_t byte = data[i];
    
    // Status byte (starts with 1)
    if (byte & 0x80) {
      runningStatus = byte;
      dataIndex = 0;
      
      // Some messages have no data bytes
      if ((runningStatus & 0xF0) == MIDI_PROGRAM_CHANGE || 
          (runningStatus & 0xF0) == MIDI_CHANNEL_PRESSURE) {
        // Wait for 1 data byte
        continue;
      }
    } else {
      // Data byte
      if (runningStatus == 0) continue; // No status set yet
      
      pendingData[dataIndex++] = byte;
      
      // Check if we have complete message
      bool complete = false;
      uint8_t expectedBytes = 2;
      
      if ((runningStatus & 0xF0) == MIDI_PROGRAM_CHANGE || 
          (runningStatus & 0xF0) == MIDI_CHANNEL_PRESSURE) {
        expectedBytes = 1;
      }
      
      if (dataIndex >= expectedBytes) {
        processMIDIMessage(runningStatus, pendingData[0], 
                          expectedBytes > 1 ? pendingData[1] : 0);
        dataIndex = 0;
      }
    }
  }
}

void MIDIController::processMIDIMessage(uint8_t status, uint8_t data1, uint8_t data2) {
  MIDIMessage msg;
  msg.type = status & 0xF0;
  msg.channel = status & 0x0F;
  msg.data1 = data1;
  msg.data2 = data2;
  msg.timestamp = millis();
  
  // Add to history
  messageHistory[historyIndex] = msg;
  historyIndex = (historyIndex + 1) % MAX_HISTORY;
  if (historyCount < MAX_HISTORY) {
    historyCount++;
  }
  
  // Update statistics
  totalMessages++;
  messagesThisSecond++;
  
  // Notify callback
  if (messageCallback) {
    messageCallback(msg);
  }
  
  // Debug output
  const char* msgType = "Unknown";
  const char* icon = "🎵";
  switch (msg.type) {
    case MIDI_NOTE_ON: msgType = "Note On"; icon = "🎹"; break;
    case MIDI_NOTE_OFF: msgType = "Note Off"; icon = "⬜"; break;
    case MIDI_CONTROL_CHANGE: msgType = "CC"; icon = "🎛️"; break;
    case MIDI_PITCH_BEND: msgType = "Pitch Bend"; icon = "🎚️"; break;
    case MIDI_PROGRAM_CHANGE: msgType = "Program"; icon = "🎼"; break;
  }
  
  Serial.printf("[MIDI] %s %s | Ch:%d | D1:%d D2:%d\n", 
                icon, msgType, msg.channel + 1, msg.data1, msg.data2);
}

void MIDIController::getRecentMessages(MIDIMessage* buffer, size_t& count, size_t maxCount) {
  count = min(historyCount, maxCount);
  
  size_t startIdx = historyIndex >= count ? historyIndex - count : MAX_HISTORY - (count - historyIndex);
  
  for (size_t i = 0; i < count; i++) {
    size_t idx = (startIdx + i) % MAX_HISTORY;
    buffer[i] = messageHistory[idx];
  }
}

void MIDIController::notifyDeviceChange(bool connected) {
  if (deviceCallback) {
    deviceCallback(connected, deviceInfo);
  }
  
  if (connected) {
    Serial.println("[WebInterface] Broadcasting MIDI device status to web clients");
    Serial.printf("[MIDI] ✅ Device ready: %s\n", deviceInfo.deviceName.c_str());
    Serial.println("[MIDI] 🎵 Toca notas MIDI 36-43 para triggear pads\n");
  } else {
    Serial.println("[WebInterface] Broadcasting MIDI disconnection to web clients");
    Serial.println("[MIDI] ❌ Device disconnected\n");
  }
}
