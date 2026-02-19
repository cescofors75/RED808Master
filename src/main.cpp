#include <Arduino.h>
#include <LittleFS.h>
#include <SPI.h>
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



// --- WiFi: Red Doméstica (modo STA) ---
// Pon aquí tu SSID y contraseña WiFi de casa.
// Si se deja vacío (""), usará solo modo AP (red propia RED808).
#define HOME_WIFI_SSID     "MIWIFI_2G_yU2f"         // ← tu WiFi doméstico, ej: "MiRedCasa"
#define HOME_WIFI_PASS     "M6LR7zHk"   
//#define HOME_WIFI_SSID     "Mr_Coconut"         // ← tu WiFi doméstico, ej: "MiRedCasa"
//#define HOME_WIFI_PASS     "Llorosenlalluvia82" 
// ← contraseña WiFi
//#define HOME_WIFI_SSID     "iPhone de Francesc "         // ← tu WiFi doméstico, ej: "MiRedCasa"
//#define HOME_WIFI_PASS     "gp5zoiqszdy9j"         // ← contraseña WiFi

#define HOME_WIFI_TIMEOUT  12000      // ms para intentar conectar (12s)

// AP fallback (siempre disponible si STA falla)
#define AP_SSID     "RED808"
#define AP_PASSWORD "red808esp32"

// --- OBJETOS GLOBALES ---
AudioEngine audioEngine;
SampleManager sampleManager;
KitManager kitManager;
Sequencer sequencer;
WebInterface webInterface;
MIDIController midiController;
Adafruit_NeoPixel rgbLed(RGB_LED_NUM, RGB_LED_PIN, NEO_GRB + NEO_KHZ800);

