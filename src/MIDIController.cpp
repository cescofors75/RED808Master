#include "MIDIController.h"
#include <Preferences.h>

// Static callback wrapper
static MIDIController* s_midiInstance = nullptr;

// Transfer callback — se llama cuando USB Host completa una lectura
static void transferCallback(usb_transfer_t* transfer) {
  if (!transfer || !transfer->context) return;
  MIDIController* ctrl = static_cast<MIDIController*>(transfer->context);

  if (transfer->status == USB_TRANSFER_STATUS_COMPLETED && transfer->actual_num_bytes > 0) {
    ctrl->handleMIDIData(transfer->data_buffer, transfer->actual_num_bytes);
  }

  // Re-submit para lectura continua
  if (transfer->status != USB_TRANSFER_STATUS_CANCELED &&
      transfer->status != USB_TRANSFER_STATUS_STALL) {
    usb_host_transfer_submit(transfer);
  }
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
  , interfaceNum(-1)
  , historyIndex(0)
  , historyCount(0)
  , totalMessages(0)
  , messagesPerSecond(0)
  , lastSecondTime(0)
  , messagesThisSecond(0)
  , runningStatus(0)
  , dataIndex(0)
  , mappingCount(0)
{
  deviceInfo.connected = false;
  deviceInfo.deviceName = "No device";
  deviceInfo.vendorId = 0;
  deviceInfo.productId = 0;
  deviceInfo.connectTime = 0;
  
  // Cargar mapeo desde NVS; si no existe, usar mapa GM por defecto
  resetToDefaultMapping();
  loadMappings();
  
  scanEnabled = false;  // Desactivado por defecto, se activa desde la web
  
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
    if (controller->scanEnabled) {
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
    } else {
      // Scan disabled - sleep longer to save CPU
      vTaskDelay(pdMS_TO_TICKS(200));
    }
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
  if (!initialized || !scanEnabled) return;

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
  // USB MIDI packets: 4 bytes cada uno [Cable/CIN, Status, Data1, Data2]
  // CIN (Code Index Number) en nibble bajo del primer byte indica el tipo de mensaje
  
  for (size_t i = 0; i + 3 < length; i += 4) {
    uint8_t header = data[i];
    uint8_t status = data[i + 1];
    uint8_t data1 = data[i + 2];
    uint8_t data2 = data[i + 3];
    
    uint8_t cin = header & 0x0F; // Code Index Number
    
    // Filtrar paquetes válidos según CIN
    // 0x08 = Note Off, 0x09 = Note On, 0x0B = Control Change, 0x0E = Pitch Bend, etc.
    if (cin >= 0x08 && cin <= 0x0F) {
      // Procesar solo si el status byte es válido (bit 7 = 1)
      if (status & 0x80) {
        processMIDIMessage(status, data1, data2);
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
  
  // Close previous device if exists
  if (deviceHandle) {
    Serial.println("[MIDI] Closing previous device first...");
    closeMidiDevice();
    vTaskDelay(pdMS_TO_TICKS(100)); // Wait for cleanup
  }
  
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
  
  // ── Buscar interfaz compatible (prioridad: MIDI Streaming > CDC > Vendor) ──
  interfaceNum = -1;
  offset = 0;
  int cdcInterface    = -1;
  int vendorInterface = -1;
  int anyInterface    = -1;

  while (offset < totalLength) {
    desc = (const usb_standard_desc_t*)((uint8_t*)configDesc + offset);

    if (desc->bDescriptorType == USB_B_DESCRIPTOR_TYPE_INTERFACE) {
      const usb_intf_desc_t* intfDesc = (const usb_intf_desc_t*)desc;

      // Clase 0x01 Audio, Subclase 0x03 MIDI Streaming — Arturia, Roland, Korg...
      if (intfDesc->bInterfaceClass == 0x01 && intfDesc->bInterfaceSubClass == 0x03) {
        interfaceNum = intfDesc->bInterfaceNumber;
        Serial.printf("[MIDI] ✓ Found USB MIDI Streaming interface: %d\n", interfaceNum);
        break;
      }
      // CDC Data 0x0A
      if (intfDesc->bInterfaceClass == 0x0A && cdcInterface < 0)
        cdcInterface = intfDesc->bInterfaceNumber;
      // Vendor Specific 0xFF
      if (intfDesc->bInterfaceClass == 0xFF && vendorInterface < 0)
        vendorInterface = intfDesc->bInterfaceNumber;
      // Cualquiera con endpoints
      if (intfDesc->bNumEndpoints > 0 && anyInterface < 0)
        anyInterface = intfDesc->bInterfaceNumber;
    }

    offset += desc->bLength;
  }

  // Fallback en orden de preferencia
  if (interfaceNum < 0) interfaceNum = cdcInterface;
  if (interfaceNum < 0) interfaceNum = vendorInterface;
  if (interfaceNum < 0) interfaceNum = anyInterface;
  if (interfaceNum < 0) interfaceNum = 0; // último recurso

  Serial.printf("[MIDI] Using interface: %d\n", interfaceNum);
  
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
  Serial.println("[MIDI] Closing device and freeing resources...");
  
  if (midiTransfer) {
    usb_host_transfer_free(midiTransfer);
    midiTransfer = nullptr;
    Serial.println("[MIDI] ✓ Transfer freed");
  }
  
  // Release interface before closing device
  if (deviceHandle && interfaceNum >= 0) {
    esp_err_t err = usb_host_interface_release(clientHandle, deviceHandle, interfaceNum);
    if (err == ESP_OK) {
      Serial.printf("[MIDI] ✓ Interface %d released\n", interfaceNum);
    }
    interfaceNum = -1;
  }
  
  if (deviceHandle) {
    usb_host_device_close(clientHandle, deviceHandle);
    deviceHandle = nullptr;
    Serial.println("[MIDI] ✓ Device closed");
  }
  
  midiEndpointAddress = 0;
  midiMaxPacketSize = 0;
  
  Serial.println("[MIDI] ✓ All resources freed");
}

void MIDIController::readMidiData() {
  if (!midiTransfer || !deviceHandle) return;

  // Solo enviar si el transfer NO está ya en vuelo
  // (el callback se encarga de re-submitear automáticamente)
  static bool transferSubmitted = false;
  if (transferSubmitted) return;

  midiTransfer->device_handle    = deviceHandle;
  midiTransfer->bEndpointAddress = midiEndpointAddress;
  midiTransfer->callback         = transferCallback;
  midiTransfer->context          = this;
  midiTransfer->num_bytes        = midiMaxPacketSize;
  midiTransfer->timeout_ms       = 0; // 0 = sin timeout, lectura continua

  esp_err_t err = usb_host_transfer_submit(midiTransfer);
  if (err == ESP_OK) {
    transferSubmitted = true;
    Serial.println("[MIDI] ✓ Continuous read transfer submitted");
  } else {
    Serial.printf("[MIDI] ⚠️ Transfer submit failed: %s\n", esp_err_to_name(err));
  }
}

// ========================================
// MIDI NOTE MAPPING FUNCTIONS
// ========================================

void MIDIController::setPadMapping(int8_t pad, uint8_t newNote) {
  // Buscar en los primeros 16 (mapeos principales) el que tenga este pad
  for (int i = 0; i < mappingCount && i < 16; i++) {
    if (noteMappings[i].pad == pad) {
      noteMappings[i].note    = newNote;
      noteMappings[i].enabled = true;
      Serial.printf("[MIDI Mapping] Pad %d → Note %d\n", pad, newNote);
      saveMappings();
      return;
    }
  }
  // Si no estaba en los primeros 16, añadir nuevo
  setNoteMapping(newNote, pad);
}

void MIDIController::setNoteMapping(uint8_t note, int8_t pad) {
  // Buscar si ya existe mapeo para esta nota
  for (int i = 0; i < mappingCount; i++) {
    if (noteMappings[i].note == note) {
      noteMappings[i].pad = pad;
      noteMappings[i].enabled = (pad >= 0);
      Serial.printf("[MIDI Mapping] Updated: Note %d → Pad %d\n", note, pad);
      saveMappings();
      return;
    }
  }
  
  // Agregar nuevo mapeo si hay espacio
  if (mappingCount < MAX_MIDI_MAPPINGS) {
    noteMappings[mappingCount].note = note;
    noteMappings[mappingCount].pad = pad;
    noteMappings[mappingCount].enabled = (pad >= 0);
    mappingCount++;
    Serial.printf("[MIDI Mapping] Added: Note %d → Pad %d\n", note, pad);
    saveMappings();
  } else {
    Serial.println("[MIDI Mapping] ⚠️ Maximum mappings reached!");
  }
}

int8_t MIDIController::getMappedPad(uint8_t note) const {
  for (int i = 0; i < mappingCount; i++) {
    if (noteMappings[i].note == note && noteMappings[i].enabled) {
      return noteMappings[i].pad;
    }
  }
  return -1; // No mapping found
}

void MIDIController::clearMapping(uint8_t note) {
  for (int i = 0; i < mappingCount; i++) {
    if (noteMappings[i].note == note) {
      noteMappings[i].enabled = false;
      Serial.printf("[MIDI Mapping] Cleared: Note %d\n", note);
      return;
    }
  }
}

void MIDIController::resetToDefaultMapping() {
  mappingCount = 0;

  // ================================================================
  // Mapa General MIDI Drum (GM) completo para los 16 pads RED808
  // ================================================================
  // Pad  0: BD  (Bass Drum)      → 36 C1  (Acoustic Bass Drum)
  // Pad  1: SD  (Snare)          → 38 D1  (Acoustic Snare)
  // Pad  2: CH  (Closed Hi-Hat)  → 42 F#1 (Closed Hi-Hat)
  // Pad  3: OH  (Open Hi-Hat)    → 46 A#1 (Open Hi-Hat)
  // Pad  4: CY  (Cymbal)         → 49 C#2 (Crash Cymbal 1)
  // Pad  5: CP  (Clap)           → 39 D#1 (Hand Clap)
  // Pad  6: RS  (Rimshot)        → 37 C#1 (Side Stick / Rimshot)
  // Pad  7: CB  (Cowbell)        → 56 G#2 (Cowbell)
  // Pad  8: LT  (Low Tom)        → 41 F1  (Low Floor Tom)
  // Pad  9: MT  (Mid Tom)        → 47 B1  (Low-Mid Tom)
  // Pad 10: HT  (High Tom)       → 50 D2  (High Tom)
  // Pad 11: MA  (Maracas)        → 70 A#3 (Maracas)
  // Pad 12: CL  (Claves)         → 75 D#4 (Claves)
  // Pad 13: HC  (Hi Conga)       → 62 D3  (Mute Hi Conga)
  // Pad 14: MC  (Mid Conga)      → 63 D#3 (Open Hi Conga)
  // Pad 15: LC  (Low Conga)      → 64 E3  (Low Conga)

  struct { uint8_t note; int8_t pad; } defaultMap[16] = {
    {36, 0},  // BD  - Acoustic Bass Drum
    {38, 1},  // SD  - Acoustic Snare
    {42, 2},  // CH  - Closed Hi-Hat
    {46, 3},  // OH  - Open Hi-Hat
    {49, 4},  // CY  - Crash Cymbal 1
    {39, 5},  // CP  - Hand Clap
    {37, 6},  // RS  - Side Stick
    {56, 7},  // CB  - Cowbell
    {41, 8},  // LT  - Low Floor Tom
    {47, 9},  // MT  - Low-Mid Tom
    {50, 10}, // HT  - High Tom
    {70, 11}, // MA  - Maracas
    {75, 12}, // CL  - Claves
    {62, 13}, // HC  - Mute Hi Conga
    {63, 14}, // MC  - Open Hi Conga
    {64, 15}  // LC  - Low Conga
  };

  for (int i = 0; i < 16; i++) {
    noteMappings[i].note    = defaultMap[i].note;
    noteMappings[i].pad     = defaultMap[i].pad;
    noteMappings[i].enabled = true;
  }
  mappingCount = 16;

  // Notas alternativas GM (alias → mismo pad)
  noteMappings[mappingCount++] = {35, 0, true};  // Acoustic Bass Drum 2 → BD
  noteMappings[mappingCount++] = {40, 1, true};  // Electric Snare       → SD
  noteMappings[mappingCount++] = {44, 2, true};  // Pedal Hi-Hat         → CH
  noteMappings[mappingCount++] = {51, 4, true};  // Ride Cymbal 1        → CY
  noteMappings[mappingCount++] = {57, 4, true};  // Crash Cymbal 2       → CY
  noteMappings[mappingCount++] = {59, 4, true};  // Ride Cymbal 2        → CY
  noteMappings[mappingCount++] = {43, 8, true};  // High Floor Tom       → LT
  noteMappings[mappingCount++] = {45, 9, true};  // Low Tom              → MT

  Serial.println("[MIDI Mapping] Reset to GM Drum Map (16 pads)");
  Serial.println("  BD=36, SD=38, CH=42, OH=46, CY=49, CP=39, RS=37, CB=56");
  Serial.println("  LT=41, MT=47, HT=50, MA=70, CL=75, HC=62, MC=63, LC=64");
  saveMappings();
}

void MIDIController::saveMappings() {
  Preferences prefs;
  prefs.begin("midi_map", false);
  prefs.putInt("count", mappingCount);
  for (int i = 0; i < mappingCount; i++) {
    char keyNote[12], keyPad[12], keyEn[12];
    snprintf(keyNote, sizeof(keyNote), "n%d", i);
    snprintf(keyPad,  sizeof(keyPad),  "p%d", i);
    snprintf(keyEn,   sizeof(keyEn),   "e%d", i);
    prefs.putUChar(keyNote, noteMappings[i].note);
    prefs.putChar (keyPad,  noteMappings[i].pad);
    prefs.putBool (keyEn,   noteMappings[i].enabled);
  }
  prefs.end();
  Serial.printf("[MIDI Mapping] Saved %d mappings to NVS\n", mappingCount);
}

void MIDIController::loadMappings() {
  Preferences prefs;
  prefs.begin("midi_map", true);  // read-only
  int count = prefs.getInt("count", -1);
  prefs.end();

  if (count <= 0 || count > MAX_MIDI_MAPPINGS) {
    Serial.println("[MIDI Mapping] No saved mapping found, using GM defaults");
    return;  // Mantener el GM por defecto ya cargado
  }

  Preferences prefs2;
  prefs2.begin("midi_map", true);
  mappingCount = 0;
  for (int i = 0; i < count; i++) {
    char keyNote[12], keyPad[12], keyEn[12];
    snprintf(keyNote, sizeof(keyNote), "n%d", i);
    snprintf(keyPad,  sizeof(keyPad),  "p%d", i);
    snprintf(keyEn,   sizeof(keyEn),   "e%d", i);
    noteMappings[i].note    = prefs2.getUChar(keyNote, 0);
    noteMappings[i].pad     = prefs2.getChar (keyPad,  -1);
    noteMappings[i].enabled = prefs2.getBool (keyEn,   false);
    mappingCount++;
  }
  prefs2.end();
  Serial.printf("[MIDI Mapping] Loaded %d mappings from NVS\n", mappingCount);
}

const MIDINoteMapping* MIDIController::getAllMappings(int& count) const {
  count = mappingCount;
  return noteMappings;
}
