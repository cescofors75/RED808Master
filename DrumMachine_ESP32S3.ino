/*
 * ESP32-S3 Drum Machine
 * Cesco - 2025
 * 
 * Hardware:
 * - ESP32-S3 amb placa d'expansió
 * - PCM5102A DAC (I2S)
 * - Pantalla ST7789 240x240 (SPI)
 * - 4 botons navegació
 * - Pads/triggers
 */

#include <Arduino.h>
#include <LittleFS.h>
#include <FS.h>
#include "AudioEngine.h"
// DisplayManager e InputManager deshabilitados temporalmente para debugging
// #include "DisplayManager.h"
// #include "InputManager.h"
#include "SampleManager.h"
#include "KitManager.h"
#include "Sequencer.h"
#include "WebInterface.h"

// ===== HARDWARE CONFIGURATION =====
// Configura el hardware que tens connectat
// #define HAS_DISPLAY     // Pantalla ST7789 240x240 (deshabilitada para debugging)
#define HAS_AUDIO       // PCM5102A DAC
// #define HAS_BUTTONS     // 4 botons navegació (deshabilitados para debugging)
// #define HAS_PADS     // Descomenta si tens pads físics connectats

// Si NO tens pads físics, el control és 100% via web interface (iPad)

// TRIA UNA DE LES DUES OPCIONS:

// Opció 1: Connectar a la teva WiFi
//#define WIFI_MODE_STATION  // Descomenta per connectar a WiFi
//#define WIFI_SSID "EL_TEU_WIFI"
//#define WIFI_PASSWORD "LA_TEVA_PASSWORD"

// Opció 2: Crear Access Point propi
#define WIFI_MODE_AP  // Descomenta per crear AP
#define AP_SSID "RED808"
#define AP_PASSWORD "red808esp32"  // Deixa buit per AP obert

// ===== PIN CONFIGURATION =====
// I2S (PCM5102A)
#define I2S_BCK_PIN   42
#define I2S_WS_PIN    41
#define I2S_DATA_PIN  2

// SPI Display (ST7789)
#define TFT_CS        10
#define TFT_DC        11
#define TFT_RST       12
#define TFT_MOSI      13
#define TFT_SCLK      14
// NO backlight pin (connectat directament a 3.3V)

// Navigation Buttons
#define BTN_UP        15
#define BTN_DOWN      16
#define BTN_SELECT    17
#define BTN_BACK      18

// Trigger Pads (ADC or Digital)
#define PAD_1         4
#define PAD_2         5
#define PAD_3         6
#define PAD_4         7
#define PAD_5         8
#define PAD_6         9
#define PAD_7         20
#define PAD_8         21
#define PAD_9         35
#define PAD_10        36
#define PAD_11        37
#define PAD_12        38
#define PAD_13        39
#define PAD_14        40
#define PAD_15        45
#define PAD_16        46

// ===== GLOBAL OBJECTS =====
AudioEngine audioEngine;
// DisplayManager display; // Deshabilitado para debugging
// InputManager input; // Deshabilitado para debugging
SampleManager sampleManager;
KitManager kitManager;
Sequencer sequencer;
WebInterface webInterface;

// ===== TASK HANDLES =====
TaskHandle_t audioTaskHandle = NULL;
TaskHandle_t systemTaskHandle = NULL;

// ===== AUDIO TASK (Core 1) =====
void audioTask(void* parameter) {
  Serial.println("[AUDIO] Task started on Core 1");
  
  while (true) {
    audioEngine.process();
    // sequencer.process(); // Sequencer is handled by AudioEngine
    vTaskDelay(1); // Yield to other tasks
  }
}

// ===== SYSTEM TASK (Core 0) =====
void systemTask(void* parameter) {
  Serial.println("[SYSTEM] Task started on Core 0");
  
  while (true) {
    // Solo procesa web interface y secuenciador
    webInterface.update();
    
    vTaskDelay(10); // 100Hz update rate
  }
}

