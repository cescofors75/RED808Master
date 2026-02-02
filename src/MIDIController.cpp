#include "MIDIController.h"

// Static callback wrapper
static MIDIController* s_midiInstance = nullptr;

// Transfer callback (requerido por USB Host)
static void transferCallback(usb_transfer_t* transfer) {
  // Este callback se llama cuando se completa el transfer
  // No hacemos nada aquí porque procesamos los datos en el main loop
}

MIDIController::MIDIController() 
  : clientHandle(nullptr)
  , hostTaskHandle(nullptr)
  , initialized(false)
  , hostInitialized(false)
  , deviceHandle(nullptr)
  , midiTransfer(nullptr)
  , midiEndpointAddress(0)
  , midiMaxPacketSize(0)
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
  closeMidiDevice();
  
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
  Serial.println("[MIDI Task] Monitoring USB OTG port for connections...");
  Serial.println("[DEBUG] Polling USB host events every 10ms");
  Serial.println("[DEBUG] Conecta/desconecta el dispositivo ahora...\n");
  
  uint32_t lastDebugTime = 0;
  uint32_t pollCount = 0;
  
  while (true) {
    // Handle USB host events
    usb_host_client_handle_events(controller->clientHandle, pdMS_TO_TICKS(10));
    usb_host_lib_handle_events(pdMS_TO_TICKS(10), nullptr);
    
    pollCount++;
    
    // Debug output every 5 seconds to show the task is alive
    if (millis() - lastDebugTime > 5000) {
      Serial.printf("[MIDI Task] Alive - polled %d times in 5s | Device: %s\n", 
                    pollCount, controller->deviceInfo.connected ? "CONNECTED" : "waiting...");
      pollCount = 0;
      lastDebugTime = millis();
    }
    
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void MIDIController::clientEventCallback(const usb_host_client_event_msg_t* eventMsg, void* arg) {
  MIDIController* controller = static_cast<MIDIController*>(arg);
  
  Serial.printf("\n[DEBUG] USB Event received! Type: %d\n", eventMsg->event);
  
  if (eventMsg->event == USB_HOST_CLIENT_EVENT_NEW_DEV) {
    Serial.println("\n╔═══════════════════════════════════════════════╗");
    Serial.println("║   🎹 DISPOSITIVO USB DETECTADO 🎹           ║");
    Serial.println("╚═══════════════════════════════════════════════╝");
    Serial.printf("[MIDI] Device address: %d\n", eventMsg->new_dev.address);
    Serial.println("[MIDI] Intentando abrir y enumerar dispositivo...");
    Serial.println("[MIDI] Puerto: USB OTG (GPIO 19/20)");
    
    // Open MIDI device and start reading
    if (controller->openMidiDevice(eventMsg->new_dev.address)) {
      controller->deviceInfo.connected = true;
      controller->deviceInfo.deviceName = "USB MIDI Device";
      controller->deviceInfo.connectTime = millis();
      controller->notifyDeviceChange(true);
    } else {
      Serial.println("[MIDI] ❌ Failed to open MIDI device");
    }
    
  } else if (eventMsg->event == USB_HOST_CLIENT_EVENT_DEV_GONE) {
    Serial.println("\n╔═══════════════════════════════════════════════╗");
    Serial.println("║   ⚠️  DISPOSITIVO MIDI DESCONECTADO ⚠️       ║");
    Serial.println("╚═══════════════════════════════════════════════╝");
    Serial.println("[MIDI] Device removed from USB OTG");
    Serial.println("[MIDI] Esperando nueva conexión...\n");
    
    controller->closeMidiDevice();
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
  
  // Read MIDI data from USB device
  if (deviceHandle && midiTransfer) {
    readMidiData();
  }
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

bool MIDIController::openMidiDevice(uint8_t deviceAddress) {
  Serial.println("[MIDI] Opening MIDI device...");
  
  // Open the device
  esp_err_t err = usb_host_device_open(clientHandle, deviceAddress, &deviceHandle);
  if (err != ESP_OK) {
    Serial.printf("[MIDI] ❌ Failed to open device: %s\n", esp_err_to_name(err));
    return false;
  }
  Serial.println("[MIDI] ✓ Device opened");
  
  // Get device descriptor
  const usb_device_desc_t* deviceDesc;
  err = usb_host_get_device_descriptor(deviceHandle, &deviceDesc);
  if (err != ESP_OK) {
    Serial.printf("[MIDI] ❌ Failed to get device descriptor: %s\n", esp_err_to_name(err));
    usb_host_device_close(clientHandle, deviceHandle);
    deviceHandle = nullptr;
    return false;
  }
  
  Serial.printf("[MIDI] Device VID:PID = %04X:%04X\n", deviceDesc->idVendor, deviceDesc->idProduct);
  deviceInfo.vendorId = deviceDesc->idVendor;
  deviceInfo.productId = deviceDesc->idProduct;
  
  // Get configuration descriptor
  Serial.println("[MIDI] Getting configuration descriptor...");
  const usb_config_desc_t* configDesc;
  err = usb_host_get_active_config_descriptor(deviceHandle, &configDesc);
  if (err != ESP_OK) {
    Serial.printf("[MIDI] ❌ Failed to get config descriptor: %s\n", esp_err_to_name(err));
    usb_host_device_close(clientHandle, deviceHandle);
    deviceHandle = nullptr;
    return false;
  }
  Serial.printf("[MIDI] ✓ Config descriptor OK (length: %d bytes)\n", configDesc->wTotalLength);
  
  // Listar TODAS las interfaces para debug
  Serial.println("[MIDI] === Listing all interfaces ===");
  const usb_standard_desc_t* desc = (const usb_standard_desc_t*)configDesc;
  uint16_t totalLength = configDesc->wTotalLength;
  uint16_t offset = 0;
  
  while (offset < totalLength) {
    desc = (const usb_standard_desc_t*)((uint8_t*)configDesc + offset);
    
    if (desc->bDescriptorType == USB_B_DESCRIPTOR_TYPE_INTERFACE) {
      const usb_intf_desc_t* intfDesc = (const usb_intf_desc_t*)desc;
      Serial.printf("[MIDI]   Interface #%d: Class=0x%02X, SubClass=0x%02X, Protocol=0x%02X\n",
                    intfDesc->bInterfaceNumber,
                    intfDesc->bInterfaceClass,
                    intfDesc->bInterfaceSubClass,
                    intfDesc->bInterfaceProtocol);
    }
    
    offset += desc->bLength;
  }
  Serial.println("[MIDI] === End interface list ===");
  
  // Buscar interfaz compatible (versión que funcionaba)
  int interfaceNum = -1;
  offset = 0;
  
  while (offset < totalLength) {
    desc = (const usb_standard_desc_t*)((uint8_t*)configDesc + offset);
    
    if (desc->bDescriptorType == USB_B_DESCRIPTOR_TYPE_INTERFACE) {
      const usb_intf_desc_t* intfDesc = (const usb_intf_desc_t*)desc;
      
      // CDC Data: Class=0x0A
      if (intfDesc->bInterfaceClass == 0x0A) {
        interfaceNum = intfDesc->bInterfaceNumber;
        Serial.printf("[MIDI] ✓ Found CDC Data interface: %d\n", interfaceNum);
        break;
      }
      
      // Vendor Specific: Class=0xFF (lo que está enviando ahora el device)
      if (intfDesc->bInterfaceClass == 0xFF) {
        interfaceNum = intfDesc->bInterfaceNumber;
        Serial.printf("[MIDI] ✓ Found Vendor Specific interface: %d\n", interfaceNum);
        break;
      }
    }
    
    offset += desc->bLength;
  }
  
  // Si no encontramos nada, usar la interfaz 0 como fallback
  if (interfaceNum < 0) {
    Serial.println("[MIDI] ⚠️  No standard interface, using interface 0");
    interfaceNum = 0;
  }
  
  if (interfaceNum < 0) {
    Serial.println("[MIDI] ❌ No compatible interface found");
    usb_host_device_close(clientHandle, deviceHandle);
    deviceHandle = nullptr;
    return false;
  }
  
  // Claim the interface
  err = usb_host_interface_claim(clientHandle, deviceHandle, interfaceNum, 0);
  if (err != ESP_OK) {
    Serial.printf("[MIDI] ❌ Failed to claim interface: %s\n", esp_err_to_name(err));
    usb_host_device_close(clientHandle, deviceHandle);
    deviceHandle = nullptr;
    return false;
  }
  Serial.printf("[MIDI] ✓ Interface %d claimed\n", interfaceNum);
  
  // Find BULK IN endpoint for data (prefer BULK over INTERRUPT)
  offset = 0;
  midiEndpointAddress = 0;
  midiMaxPacketSize = 64;
  uint8_t fallbackEndpoint = 0;
  
  Serial.println("[MIDI] Scanning endpoints...");
  
  while (offset < totalLength) {
    desc = (const usb_standard_desc_t*)((uint8_t*)configDesc + offset);
    
    if (desc->bDescriptorType == USB_B_DESCRIPTOR_TYPE_ENDPOINT) {
      const usb_ep_desc_t* epDesc = (const usb_ep_desc_t*)desc;
      
      uint8_t epType = epDesc->bmAttributes & 0x03;
      bool isIN = (epDesc->bEndpointAddress & 0x80);
      
      if (isIN) {
        const char* typeStr = (epType == 0x02) ? "BULK" : 
                             (epType == 0x03) ? "INTERRUPT" : "OTHER";
        Serial.printf("[MIDI]   EP 0x%02X: %s, maxPacket=%d\n", 
                      epDesc->bEndpointAddress, typeStr, epDesc->wMaxPacketSize);
        
        // Preferir BULK (0x02) para datos
        if (epType == 0x02) {
          midiEndpointAddress = epDesc->bEndpointAddress;
          midiMaxPacketSize = epDesc->wMaxPacketSize;
        } else if (epType == 0x03 && midiEndpointAddress == 0) {
          // Usar INTERRUPT solo si no hay BULK
          fallbackEndpoint = epDesc->bEndpointAddress;
          midiMaxPacketSize = epDesc->wMaxPacketSize;
        }
      }
    }
    
    offset += desc->bLength;
  }
  
  // Si no encontramos BULK, usar INTERRUPT
  if (midiEndpointAddress == 0 && fallbackEndpoint != 0) {
    midiEndpointAddress = fallbackEndpoint;
    Serial.printf("[MIDI] ✓ Using INTERRUPT endpoint: 0x%02X\n", midiEndpointAddress);
  } else if (midiEndpointAddress != 0) {
    Serial.printf("[MIDI] ✓ Using BULK endpoint: 0x%02X\n", midiEndpointAddress);
  }
  
  if (midiEndpointAddress == 0) {
    Serial.println("[MIDI] ❌ No IN endpoint found");
    usb_host_interface_release(clientHandle, deviceHandle, interfaceNum);
    usb_host_device_close(clientHandle, deviceHandle);
    deviceHandle = nullptr;
    return false;
  }
  
  // Allocate transfer for reading data
  err = usb_host_transfer_alloc(midiMaxPacketSize, 0, &midiTransfer);
  if (err != ESP_OK) {
    Serial.printf("[MIDI] ❌ Failed to allocate transfer: %s\n", esp_err_to_name(err));
    usb_host_interface_release(clientHandle, deviceHandle, interfaceNum);
    usb_host_device_close(clientHandle, deviceHandle);
    deviceHandle = nullptr;
    return false;
  }
  
  Serial.println("[MIDI] ✓ Transfer allocated");
  Serial.println("[MIDI] ✓ Ready to read MIDI data!");
  return true;
}

void MIDIController::closeMidiDevice() {
  if (midiTransfer) {
    usb_host_transfer_free(midiTransfer);
    midiTransfer = nullptr;
  }
  
  if (deviceHandle) {
    usb_host_device_close(clientHandle, deviceHandle);
    deviceHandle = nullptr;
  }
  
  midiEndpointAddress = 0;
  midiMaxPacketSize = 0;
}

void MIDIController::readMidiData() {
  if (!midiTransfer || !deviceHandle) return;
  
  // Setup transfer
  midiTransfer->device_handle = deviceHandle;
  midiTransfer->bEndpointAddress = midiEndpointAddress;
  midiTransfer->callback = transferCallback; // Ahora con callback
  midiTransfer->context = this;
  midiTransfer->num_bytes = midiMaxPacketSize;
  midiTransfer->timeout_ms = 10;
  
  // Submit transfer
  esp_err_t err = usb_host_transfer_submit(midiTransfer);
  if (err != ESP_OK) {
    return; // No data available or error
  }
  
  // Wait for completion (synchronous mode)
  vTaskDelay(pdMS_TO_TICKS(1)); // Small delay for transfer to complete
  
  // Check if transfer completed
  if (midiTransfer->status == USB_TRANSFER_STATUS_COMPLETED && 
      midiTransfer->actual_num_bytes > 0) {
    
    uint8_t* data = midiTransfer->data_buffer;
    size_t numBytes = midiTransfer->actual_num_bytes;
    
    // Debug: mostrar datos raw recibidos
    static uint32_t lastDebugPrint = 0;
    if (millis() - lastDebugPrint > 500) {
      Serial.printf("[MIDI] Received %d bytes: ", numBytes);
      for (size_t i = 0; i < min(numBytes, (size_t)16); i++) {
        Serial.printf("%02X ", data[i]);
      }
      Serial.println();
      lastDebugPrint = millis();
    }
    
    // Procesar como MIDI raw (3 bytes por mensaje)
    // El device envía: [status byte] [data1] [data2]
    handleMIDIData(data, numBytes);
  }
}