// Colores por instrumento - 16 instrumentos RGB (formato 0xRRGGBB estándar)
const uint32_t instrumentColors[16] = {
    0xFF0000,  // 0: BD (KICK) - Rojo
    0xFFA500,  // 1: SD (SNARE) - Naranja
    0xFFFF00,  // 2: CH (CL-HAT) - Amarillo
    0x00FFFF,  // 3: OH (OP-HAT) - Cian
    0xE6194B,  // 4: CY (CYMBAL) - Carmín
    0xFF00FF,  // 5: CP (CLAP) - Magenta
    0x00FF00,  // 6: RS (RIMSHOT) - Verde
    0xF58231,  // 7: CB (COWBELL) - Naranja oscuro
    0x911EB4,  // 8: LT (LOW TOM) - Púrpura
    0x46F0F0,  // 9: MT (MID TOM) - Turquesa
    0xF032E6,  // 10: HT (HIGH TOM) - Rosa
    0xBCF60C,  // 11: MA (MARACAS) - Lima
    0x38CEFF,  // 12: CL (CLAVES) - Azul claro
    0xFABEBE,  // 13: HC (HI CONGA) - Rosa pálido
    0x008080,  // 14: MC (MID CONGA) - Teal
    0x484DFF   // 15: LC (LOW CONGA) - Azul índigo
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
void onStepTrigger(int track, uint8_t velocity, uint8_t trackVolume, uint32_t noteLenSamples) {
    audioEngine.triggerSampleSequencer(track, velocity, trackVolume, noteLenSamples);
}

// Función para triggers manuales desde live pads (web interface)
// Esta SÍ enciende el LED RGB
void triggerPadWithLED(int track, uint8_t velocity) {
    audioEngine.triggerSampleLive(track, velocity);
    
    // Iluminar LED RGB con color del instrumento (solo pads principales 0-15)
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
    const char* families[] = {"BD", "SD", "CH", "OH", "CY", "CP", "RS", "CB", "LT", "MT", "HT", "MA", "CL", "HC", "MC", "LC"};
    
    // ============= RED 808 KARZ - Default Sample Kit =============
    // Mapping: filename prefix -> family index
    // 808 BD -> BD(0), 808 SD -> SD(1), 808 HH -> CH(2), 808 OH -> OH(3),
    // 808 CY -> CY(4), 808 CP -> CP(5), 808 RS -> RS(6), 808 COW -> CB(7),
    // 808 LT -> LT(8), 808 MT -> MT(9), 808 HT -> HT(10), 808 MA -> MA(11),
    // 808 CL -> CL(12), 808 HC -> HC(13), 808 MC -> MC(14), 808 LC -> LC(15)
    struct KarzMapping {
        const char* prefix;  // Prefix in filename (after "808 ")
        int padIndex;        // Target pad/family index
    };
    const KarzMapping karzMap[] = {
        {"BD", 0}, {"SD", 1}, {"HH", 2}, {"OH", 3},
        {"CY", 4}, {"CP", 5}, {"RS", 6}, {"COW", 7},
        {"LT", 8}, {"MT", 9}, {"HT", 10}, {"MA", 11},
        {"CL", 12}, {"HC", 13}, {"MC", 14}, {"LC", 15}
    };
    const int karzMapSize = sizeof(karzMap) / sizeof(karzMap[0]);
    
    bool karzLoaded[16] = {false};
    int karzCount = 0;
    
    // Try loading from RED 808 KARZ first
    File karzDir = LittleFS.open("/RED 808 KARZ", "r");
    if (karzDir && karzDir.isDirectory()) {
        Serial.println("[RED 808 KARZ] Found default kit folder, loading...");
        File kf = karzDir.openNextFile();
        while (kf) {
            if (!kf.isDirectory()) {
                String kfName = kf.name();
                int lastSlash = kfName.lastIndexOf('/');
                if (lastSlash >= 0) kfName = kfName.substring(lastSlash + 1);
                
                if (isValidSampleFile(kfName)) {
                    // Parse: "808 XX ..." -> extract XX
                    String upper = kfName;
                    upper.toUpperCase();
                    
                    for (int m = 0; m < karzMapSize; m++) {
                        if (karzLoaded[karzMap[m].padIndex]) continue;
                        
                        String searchStr = String("808 ") + String(karzMap[m].prefix);
                        if (upper.startsWith(searchStr) || upper.startsWith(String("808") + String(karzMap[m].prefix))) {
                            String fullPath = String("/RED 808 KARZ/") + kfName;
                            Serial.printf("  [KARZ] %s -> %s (pad %d)... ", kfName.c_str(), families[karzMap[m].padIndex], karzMap[m].padIndex);
                            
                            if (sampleManager.loadSample(fullPath.c_str(), karzMap[m].padIndex)) {
                                Serial.printf("✓ (%d bytes)\n", sampleManager.getSampleLength(karzMap[m].padIndex) * 2);
                                karzLoaded[karzMap[m].padIndex] = true;
                                karzCount++;
                            } else {
                                Serial.println("✗ FAILED");
                            }
                            break;
                        }
                    }
                }
            }
            kf.close();
            kf = karzDir.openNextFile();
        }
        karzDir.close();
        Serial.printf("[RED 808 KARZ] Loaded %d/%d instruments from default kit\n", karzCount, 16);
    } else {
        Serial.println("[RED 808 KARZ] Default kit folder not found, using per-family folders");
    }
    
    // Fallback: load missing instruments from individual family folders
    for (int i = 0; i < 16; i++) {
        if (karzLoaded[i]) continue;  // Already loaded from KARZ
        
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
    
    Serial.printf("✓ Samples loaded: %d/16\n", sampleManager.getLoadedSamplesCount());

    // 4. Sequencer Setup
    sequencer.setStepCallback(onStepTrigger);
    
    // Callback para sincronización en tiempo real con la web
    sequencer.setStepChangeCallback([](int newStep) {
        webInterface.broadcastStep(newStep);
    });
    // Callback para cambio de patrón en song mode
    sequencer.setPatternChangeCallback([](int newPattern, int songLength) {
        webInterface.broadcastSongPattern(newPattern, songLength);
    });
    sequencer.setTempo(110); // BPM inicial
    
    // === PATRÓN 0: HIP HOP BOOM BAP (16 tracks) ===
    sequencer.selectPattern(0);
    sequencer.setStep(0, 0, true);   // BD: Kick en 1
    sequencer.setStep(0, 3, true);   // BD: Kick ghost
    sequencer.setStep(0, 10, true);  // BD: Kick sincopado
    sequencer.setStep(1, 4, true);   // SD: Snare en 2
    sequencer.setStep(1, 12, true);  // SD: Snare en 4
    for(int i=0; i<16; i+=2) sequencer.setStep(2, i, true); // CH: patrón cerrado
    sequencer.setStep(3, 6, true);   // OH: para swing
    sequencer.setStep(3, 14, true);  // OH: al final
    sequencer.setStep(5, 4, true);   // CP: Clap doblando snare
    sequencer.setStep(5, 12, true);
    sequencer.setStep(6, 7, true);   // RS: Rimshot fill
    sequencer.setStep(7, 5, true);   // CB: Cowbell groove
    sequencer.setStep(7, 13, true);  // CB: Cowbell extra
    sequencer.setStep(4, 15, true);  // CY: Cymbal crash final
    
    // === PATRÓN 1: TECHNO DETROIT (16 tracks) ===
    sequencer.selectPattern(1);
    for(int i=0; i<16; i+=4) sequencer.setStep(0, i, true); // BD: Four on the floor
    sequencer.setStep(1, 4, true);   // SD: Snare en 2
    sequencer.setStep(1, 12, true);  // SD: Snare en 4
    for(int i=0; i<16; i++) sequencer.setStep(2, i, true);  // CH: 16th hi-hats
    sequencer.setStep(3, 8, true);   // OH: en medio
    sequencer.setStep(5, 4, true);   // CP: Clap capa snare
    sequencer.setStep(5, 8, true);
    sequencer.setStep(5, 12, true);
    sequencer.setStep(6, 7, true);   // RS: Rim accent
    sequencer.setStep(6, 11, true);
    sequencer.setStep(6, 15, true);
    sequencer.setStep(12, 3, true);  // CL: Claves offbeat
    sequencer.setStep(12, 7, true);
    sequencer.setStep(12, 11, true);
    sequencer.setStep(12, 15, true);
    sequencer.setStep(4, 0, true);   // CY: Cymbal intro
    sequencer.setStep(4, 8, true);   // CY: medio
    
    // === PATRÓN 2: DRUM & BASS (16 tracks) ===
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
    sequencer.setStep(5, 4, true);   // CP: Clap layers
    sequencer.setStep(5, 8, true);
    sequencer.setStep(5, 12, true);
    sequencer.setStep(4, 0, true);   // CY: Cymbal intro
    sequencer.setStep(4, 8, true);
    sequencer.setStep(4, 15, true);
    
    // === PATRÓN 3: LATIN PERCUSSION (16 tracks) ===
    sequencer.selectPattern(3);
    sequencer.setStep(0, 0, true);   // BD: Kick
    sequencer.setStep(0, 8, true);
    sequencer.setStep(1, 4, true);   // SD: Snare
    sequencer.setStep(1, 12, true);
    sequencer.setStep(2, 0, true);   // CH: Hi-hat pattern
    sequencer.setStep(2, 2, true);
    sequencer.setStep(2, 4, true);
    sequencer.setStep(2, 6, true);
    sequencer.setStep(2, 8, true);
    sequencer.setStep(2, 10, true);
    sequencer.setStep(2, 12, true);
    sequencer.setStep(2, 14, true);
    sequencer.setStep(7, 1, true);   // CB: Cowbell clave
    sequencer.setStep(7, 5, true);
    sequencer.setStep(7, 9, true);
    sequencer.setStep(7, 13, true);
    sequencer.setStep(12, 0, true);  // CL: Claves pattern
    sequencer.setStep(12, 3, true);
    sequencer.setStep(12, 6, true);
    sequencer.setStep(12, 10, true);
    sequencer.setStep(13, 2, true);  // HC: Hi Conga
    sequencer.setStep(13, 7, true);
    sequencer.setStep(13, 11, true);
    sequencer.setStep(14, 4, true);  // MC: Mid Conga
    sequencer.setStep(14, 9, true);
    sequencer.setStep(14, 14, true);
    sequencer.setStep(15, 0, true);  // LC: Low Conga
    sequencer.setStep(15, 6, true);
    sequencer.setStep(15, 12, true);
    sequencer.setStep(11, 1, true);  // MA: Maracas shuffle
    sequencer.setStep(11, 3, true);
    sequencer.setStep(11, 5, true);
    sequencer.setStep(11, 7, true);
    sequencer.setStep(11, 9, true);
    sequencer.setStep(11, 11, true);
    sequencer.setStep(11, 13, true);
    sequencer.setStep(11, 15, true);
    
    // === PATRÓN 4: CHICAGO HOUSE (16 tracks) ===
    sequencer.selectPattern(4);
    for(int i=0; i<16; i+=4) sequencer.setStep(0, i, true); // BD: Four on floor
    sequencer.setStep(1, 4, true);   // SD: Snare 2 y 4
    sequencer.setStep(1, 12, true);
    for(int i=2; i<16; i+=4) sequencer.setStep(2, i, true); // CH: offbeat
    sequencer.setStep(3, 6, true);   // OH: sincopado
    sequencer.setStep(3, 10, true);
    sequencer.setStep(3, 14, true);
    sequencer.setStep(5, 4, true);   // CP: Clap dobla snare
    sequencer.setStep(5, 8, true);
    sequencer.setStep(5, 12, true);
    sequencer.setStep(6, 1, true);   // RS: Rim house
    sequencer.setStep(6, 5, true);
    sequencer.setStep(6, 9, true);
    sequencer.setStep(6, 13, true);
    sequencer.setStep(4, 0, true);   // CY: Cymbal intro
    sequencer.setStep(4, 8, true);

    sequencer.selectPattern(0); // Empezar con Hip Hop
    // sequencer.start(); // DISABLED: User must press PLAY
    Serial.println("✓ Sequencer: 5 patrones cargados (Hip Hop, Techno, DnB, Breakbeat, House)");
    Serial.println("   Sequencer en PAUSA - presiona PLAY para iniciar");

    // 5. WiFi: STA (red doméstica) con fallback a AP
    Serial.println("\n[STEP 5] Starting WiFi...");
    
    showWiFiLED();
    delay(500);
    
    const char* staSSID = strlen(HOME_WIFI_SSID) > 0 ? HOME_WIFI_SSID : nullptr;
    const char* staPass = strlen(HOME_WIFI_PASS) > 0 ? HOME_WIFI_PASS : nullptr;
    
    if (webInterface.begin(AP_SSID, AP_PASSWORD, staSSID, staPass, HOME_WIFI_TIMEOUT)) {
        if (webInterface.isSTAMode()) {
            Serial.print("WiFi STA OK | IP: ");
        } else {
            Serial.print("WiFi AP OK | IP: ");
        }
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
    if (webInterface.isSTAMode()) {
        Serial.printf("\n=== RED808 READY - STA mode, open http://%s ===\n\n",
                      webInterface.getIP().c_str());
    } else {
        Serial.println("\n=== RED808 READY - Connect to WiFi RED808, open 192.168.4.1 ===\n");
    }
}

void loop() {
    // Stats cada 10 segundos (reducido para menos overhead)
    static uint32_t lastStats = 0;
    if (millis() - lastStats > 10000) {
        Serial.printf("Uptime: %ds | Heap: %d (min:%d) | PSRAM: %d | WS clients: %d\n", 
                      millis()/1000, ESP.getFreeHeap(), ESP.getMinFreeHeap(), 
                      ESP.getFreePsram(), 0);
        
        // Warn if heap is dangerously low
        if (ESP.getFreeHeap() < 30000) {
            Serial.println("⚠️ WARNING: Low heap memory!");
        }
        lastStats = millis();
    }
    vTaskDelay(pdMS_TO_TICKS(100)); // loop() no hace nada crítico
}
