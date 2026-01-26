#include <Arduino.h>
#include <LittleFS.h>
#include <Adafruit_NeoPixel.h>
#include "AudioEngine.h"
#include "SampleManager.h"
#include "KitManager.h"
#include "Sequencer.h"
#include "WebInterface.h"

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
Adafruit_NeoPixel rgbLed(RGB_LED_NUM, RGB_LED_PIN, NEO_GRB + NEO_KHZ800);

// Colores por instrumento - Colores RGB puros e intensos para el LED (16 tracks)
const uint32_t instrumentColors[16] = {
    0xFF0000,  // 0: BD - Rojo puro
    0x0000FF,  // 1: SD - Azul puro
    0xFF8000,  // 2: CH - Naranja intenso
    0x00FF00,  // 3: OH - Verde puro
    0xFF00FF,  // 4: CP - Magenta puro
    0xFF4500,  // 5: CB - Naranja rojizo
    0x00FFFF,  // 6: RS - Cyan puro
    0xFFFFFF,  // 7: CL - Blanco
    0xFFFF00,  // 8: MA - Amarillo puro
    0xFF0080,  // 9: CY - Rosa fucsia
    0x8000FF,  // 10: HT - Violeta
    0x00FF80,  // 11: LT - Verde lima
    0xFF8080,  // 12: MC - Coral
    0x80FFFF,  // 13: MT - Celeste
    0xFF80FF,  // 14: HC - Lavanda
    0x80FF80   // 15: LC - Verde menta
};

