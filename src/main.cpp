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

// Colores por instrumento - Colores AKAI APC mini profesionales en formato GRB (8 tracks)
const uint32_t instrumentColors[8] = {
    0xFFC800,  // 0: BD - Verde lima (RGB: 200,255,0 → GRB: 0xFF,0xC8,0x00)
    0xFFFF00,  // 1: SD - Amarillo (RGB: 255,255,0 → GRB: 0xFF,0xFF,0x00)
    0xE500FF,  // 2: CH - Cian claro (RGB: 0,229,255 → GRB: 0xE5,0x00,0xFF)
    0xFF00CC,  // 3: OH - Turquesa (RGB: 0,255,204 → GRB: 0xFF,0x00,0xCC)
    0xAAFF66,  // 4: CP - Naranja (RGB: 255,170,102 → GRB: 0xAA,0xFF,0x66)
    0x55AAFF,  // 5: RS - Púrpura (RGB: 170,85,255 → GRB: 0x55,0xAA,0xFF)
    0x00FF88,  // 6: CL - Rosa magenta (RGB: 255,0,136 → GRB: 0x00,0xFF,0x88)
    0xAAFFCC   // 7: CY - Rosa claro (RGB: 255,170,204 → GRB: 0xAA,0xFF,0xCC)
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
// - Procesamiento de audio en tiempo real
// - Mezcla de samples y aplicación de filtros
// - Salida I2S al DAC externo
// - NO interrumpir NUNCA para evitar glitches
void audioTask(void *pvParameters) {
    Serial.println("[Task] Audio Task iniciada en Core 1 (Prioridad: 24)");
    while (true) {
        audioEngine.process();
        taskYIELD(); // Ceder solo si hay otra tarea de igual prioridad
    }
}

// CORE 0: System, WiFi, Web Server (Prioridad Media)
// - Secuenciador de patrones MIDI-style
// - WiFi Access Point + WebServer
// - Comandos UDP para control remoto
// - Actualización de LED RGB con fade
// - Todas las operaciones de red y UI
void systemTask(void *pvParameters) {
    Serial.println("[Task] System Task iniciada en Core 0 (Prioridad: 5)");
    Serial.flush();
    
    uint32_t lastLedUpdate = 0;
    
    while (true) {
        sequencer.update();
        webInterface.update(); // WiFi activado
        webInterface.handleUdp(); // Manejar comandos UDP
        midiController.update(); // Procesar eventos MIDI USB
        
        // Fade out del LED después de trigger
        if (ledFading && millis() - lastLedUpdate > 20) {  // Más lento (cada 20ms)
            lastLedUpdate = millis();
            if (ledBrightness > 10) {
                ledBrightness -= 8;  // Fade más suave
                rgbLed.setBrightness(ledBrightness);
                rgbLed.show();
            } else {
                rgbLed.clear();
                rgbLed.show();
                ledFading = false;
                ledBrightness = 0;
            }
        }
        
        vTaskDelay(5); // 200Hz update rate - Balance perfecto entre CPU y responsividad
    }
}

// Callback que el Sequencer llama cada vez que hay un "trigger" en un step
// NO enciende el LED (solo secuenciador)
void onStepTrigger(int track, uint8_t velocity) {
    audioEngine.triggerSampleSequencer(track, velocity);
}

// Función para triggers manuales desde live pads (web interface)
// Esta SÍ enciende el LED RGB
void triggerPadWithLED(int track, uint8_t velocity) {
    Serial.printf("[PAD TRIGGER] Track: %d, Velocity: %d\n", track, velocity);
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

void listDir(const char * dirname, int levels){
    Serial.printf("Listing directory: %s\n", dirname);
    File root = LittleFS.open(dirname);
    if(!root){
        Serial.println("- failed to open directory");
        return;
    }
    if(!root.isDirectory()){
        Serial.println(" - not a directory");
        return;
    }

    File file = root.openNextFile();
    while(file){
        if(file.isDirectory()){
            Serial.printf("  DIR : %s\n", file.name());
            if(levels){
                listDir(file.path(), levels -1);
            }
        } else {
            Serial.printf("  FILE: %s  SIZE: %d\n", file.name(), (int)file.size());
        }
        file = root.openNextFile();
    }
}

void setup() {
    // Inicializar LED RGB PRIMERO - MAGENTA BRILLANTE: BOOT
    rgbLed.begin();
    rgbLed.setBrightness(255);
    showBootLED(); // Magenta: Sistema iniciando
    delay(1000); // Mostrar magenta 1 segundo
    
    Serial.begin(115200);
    
    // Esperar hasta que el monitor serial se conecte (máximo 10 segundos)
    // Durante la espera, el LED sigue en MAGENTA
    int waitCount = 0;
    while (!Serial && waitCount < 20) {
        delay(500);
        waitCount++;
    }
    
    // Mensajes de inicio visibles
    Serial.println("\n\n\n");
    Serial.println("=================================");
    Serial.println("    BOOT START - RED808");
    Serial.println("=================================");
    Serial.println("Serial Monitor Connected!");
    Serial.flush();
    
    // LED sigue mostrando magenta
    Serial.println("[STEP 0] RGB LED initialized (MAGENTA - boot starting)");
    delay(1000); // Mantener magenta visible
    
    Serial.println("\n\n=== ESP32-S3 DRUM MACHINE - DIAGNOSTIC MODE ===");
    Serial.println("[STEP 1] Starting Filesystem...");
    Serial.flush();
    
    // 1. Filesystem
    if (!LittleFS.begin(true)) {
        Serial.println("❌ LittleFS FAIL");
        // LED ROJO para error
        rgbLed.setPixelColor(0, 0xFF0000);
        rgbLed.show();
        while(1) { delay(1000); } // Detener aquí
    }
    Serial.println("✓ LittleFS Mounted");

    // --- EXPLORACIÓN PROFUNDA ---
    Serial.println("\n[STEP 2] Explorando contenido:");
    listDir("/", 2);
    Serial.println("---------------------------------------\n");

    Serial.println("[STEP 3] Starting Audio Engine...");
    // 2. Audio Engine (I2S External DAC)
    if (!audioEngine.begin(I2S_BCK, I2S_WS, I2S_DOUT)) {
        Serial.println("❌ AUDIO ENGINE FAIL");
        // LED ROJO para error
        rgbLed.setPixelColor(0, 0xFF0000);
        rgbLed.show();
        while(1) { delay(1000); } // Detener aquí
    }
    Serial.println("✓ Audio Engine (External DAC) OK");
    


    Serial.println("[STEP 4] Initializing Sample Manager...");
    Serial.flush();
    
    // AMARILLO BRILLANTE: Cargando samples
    showLoadingSamplesLED();
    Serial.println("✓ LED: YELLOW (Loading samples)");
    delay(800); // Tiempo para ver el amarillo antes de empezar a cargar
    
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

    // 5. WiFi AP - Inicialización
    Serial.println("\n[STEP 6] Preparando WiFi...");
    
    // AZUL: WiFi iniciando
    showWiFiLED();
    Serial.println("✓ LED: BLUE (WiFi starting)");
    delay(1200); // Más tiempo para ver el azul
    
    Serial.println("[WiFi] Iniciando Access Point...");
    if (webInterface.begin("RED808", "red808esp32")) {
        Serial.println("✓ WiFi AP iniciado");
        Serial.print("   SSID: RED808\n   IP: ");
        Serial.println(webInterface.getIP());
        
        // VERDE: Servidor web listo
        showWebServerLED();
        Serial.println("✓ LED: GREEN (Web server ready)");
        delay(1200); // Más tiempo para ver el verde
    } else {
        Serial.println("❌ WiFi falló - continuando sin WiFi");
    }

    // --- INICIALIZAR MIDI USB HOST ---
    Serial.println("\n[STEP 6.5] Initializing MIDI USB Host...");
    Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
    if (midiController.begin()) {
        // Conectar MIDI controller con WebInterface
        webInterface.setMIDIController(&midiController);
        
        // Callback para mensajes MIDI
        midiController.setMessageCallback([](const MIDIMessage& msg) {
            // Broadcast a la web
            webInterface.broadcastMIDIMessage(msg);
            
            // Mapear MIDI notes a pads (36-43 = BD,SD,CH,OH,CP,RS,CL,CY)
            if (msg.type == MIDI_NOTE_ON && msg.data2 > 0) {
                int pad = msg.data1 - 36;
                if (pad >= 0 && pad < 8) {
                    triggerPadWithLED(pad, msg.data2);
                }
            }
        });
        
        // Callback para conexión/desconexión de dispositivos
        midiController.setDeviceCallback([](bool connected, const MIDIDeviceInfo& info) {
            webInterface.broadcastMIDIDeviceStatus(connected, info);
        });
        
        Serial.println("✅ MIDI USB Host ready on USB OTG port");
        Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    } else {
        Serial.println("⚠️  MIDI USB Host initialization failed");
        Serial.println("   Continuando sin soporte MIDI USB");
        Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    }

    // --- LANZAMIENTO DE TAREAS OPTIMIZADAS ---
    Serial.println("\n[STEP 7] Creating optimized dual-core tasks...");
    Serial.println("ESP32-S3 Dual Core Configuration:");
    Serial.println("  CORE 1 (240MHz): Audio Engine (Real-time DSP)");
    Serial.println("  CORE 0 (240MHz): WiFi + WebServer + Sequencer");
    
    // CORE 1: Audio Task - Máxima prioridad (24) y stack grande
    xTaskCreatePinnedToCore(
        audioTask,
        "AudioTask",
        12288,  // 12KB stack - Audio processing con headroom
        NULL,
        24,     // Prioridad máxima - NUNCA interrumpir audio
        NULL,
        1       // CORE 1: Dedicado a audio DSP
    );
    
    // CORE 0: System Task - Prioridad media (5) para WiFi/Web
    xTaskCreatePinnedToCore(
        systemTask,
        "SystemTask",
        12288,  // 12KB stack - WiFi + WebServer necesita espacio
        NULL,
        5,      // Prioridad media - No interferir con audio
        NULL,
        0       // CORE 0: WiFi, Web, Sequencer, LED
    );

    Serial.println("\n--- SISTEMA INICIADO ---");
    
    // BLANCO BRILLANTE: Sistema completamente listo
    showReadyLED();
    Serial.println("✓ LED: WHITE (System ready!) - LED will turn off in 2 seconds");
    
    Serial.println("\n🎵 RED808 LISTO - Conecta a WiFi y abre 192.168.4.1 🎵\n");
}

void loop() {
    // DISABLED: Cambio automático de patrones - el usuario controla manualmente
    // static uint32_t lastPatternChange = 0;
    // static int currentPatternIndex = 0;
    // const uint32_t patternDuration = 8800;
    
    // if (millis() - lastPatternChange > patternDuration) {
    //     currentPatternIndex = (currentPatternIndex + 1) % 5;
    //     sequencer.selectPattern(currentPatternIndex);
    //     const char* patternNames[] = {"Hip Hop", "Techno", "Drum & Bass", "Breakbeat", "House"};
    //     Serial.printf("\n>>> Cambiando a Patrón %d: %s <<<\n", currentPatternIndex, patternNames[currentPatternIndex]);
    //     lastPatternChange = millis();
    // }
    
    // Stats cada 5 segundos
    static uint32_t lastStats = 0;
    if (millis() - lastStats > 5000) {
        Serial.printf("Uptime: %d s | Free Heap: %d | PSRAM: %d\n", 
                      millis()/1000, ESP.getFreeHeap(), ESP.getFreePsram());
        lastStats = millis();
    }
    delay(10);
}
