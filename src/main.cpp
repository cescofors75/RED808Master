#include <Arduino.h>
#include <LittleFS.h>
#include <Adafruit_NeoPixel.h>
#include "AudioEngine.h"
#include "SampleManager.h"
#include "KitManager.h"
#include "Sequencer.h"
#include "WebInterface.h"
#include "MIDIController.h"

// --- CONFIGURACIÓN DE HARDWARE ---
#define I2S_BCK   42    // BCLK - Bit Clock
#define I2S_WS    41    // LRC/WS - Word Select (Left/Right Clock)
#define I2S_DOUT  40   // DIN/DOUT - Data (TX pin)

// LED RGB integrado ESP32-S3
#define RGB_LED_PIN  48
#define RGB_LED_NUM  1

// --- OBJETOS GLOBALES ---
AudioEngine audioEngine;
SampleManager sampleManager;
KitManager kitManager;
Sequencer sequencer;
WebInterface webInterface;
MIDIController midiController;
Adafruit_NeoPixel rgbLed(RGB_LED_NUM, RGB_LED_PIN, NEO_GRB + NEO_KHZ800);

// Colores por instrumento - Colores RGB565 profesionales en formato GRB (8 tracks)
const uint32_t instrumentColors[8] = {
    0x00FF00,  // 0: BD (KICK) - Rojo brillante (RGB: 255,0,0 → GRB: 0x00,0xFF,0x00)
    0xA5FF00,  // 1: SD (SNARE) - Naranja (RGB: 255,165,0 → GRB: 0xA5,0xFF,0x00)
    0xFFFF00,  // 2: CH (CL-HAT) - Amarillo (RGB: 255,255,0 → GRB: 0xFF,0xFF,0x00)
    0xFF00FF,  // 3: OH (OP-HAT) - Cian (RGB: 0,255,255 → GRB: 0xFF,0x00,0xFF)
    0x00FFFF,  // 4: CL (CLAP) - Magenta (RGB: 255,0,255 → GRB: 0x00,0xFF,0xFF)
    0xFF0000,  // 5: T1 (TOM-LO) - Verde (RGB: 0,255,0 → GRB: 0xFF,0x00,0x00)
    0xCE38FF,  // 6: T2 (TOM-HI) - Verde agua (RGB: 56,206,255 → GRB: 0xCE,0x38,0xFF)
    0x4D48FF   // 7: CY (CYMBAL) - Azul claro (RGB: 72,77,255 → GRB: 0x4D,0x48,0xFF)
};

// Utility to detect supported audio sample files (.raw or .wav)
static bool isValidSampleFile(const String& filename) {
    return filename.endsWith(".raw") || filename.endsWith(".RAW") ||
           filename.endsWith(".wav") || filename.endsWith(".WAV");
}

// === FUNCIONES DE SECUENCIA LED ===
void showBootLED() {
    // Púrpura BRILLANTE: Inicio del sistema
    rgbLed.setBrightness(255);
    rgbLed.setPixelColor(0, 0xFF00FF); // Magenta más brillante que púrpura
    rgbLed.show();
}

void showLoadingSamplesLED() {
    // Amarillo BRILLANTE: Cargando samples
    rgbLed.setBrightness(255);
    rgbLed.setPixelColor(0, 0xFFFF00);
    rgbLed.show();
}

void showWiFiLED() {
    // Azul BRILLANTE: WiFi activándose
    rgbLed.setBrightness(255);
    rgbLed.setPixelColor(0, 0x0080FF);
    rgbLed.show();
}

void showWebServerLED() {
    // Verde BRILLANTE: Servidor web listo
    rgbLed.setBrightness(255);
    rgbLed.setPixelColor(0, 0x00FF00);
    rgbLed.show();
}

void showReadyLED() {
    // Blanco brillante: Sistema listo
    rgbLed.setPixelColor(0, 0xFFFFFF);
    rgbLed.setBrightness(255);
    rgbLed.show();
    delay(2000); // 2 segundos para ver que está listo
    // Apagar
    rgbLed.clear();
    rgbLed.show();
}