// ===== SETUP =====
void setup() {
  // Serial con delay para ESP32-S3
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println();
  Serial.println("=================================");
  Serial.println("   RED808 DRUM MACHINE ESP32-S3");  
  Serial.println("=================================");
  Serial.flush();
  delay(500);
  
  // Initialize LittleFS
  Serial.println("[FS] Mounting LittleFS...");
  if (!LittleFS.begin(true)) {
    Serial.println("[FS] ERROR: LittleFS Mount Failed!");
    while(1) { delay(1000); }
  }
  Serial.println("[FS] LittleFS mounted successfully");
  Serial.flush();
  
  // Check PSRAM
  if (psramFound()) {
    Serial.printf("[PSRAM] Found: %d bytes\n", ESP.getPsramSize());
    Serial.printf("[PSRAM] Free: %d bytes\n", ESP.getFreePsram());
  } else {
    Serial.println("[PSRAM] WARNING: No PSRAM found!");
  }
  Serial.flush();
  
  // Initialize components  
  Serial.println("[SAMPLE] Initializing Sample Manager...");
  sampleManager.begin();
  Serial.flush();
  
  Serial.println("[AUDIO] Initializing Audio Engine...");
  audioEngine.begin(I2S_BCK_PIN, I2S_WS_PIN, I2S_DATA_PIN);
  Serial.flush();
  
  Serial.println("[SEQUENCER] Initializing Sequencer...");
  // sequencer.begin(&audioEngine); // Method doesn't exist, initialize directly
  sequencer.setTempo(110);
  Serial.flush();
  
  // Load kits (replaces loadDefaultSamples)
  Serial.println("[KIT] Loading kits...");
  if (!kitManager.begin()) {
    Serial.println("[KIT] WARNING: No kits found!");
  } else {
    kitManager.printKitInfo(kitManager.getCurrentKit());
  }
  Serial.flush();
  
  // Setup Web Interface
  Serial.println("[WEB] Initializing Web Interface...");
  Serial.flush();

#ifdef WIFI_MODE_AP
  webInterface.begin(AP_SSID, AP_PASSWORD);
  
  Serial.println("[WIFI] Access Point started:");
  Serial.print("  SSID: ");
  Serial.println(AP_SSID);
  Serial.print("  Password: ");
  Serial.println(AP_PASSWORD);
  Serial.print("  IP: ");
  Serial.println(webInterface.getIP());
  Serial.println("  Connect and open: http://192.168.4.1");
  Serial.flush();
#endif
  
  // Create tasks on different cores
  Serial.println("[TASK] Creating Core 1 task (Audio)...");
  xTaskCreatePinnedToCore(
    audioTask,        // Function
    "AudioTask",      // Name
    8192,             // Stack size
    NULL,             // Parameters
    2,                // Priority (high)
    &audioTaskHandle, // Handle
    1                 // Core 1
  );
  Serial.flush();
  
  Serial.println("[TASK] Creating Core 0 task (System)...");
  xTaskCreatePinnedToCore(
    systemTask,       // Function
    "SystemTask",     // Name
    8192,             // Stack size
    NULL,             // Parameters
    1,                // Priority (normal)
    &systemTaskHandle,// Handle
    0                 // Core 0
  );
  Serial.flush();
  
  Serial.println();
  Serial.println("=================================");
  Serial.println("   SYSTEM READY!");
  Serial.println("   Connect to: RED808");
  Serial.println("   Open: http://192.168.4.1");
  Serial.println("=================================");
  Serial.println();
  Serial.flush();
}

// ===== MAIN LOOP =====
void loop() {
  // Todo se maneja en las tareas, el loop principal solo espera
  delay(1000);
  
  // Imprime estadísticas cada segundo
  static uint32_t lastStats = 0;
  if (millis() - lastStats > 10000) { // Cada 10 segundos
    lastStats = millis();
    Serial.printf("[STATS] Free heap: %d bytes | Free PSRAM: %d bytes\n", 
                  ESP.getFreeHeap(), ESP.getFreePsram());
    Serial.flush();
  }
}

// ===== FIN DEL CÓDIGO =====
// Las funciones setupPads(), checkPads() y loadDefaultSamples() han sido eliminadas
// ya que no son necesarias para el funcionamiento actual del sistema.
// El control es 100% via web interface.