// Utility to detect supported audio sample files (.raw or .wav)
static bool isValidSampleFile(const String& filename) {
    return filename.endsWith(".raw") || filename.endsWith(".RAW") ||
           filename.endsWith(".wav") || filename.endsWith(".WAV");
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
// Tarea de Audio: Core 1 (Alta prioridad)
void audioTask(void *pvParameters) {
    Serial.println("[Task] Audio Task iniciada en Core 1");
    while (true) {
        audioEngine.process();
        vTaskDelay(1); 
    }
}

// Tarea del Sistema: Core 0 (Secuenciador, Web, LED)
void systemTask(void *pvParameters) {
    Serial.println("[Task] System Task iniciada en Core 0");
    Serial.flush();
    
    uint32_t lastLedUpdate = 0;
    
    while (true) {
        sequencer.update();
        webInterface.update(); // WiFi activado
        webInterface.handleUdp(); // Manejar comandos UDP
        
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
        
        vTaskDelay(10); // 100Hz update rate
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
    Serial.begin(115200);
    
    // Esperar hasta que el monitor serial se conecte (máximo 10 segundos)
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
    delay(1000);
    
    // Inicializar LED RGB
    Serial.println("[STEP 0] Initializing RGB LED...");
    rgbLed.begin();
    rgbLed.setBrightness(255);
    rgbLed.setPixelColor(0, 0xFF0000); // Rojo al inicio
    rgbLed.show();
    delay(500);
    rgbLed.clear();
    rgbLed.show();
    Serial.println("✓ RGB LED OK");
    
    Serial.println("\n\n=== ESP32-S3 DRUM MACHINE - DIAGNOSTIC MODE ===");
    Serial.println("[STEP 1] Starting Filesystem...");
    Serial.flush();
    
    // 1. Filesystem
    if (!LittleFS.begin(true)) {
        Serial.println("❌ LittleFS FAIL");
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
        while(1) { delay(1000); } // Detener aquí
    }
    Serial.println("✓ Audio Engine (External DAC) OK");
    


    Serial.println("[STEP 4] Initializing Sample Manager...");
    Serial.flush();
    
    // 3. Sample Manager - Cargar todos los samples por familia
    sampleManager.begin();
    
    Serial.println("[STEP 5] Loading all samples from families...");
    const char* families[] = {"BD", "SD", "CH", "OH", "CP", "CB", "RS", "CL", "MA", "CY", "HT", "LT", "MC", "MT", "HC", "LC"};
    
    for (int i = 0; i < 16; i++) {
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
    sequencer.setStep(4, 4, true);   // CP: Clap doblando snare
    sequencer.setStep(4, 12, true);
    sequencer.setStep(5, 11, true);  // CB: Cowbell accent
    sequencer.setStep(6, 7, true);   // RS: Rimshot fill
    sequencer.setStep(7, 5, true);   // CL: Claves groove
    sequencer.setStep(7, 13, true);  // CL: Claves extra
    sequencer.setStep(8, 2, true);   // MA: Maracas shake
    sequencer.setStep(8, 6, true);
    sequencer.setStep(8, 10, true);
    sequencer.setStep(8, 14, true);  // MA: Maracas constante
    sequencer.setStep(9, 15, true);  // CY: Cymbal crash final
    sequencer.setStep(10, 9, true);  // HT: Hi tom fill
    sequencer.setStep(10, 13, true);
    sequencer.setStep(11, 7, true);  // LT: Low tom
    sequencer.setStep(11, 11, true);
    sequencer.setStep(12, 1, true);  // MC: Mid conga texture
    sequencer.setStep(12, 5, true);
    sequencer.setStep(12, 9, true);
    sequencer.setStep(12, 13, true);
    sequencer.setStep(13, 3, true);  // MT: Mid tom accent
    sequencer.setStep(13, 11, true);
    sequencer.setStep(14, 6, true);  // HC: Hi conga
    sequencer.setStep(14, 10, true);
    sequencer.setStep(15, 3, true);  // LC: Low conga groove
    sequencer.setStep(15, 7, true);
    sequencer.setStep(15, 15, true);
    
    // === PATRÓN 1: TECHNO DETROIT (16 tracks) ===
    sequencer.selectPattern(1);
    for(int i=0; i<16; i+=4) sequencer.setStep(0, i, true); // BD: Four on the floor
    sequencer.setStep(1, 4, true);   // SD: Snare en 2
    sequencer.setStep(1, 12, true);  // SD: Snare en 4
    for(int i=0; i<16; i++) sequencer.setStep(2, i, true);  // CH: 16th hi-hats
    sequencer.setStep(3, 8, true);   // OH: en medio
    sequencer.setStep(4, 4, true);   // CP: Clap capa snare
    sequencer.setStep(4, 8, true);
    sequencer.setStep(4, 12, true);
    sequencer.setStep(5, 2, true);   // CB: Cowbell syncopado
    sequencer.setStep(5, 6, true);
    sequencer.setStep(5, 10, true);
    sequencer.setStep(5, 14, true);
    sequencer.setStep(6, 7, true);   // RS: Rim accent
    sequencer.setStep(6, 11, true);
    sequencer.setStep(6, 15, true);
    sequencer.setStep(7, 3, true);   // CL: Claves offbeat
    sequencer.setStep(7, 7, true);
    sequencer.setStep(7, 11, true);
    sequencer.setStep(7, 15, true);
    for(int i=1; i<16; i+=2) sequencer.setStep(8, i, true); // MA: Maracas 8th
    sequencer.setStep(9, 0, true);   // CY: Cymbal intro
    sequencer.setStep(9, 8, true);   // CY: medio
    sequencer.setStep(10, 5, true);  // HT: Hi tom punch
    sequencer.setStep(10, 9, true);
    sequencer.setStep(10, 13, true);
    sequencer.setStep(11, 3, true);  // LT: Low tom drop
    sequencer.setStep(11, 7, true);
    sequencer.setStep(11, 15, true);
    for(int i=0; i<16; i+=4) sequencer.setStep(12, i, true); // MC: Mid conga steady
    sequencer.setStep(13, 2, true);  // MT: Mid tom
    sequencer.setStep(13, 6, true);
    sequencer.setStep(13, 10, true);
    sequencer.setStep(13, 14, true);
    for(int i=1; i<16; i+=4) sequencer.setStep(14, i, true); // HC: Hi conga texture
    sequencer.setStep(15, 7, true);  // LC: Low conga
    sequencer.setStep(15, 11, true);
    sequencer.setStep(15, 15, true);
    
    // === PATRÓN 2: DRUM & BASS AMEN (16 tracks) ===
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
    sequencer.setStep(5, 5, true);   // CB: Cowbell DnB
    sequencer.setStep(5, 9, true);
    sequencer.setStep(5, 13, true);
    sequencer.setStep(6, 3, true);   // RS: Rim pattern
    sequencer.setStep(6, 6, true);
    sequencer.setStep(6, 8, true);
    sequencer.setStep(6, 11, true);
    for(int i=0; i<16; i+=3) sequencer.setStep(7, i, true); // CL: Claves fast triplets
    for(int i=0; i<16; i+=2) sequencer.setStep(8, i, true); // MA: Shake constant
    sequencer.setStep(9, 0, true);   // CY: Cymbal intro
    sequencer.setStep(9, 8, true);   // CY: medio
    sequencer.setStep(9, 15, true);  // CY: final
    for(int i=1; i<16; i+=3) sequencer.setStep(10, i, true); // HT: Hi tom roll triplet
    sequencer.setStep(11, 1, true);  // LT: Low tom punch
    sequencer.setStep(11, 5, true);
    sequencer.setStep(11, 9, true);
    sequencer.setStep(11, 13, true);
    for(int i=2; i<16; i+=4) sequencer.setStep(12, i, true); // MC: Conga pattern 16th
    sequencer.setStep(13, 3, true);  // MT: Mid tom breaks
    sequencer.setStep(13, 7, true);
    sequencer.setStep(13, 11, true);
    sequencer.setStep(13, 14, true);
    for(int i=1; i<16; i+=3) sequencer.setStep(14, i, true); // HC: Hi conga fast triplet
    for(int i=0; i<16; i+=4) sequencer.setStep(15, i, true); // LC: Low conga groove steady
    
    // === PATRÓN 3: BREAKBEAT SHUFFLE (16 tracks) ===
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
    sequencer.setStep(5, 3, true);   // CB: Cowbell groove
    sequencer.setStep(5, 7, true);
    sequencer.setStep(5, 11, true);
    sequencer.setStep(5, 15, true);
    sequencer.setStep(6, 1, true);   // RS: Rim shuffle
    sequencer.setStep(6, 3, true);
    sequencer.setStep(6, 9, true);
    for(int i=0; i<16; i+=4) sequencer.setStep(7, i, true); // CL: Claves break steady
    sequencer.setStep(8, 1, true);   // MA: Maracas swing
    sequencer.setStep(8, 5, true);
    sequencer.setStep(8, 9, true);
    sequencer.setStep(8, 13, true);
    sequencer.setStep(9, 0, true);   // CY: Cymbal crash
    sequencer.setStep(9, 12, true);
    sequencer.setStep(10, 2, true);  // HT: Hi tom fills
    sequencer.setStep(10, 6, true);
    sequencer.setStep(10, 10, true);
    sequencer.setStep(10, 14, true);
    sequencer.setStep(11, 4, true);  // LT: Low tom drop
    sequencer.setStep(11, 8, true);
    sequencer.setStep(11, 14, true);
    for(int i=1; i<16; i+=3) sequencer.setStep(12, i, true); // MC: Mid conga layer triplet
    sequencer.setStep(13, 2, true);  // MT: Mid tom accent
    sequencer.setStep(13, 6, true);
    sequencer.setStep(13, 11, true);
    sequencer.setStep(14, 3, true);  // HC: Hi conga shuffle
    sequencer.setStep(14, 5, true);
    sequencer.setStep(14, 9, true);
    sequencer.setStep(14, 15, true);
    sequencer.setStep(15, 7, true);  // LC: Low conga punch
    sequencer.setStep(15, 10, true);
    sequencer.setStep(15, 13, true);
    
    // === PATRÓN 4: CHICAGO HOUSE (16 tracks) ===
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
    sequencer.setStep(5, 3, true);   // CB: Cowbell latino
    sequencer.setStep(5, 7, true);
    sequencer.setStep(5, 11, true);
    sequencer.setStep(5, 15, true);
    sequencer.setStep(6, 1, true);   // RS: Rim house
    sequencer.setStep(6, 5, true);
    sequencer.setStep(6, 9, true);
    sequencer.setStep(6, 13, true);
    for(int i=0; i<16; i+=4) sequencer.setStep(7, i, true); // CL: Claves steady
    sequencer.setStep(8, 2, true);   // MA: Maracas house 16th
    sequencer.setStep(8, 6, true);
    sequencer.setStep(8, 10, true);
    sequencer.setStep(8, 14, true);
    sequencer.setStep(9, 0, true);   // CY: Cymbal intro
    sequencer.setStep(9, 8, true);
    sequencer.setStep(10, 5, true);  // HT: Hi tom accent
    sequencer.setStep(10, 9, true);
    sequencer.setStep(10, 13, true);
    sequencer.setStep(11, 3, true);  // LT: Low tom punch
    sequencer.setStep(11, 7, true);
    sequencer.setStep(11, 11, true);
    sequencer.setStep(11, 15, true);
    for(int i=0; i<16; i+=2) sequencer.setStep(12, i, true); // MC: Mid conga latin 8th
    sequencer.setStep(13, 6, true);  // MT: Mid tom
    
    sequencer.setStep(13, 10, true);
    sequencer.setStep(13, 15, true);
    sequencer.setStep(14, 1, true);  // HC: Hi conga pattern
    sequencer.setStep(14, 5, true);
    sequencer.setStep(14, 9, true);
    sequencer.setStep(14, 13, true);
    for(int i=1; i<16; i+=4) sequencer.setStep(15, i, true); // LC: Low conga groove steady

    sequencer.selectPattern(0); // Empezar con Hip Hop
    sequencer.start();
    Serial.println("✓ Sequencer: 5 patrones cargados (Hip Hop, Techno, DnB, Breakbeat, House)");
    Serial.println("   Los patrones cambiarán automáticamente cada 4 compases");

    // 5. WiFi AP - Inicialización
    Serial.println("\n[STEP 6] Preparando WiFi...");
    delay(1000);
    
    Serial.println("[WiFi] Iniciando Access Point...");
    if (webInterface.begin("RED808", "red808esp32")) {
        Serial.println("✓ WiFi AP iniciado");
        Serial.print("   SSID: RED808\n   IP: ");
        Serial.println(webInterface.getIP());
        delay(2000); // Espera para estabilización
    } else {
        Serial.println("❌ WiFi falló - continuando sin WiFi");
    }

    // --- LANZAMIENTO DE TAREAS ---
    Serial.println("\n[STEP 7] Creating tasks...");
    Serial.println("\n[STEP 7] Starting FreeRTOS tasks...");
    
    xTaskCreatePinnedToCore(
        audioTask,
        "AudioTask",
        10240, // Aumentado de 8192
        NULL,
        24,
        NULL,
        1
    );
    
    xTaskCreatePinnedToCore(
        systemTask,
        "SystemTask",
        10240, // Aumentado de 8192
        NULL,
        5,
        NULL,
        0
    );

    Serial.println("\n--- SISTEMA INICIADO ---");
}

void loop() {
    // Cambio automático de patrones cada 4 compases (64 steps)
    static uint32_t lastPatternChange = 0;
    static int currentPatternIndex = 0;
    const uint32_t patternDuration = 8800; // 4 compases a 110 BPM (~8.8 segundos)
    
    if (millis() - lastPatternChange > patternDuration) {
        currentPatternIndex = (currentPatternIndex + 1) % 5; // Ciclar entre 0-4
        sequencer.selectPattern(currentPatternIndex);
        
        const char* patternNames[] = {"Hip Hop", "Techno", "Drum & Bass", "Breakbeat", "House"};
        Serial.printf("\n>>> Cambiando a Patrón %d: %s <<<\n", currentPatternIndex, patternNames[currentPatternIndex]);
        
        lastPatternChange = millis();
    }
    
    // Stats cada 5 segundos
    static uint32_t lastStats = 0;
    if (millis() - lastStats > 5000) {
        Serial.printf("Uptime: %d s | Free Heap: %d | PSRAM: %d | Patrón: %d\n", 
                      millis()/1000, ESP.getFreeHeap(), ESP.getFreePsram(), currentPatternIndex);
        lastStats = millis();
    }
    delay(10);
}