// Variables para control del LED RGB fade
volatile uint8_t ledBrightness = 0;
volatile bool ledFading = false;
volatile bool ledMonoMode = false;

void setLedMonoMode(bool enabled) {
    ledMonoMode = enabled;
    Serial.printf("[LED] Mono mode %s\n", enabled ? "ENABLED" : "DISABLED");
}

// --- TASKS (CORE PINNING) ---
// CORE 1: Audio Processing (Máxima Prioridad)
void audioTask(void *pvParameters) {
    Serial.println("[Task] Audio Task en Core 1 (Prioridad: 24)");
    while (true) {
        audioEngine.process();
        // No yield needed - I2S write blocks naturally via DMA
    }
}

// CORE 0: System, WiFi, Web Server (Prioridad Media)
void systemTask(void *pvParameters) {
    Serial.println("[Task] System Task en Core 0 (Prioridad: 5)");
    
    uint32_t lastLedUpdate = 0;
    
    while (true) {
        sequencer.update();
        webInterface.update();
        webInterface.handleUdp();
        midiController.update();
        
        // Fade out del LED después de trigger
        if (ledFading && millis() - lastLedUpdate > 20) {
            lastLedUpdate = millis();
            if (ledBrightness > 10) {
                ledBrightness -= 8;
                rgbLed.setBrightness(ledBrightness);
                rgbLed.show();
            } else {
                rgbLed.clear();
                rgbLed.show();
                ledFading = false;
                ledBrightness = 0;
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(2)); // 500Hz system loop - mínima latencia WiFi
    }
}

// Callback que el Sequencer llama cada vez que hay un "trigger" en un step
// NO enciende el LED (solo secuenciador)
void onStepTrigger(int track, uint8_t velocity, uint8_t trackVolume) {
    audioEngine.triggerSampleSequencer(track, velocity, trackVolume);
}

// Función para triggers manuales desde live pads (web interface)
// Esta SÍ enciende el LED RGB
void triggerPadWithLED(int track, uint8_t velocity) {
    audioEngine.triggerSampleLive(track, velocity);
    
    // Iluminar LED RGB con color del instrumento
    if (track >= 0 && track < 16) {
        uint32_t color = ledMonoMode ? 0xFF0000 : instrumentColors[track];
        ledBrightness = 255;
        ledFading = true;
        rgbLed.setBrightness(ledBrightness);
        rgbLed.setPixelColor(0, color);
        rgbLed.show();
    }
}

void setup() {
    rgbLed.begin();
    rgbLed.setBrightness(255);
    showBootLED();
    delay(500);
    
    Serial.begin(115200);
    
    // Esperar serial (máximo 3 segundos)
    int waitCount = 0;
    while (!Serial && waitCount < 6) {
        delay(500);
        waitCount++;
    }
    
    Serial.println("\n=================================");
    Serial.println("    BOOT START - RED808");
    Serial.println("=================================");
    Serial.flush();
    
    Serial.println("\n=== RED808 ESP32-S3 DRUM MACHINE ===");
    Serial.println("[STEP 1] Starting Filesystem...");
    Serial.flush();
    
    // 1. Filesystem
    if (!LittleFS.begin(true)) {
        Serial.println("LittleFS FAIL");
        rgbLed.setPixelColor(0, 0xFF0000);
        rgbLed.show();
        while(1) { delay(1000); }
    }
    Serial.println("LittleFS OK");

    Serial.println("[STEP 2] Starting Audio Engine...");
    // 2. Audio Engine (I2S External DAC)
    if (!audioEngine.begin(I2S_BCK, I2S_WS, I2S_DOUT)) {
        Serial.println("AUDIO ENGINE FAIL");
        rgbLed.setPixelColor(0, 0xFF0000);
        rgbLed.show();
        while(1) { delay(1000); }
    }
    Serial.println("Audio Engine OK");
    


    Serial.println("[STEP 3] Initializing Sample Manager...");
    Serial.flush();
    
    showLoadingSamplesLED();
    delay(300);
    
    // 3. Sample Manager - Cargar todos los samples por familia
    sampleManager.begin();
    
    Serial.println("[STEP 5] Loading all samples from families...");
    const char* families[] = {"BD", "SD", "CH", "OH", "CP", "RS", "CL", "CY"};
    
    for (int i = 0; i < 8; i++) {
        String path = String("/") + String(families[i]);
        Serial.printf("  [%d] %s: Opening %s... ", i, families[i], path.c_str());
        
        File dir = LittleFS.open(path, "r");
        
        if (dir && dir.isDirectory()) {
            Serial.println("OK");
            File file = dir.openNextFile();
            bool loaded = false;
            
            while (file && !loaded) {
                if (!file.isDirectory()) {
                    String filename = file.name();
                    if (isValidSampleFile(filename)) {
                        // Extraer solo el nombre del archivo
                        int lastSlash = filename.lastIndexOf('/');
                        if (lastSlash >= 0) {
                            filename = filename.substring(lastSlash + 1);
                        }
                        
                        String fullPath = String("/") + String(families[i]) + "/" + filename;
                        Serial.printf("       Loading %s... ", fullPath.c_str());
                        
                        if (sampleManager.loadSample(fullPath.c_str(), i)) {
                            Serial.printf("✓ (%d bytes)\n", sampleManager.getSampleLength(i) * 2);
                            loaded = true;
                        } else {
                            Serial.println("✗ FAILED");
                        }
                    }
                }
                file.close();
                if (!loaded) {
                    file = dir.openNextFile();
                }
            }
            
            dir.close();
            
            if (!loaded) {
                Serial.println("       ✗ No compatible samples (.raw/.wav) found");
            }
        } else {
            Serial.println("✗ Directory not found or not accessible");
        }
    }
    
    Serial.printf("✓ Samples loaded: %d/8\n", sampleManager.getLoadedSamplesCount());

    // 4. Sequencer Setup
    sequencer.setStepCallback(onStepTrigger);
    
    // Callback para sincronización en tiempo real con la web
    sequencer.setStepChangeCallback([](int newStep) {
        webInterface.broadcastStep(newStep);
    });
    sequencer.setTempo(110); // BPM inicial
    
    // === PATRÓN 0: HIP HOP BOOM BAP (8 tracks) ===
    sequencer.selectPattern(0);
    sequencer.setStep(0, 0, true);   // BD: Kick en 1
    sequencer.setStep(0, 3, true);   // BD: Kick ghost
    sequencer.setStep(0, 10, true);  // BD: Kick sincopado
    sequencer.setStep(1, 4, true);   // SD: Snare en 2
    sequencer.setStep(1, 12, true);  // SD: Snare en 4
    for(int i=0; i<16; i+=2) sequencer.setStep(2, i, true); // CH: patrón cerrado
    sequencer.setStep(3, 6, true);   // OH: para swing
    sequencer.setStep(3, 14, true);  // OH: al final
    sequencer.setStep(4, 4, true);   // CP: Clap doblando snare
    sequencer.setStep(4, 12, true);
    sequencer.setStep(5, 7, true);   // RS: Rimshot fill
    sequencer.setStep(6, 5, true);   // CL: Claves groove
    sequencer.setStep(6, 13, true);  // CL: Claves extra
    sequencer.setStep(7, 15, true);  // CY: Cymbal crash final
    
    // === PATRÓN 1: TECHNO DETROIT (8 tracks) ===
    sequencer.selectPattern(1);
    for(int i=0; i<16; i+=4) sequencer.setStep(0, i, true); // BD: Four on the floor
    sequencer.setStep(1, 4, true);   // SD: Snare en 2
    sequencer.setStep(1, 12, true);  // SD: Snare en 4
    for(int i=0; i<16; i++) sequencer.setStep(2, i, true);  // CH: 16th hi-hats
    sequencer.setStep(3, 8, true);   // OH: en medio
    sequencer.setStep(4, 4, true);   // CP: Clap capa snare
    sequencer.setStep(4, 8, true);
    sequencer.setStep(4, 12, true);
    sequencer.setStep(5, 7, true);   // RS: Rim accent
    sequencer.setStep(5, 11, true);
    sequencer.setStep(5, 15, true);
    sequencer.setStep(6, 3, true);   // CL: Claves offbeat
    sequencer.setStep(6, 7, true);
    sequencer.setStep(6, 11, true);
    sequencer.setStep(6, 15, true);
    sequencer.setStep(7, 0, true);   // CY: Cymbal intro
    sequencer.setStep(7, 8, true);   // CY: medio
    
    // === PATRÓN 2: DRUM & BASS AMEN (8 tracks) ===
    sequencer.selectPattern(2);
    sequencer.setStep(0, 0, true);   // BD: Kick doble
    sequencer.setStep(0, 2, true);
    sequencer.setStep(0, 10, true);  // BD: Kick sincopado
    sequencer.setStep(1, 4, true);   // SD: Snare break
    sequencer.setStep(1, 7, true);   // SD: Snare ghost
    sequencer.setStep(1, 10, true);
    sequencer.setStep(1, 12, true);
    for(int i=0; i<16; i++) sequencer.setStep(2, i, true);  // CH: constante
    sequencer.setStep(3, 6, true);   // OH: textura
    sequencer.setStep(3, 10, true);
    sequencer.setStep(3, 14, true);
    sequencer.setStep(4, 4, true);   // CP: Clap layers
    sequencer.setStep(4, 8, true);
    sequencer.setStep(4, 12, true);
    sequencer.setStep(5, 3, true);   // RS: Rim pattern
    sequencer.setStep(5, 6, true);
    sequencer.setStep(5, 8, true);
    sequencer.setStep(5, 11, true);
    for(int i=0; i<16; i+=3) sequencer.setStep(6, i, true); // CL: Claves fast triplets
    sequencer.setStep(7, 0, true);   // CY: Cymbal intro
    sequencer.setStep(7, 8, true);   // CY: medio
    sequencer.setStep(7, 15, true);  // CY: final
    
    // === PATRÓN 3: BREAKBEAT SHUFFLE (8 tracks) ===
    sequencer.selectPattern(3);
    sequencer.setStep(0, 0, true);   // BD: Kick principal
    sequencer.setStep(0, 5, true);   // BD: Kick offbeat
    sequencer.setStep(0, 10, true);
    sequencer.setStep(1, 4, true);   // SD: Snare backbeat
    sequencer.setStep(1, 12, true);
    sequencer.setStep(1, 13, true);  // SD: Snare flam
    for(int i=0; i<16; i+=3) sequencer.setStep(2, i, true); // CH: shuffle
    sequencer.setStep(3, 6, true);   // OH: acentos
    sequencer.setStep(3, 10, true);
    sequencer.setStep(3, 14, true);
    sequencer.setStep(4, 4, true);   // CP: Clap offbeat
    sequencer.setStep(4, 9, true);
    sequencer.setStep(4, 12, true);
    sequencer.setStep(5, 1, true);   // RS: Rim shuffle
    sequencer.setStep(5, 3, true);
    sequencer.setStep(5, 9, true);
    for(int i=0; i<16; i+=4) sequencer.setStep(6, i, true); // CL: Claves break steady
    sequencer.setStep(7, 0, true);   // CY: Cymbal crash
    sequencer.setStep(7, 12, true);
    
    // === PATRÓN 4: CHICAGO HOUSE (8 tracks) ===
    sequencer.selectPattern(4);
    for(int i=0; i<16; i+=4) sequencer.setStep(0, i, true); // BD: Four on floor
    sequencer.setStep(1, 4, true);   // SD: Snare 2 y 4
    sequencer.setStep(1, 12, true);
    for(int i=2; i<16; i+=4) sequencer.setStep(2, i, true); // CH: offbeat
    sequencer.setStep(3, 6, true);   // OH: sincopado
    sequencer.setStep(3, 10, true);
    sequencer.setStep(3, 14, true);
    sequencer.setStep(4, 4, true);   // CP: Clap dobla snare
    sequencer.setStep(4, 8, true);
    sequencer.setStep(4, 12, true);
    sequencer.setStep(5, 1, true);   // RS: Rim house
    sequencer.setStep(5, 5, true);
    sequencer.setStep(5, 9, true);
    sequencer.setStep(5, 13, true);
    for(int i=0; i<16; i+=4) sequencer.setStep(6, i, true); // CL: Claves steady
    sequencer.setStep(7, 0, true);   // CY: Cymbal intro
    sequencer.setStep(7, 8, true);

    sequencer.selectPattern(0); // Empezar con Hip Hop
    // sequencer.start(); // DISABLED: User must press PLAY
    Serial.println("✓ Sequencer: 5 patrones cargados (Hip Hop, Techno, DnB, Breakbeat, House)");
    Serial.println("   Sequencer en PAUSA - presiona PLAY para iniciar");

    // 5. WiFi AP
    Serial.println("\n[STEP 5] Starting WiFi...");
    
    showWiFiLED();
    delay(500);
    
    if (webInterface.begin("RED808", "red808esp32")) {
        Serial.print("WiFi AP OK | IP: ");
        Serial.println(webInterface.getIP());
        showWebServerLED();
        delay(500);
    } else {
        Serial.println("WiFi FAIL - continuing");
    }

    // --- MIDI USB HOST ---
    Serial.println("\n[STEP 6] Initializing MIDI USB...");
    if (midiController.begin()) {
        webInterface.setMIDIController(&midiController);
        
        midiController.setMessageCallback([](const MIDIMessage& msg) {
            webInterface.broadcastMIDIMessage(msg);
            if (msg.type == MIDI_NOTE_ON && msg.data2 > 0) {
                int8_t pad = midiController.getMappedPad(msg.data1);
                if (pad >= 0 && pad < 8) {
                    triggerPadWithLED(pad, msg.data2);
                }
            }
        });
        
        midiController.setDeviceCallback([](bool connected, const MIDIDeviceInfo& info) {
            webInterface.broadcastMIDIDeviceStatus(connected, info);
        });
        
        Serial.println("MIDI USB Host ready");
    } else {
        Serial.println("MIDI init failed - continuing");
    }

    // --- DUAL-CORE TASKS ---
    Serial.println("\n[STEP 7] Creating dual-core tasks...");
    
    // CORE 1: Audio Task - Prioridad máxima
    xTaskCreatePinnedToCore(
        audioTask,
        "AudioTask",
        8192,   // 8KB stack - optimizado
        NULL,
        24,     // Prioridad máxima
        NULL,
        1       // CORE 1: Audio DSP
    );
    
    // CORE 0: System Task - Prioridad media
    xTaskCreatePinnedToCore(
        systemTask,
        "SystemTask",
        16384,  // 16KB stack - WiFi/JSON
        NULL,
        5,
        NULL,
        0       // CORE 0: WiFi, Web, Sequencer
    );

    showReadyLED();
    Serial.println("\n=== RED808 READY - Connect to WiFi RED808, open 192.168.4.1 ===\n");
}

void loop() {
    // Stats cada 10 segundos (reducido para menos overhead)
    static uint32_t lastStats = 0;
    if (millis() - lastStats > 10000) {
        Serial.printf("Uptime: %ds | Heap: %d | PSRAM: %d\n", 
                      millis()/1000, ESP.getFreeHeap(), ESP.getFreePsram());
        lastStats = millis();
    }
    vTaskDelay(pdMS_TO_TICKS(100)); // loop() no hace nada crítico
}
