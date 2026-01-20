#include <Arduino.h>
#include <LittleFS.h>
#include <Adafruit_NeoPixel.h>
#include "AudioEngine.h"
#include "SampleManager.h"
#include "KitManager.h"
#include "Sequencer.h"
#include "WebInterface.h"

// --- CONFIGURACIÓN DE HARDWARE ---
#define I2S_BCK   42
#define I2S_WS    41
#define I2S_DOUT  2

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

// Colores por instrumento - Colores RGB puros e intensos para el LED
const uint32_t instrumentColors[8] = {
    0xFF0000,  // 0: BD - Rojo puro
    0x0000FF,  // 1: SD - Azul puro
    0xFF8000,  // 2: CH - Naranja intenso
    0x00FF00,  // 3: OH - Verde puro
    0xFF00FF,  // 4: CP - Magenta puro
    0xFF4500,  // 5: CB - Naranja rojizo
    0x00FFFF,  // 6: TOM - Cyan puro
    0xFFFFFF   // 7: PERC - Blanco
};

// Variables para control del LED RGB fade
volatile uint8_t ledBrightness = 0;
volatile bool ledFading = false;

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
        webInterface.update();
        
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
    audioEngine.triggerSample(track, velocity);
}

// Función para triggers manuales desde live pads (web interface)
// Esta SÍ enciende el LED RGB
void triggerPadWithLED(int track, uint8_t velocity) {
    Serial.printf("[PAD TRIGGER] Track: %d, Velocity: %d\n", track, velocity);
    audioEngine.triggerSample(track, velocity);
    
    // Iluminar LED RGB con color del instrumento
    if (track >= 0 && track < 8) {
        uint32_t color = instrumentColors[track];
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
    
    // Esperar más tiempo para que el monitor serial se conecte
    for (int i = 0; i < 5; i++) {
        delay(500);
        Serial.print(".");
    }
    
    Serial.println("\n\n");
    Serial.println("=================================");
    Serial.println("    BOOT START - RED808");
    Serial.println("=================================");
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
    // 2. Audio Engine (I2S)
    if (!audioEngine.begin(I2S_BCK, I2S_WS, I2S_DOUT)) {
        Serial.println("❌ AUDIO ENGINE FAIL");
        while(1) { delay(1000); } // Detener aquí
    }
    Serial.println("✓ Audio Engine OK");

    Serial.println("[STEP 4] Initializing Sample Manager...");
    Serial.flush();
    
    // 3. Sample Manager & Kit Manager
    sampleManager.begin();
    
    // KitManager::begin() ya escanea kits y carga el primero (Kit 0)
    // Pero vamos a darle un poco de tiempo entre el mounting y el acceso
    delay(500);
    if (kitManager.begin()) {
        Serial.println("✓ Kit Manager listo y primer kit cargado.");
    } else {
        Serial.println("⚠️ No se encontraron kits o fallo al cargar.");
    }

    // 4. Sequencer Setup
    sequencer.setStepCallback(onStepTrigger);
    
    // Callback para sincronización en tiempo real con la web
    sequencer.setStepChangeCallback([](int newStep) {
        webInterface.broadcastStep(newStep);
    });
    
    sequencer.setTempo(110); // BPM inicial
    
    // === PATRÓN 0: HIP HOP BOOM BAP ===
    sequencer.selectPattern(0);
    sequencer.setStep(0, 0, true);   // Kick en 1
    sequencer.setStep(0, 3, true);   // Kick ghost
    sequencer.setStep(0, 10, true);  // Kick sincopado
    sequencer.setStep(1, 4, true);   // Snare en 2
    sequencer.setStep(1, 12, true);  // Snare en 4
    for(int i=0; i<16; i+=2) sequencer.setStep(2, i, true); // CH patrón cerrado
    sequencer.setStep(3, 6, true);   // OH para swing
    sequencer.setStep(3, 14, true);  // OH al final
    sequencer.setStep(4, 4, true);   // Clap doblando snare
    sequencer.setStep(4, 12, true);
    sequencer.setStep(5, 11, true);  // Cowbell accent
    sequencer.setStep(6, 7, true);   // Tom bajo fill
    sequencer.setStep(7, 15, true);  // Tom medio fin de frase
    
    // === PATRÓN 1: TECHNO DETROIT ===
    sequencer.selectPattern(1);
    for(int i=0; i<16; i+=4) sequencer.setStep(0, i, true); // Four on the floor
    sequencer.setStep(1, 4, true);   // Snare en 2
    sequencer.setStep(1, 12, true);  // Snare en 4
    for(int i=0; i<16; i++) sequencer.setStep(2, i, true);  // 16th hi-hats
    sequencer.setStep(3, 8, true);   // OH en medio
    sequencer.setStep(4, 8, true);   // Clap capa con snare
    sequencer.setStep(5, 2, true);   // Cowbell syncopado
    sequencer.setStep(5, 6, true);
    sequencer.setStep(5, 10, true);
    sequencer.setStep(5, 14, true);
    sequencer.setStep(7, 11, true);  // RS accent
    
    // === PATRÓN 2: DRUM & BASS AMEN ===
    sequencer.selectPattern(2);
    sequencer.setStep(0, 0, true);   // Kick doble
    sequencer.setStep(0, 2, true);
    sequencer.setStep(0, 10, true);  // Kick sincopado
    sequencer.setStep(1, 4, true);   // Snare break
    sequencer.setStep(1, 7, true);   // Snare ghost
    sequencer.setStep(1, 10, true);
    sequencer.setStep(1, 12, true);
    for(int i=0; i<16; i++) sequencer.setStep(2, i, true);  // CH constante
    sequencer.setStep(3, 6, true);   // OH para textura
    sequencer.setStep(3, 14, true);
    sequencer.setStep(6, 3, true);
    sequencer.setStep(6, 8, true);
    sequencer.setStep(7, 11, true);
    sequencer.setStep(7, 15, true);
    
    // === PATRÓN 3: BREAKBEAT SHUFFLE ===
    sequencer.selectPattern(3);
    sequencer.setStep(0, 0, true);   // Kick principal
    sequencer.setStep(0, 5, true);   // Kick offbeat
    sequencer.setStep(0, 10, true);
    sequencer.setStep(1, 4, true);   // Snare backbeat
    sequencer.setStep(1, 12, true);
    sequencer.setStep(1, 13, true);  // Snare flam
    for(int i=0; i<16; i+=3) sequencer.setStep(2, i, true); // CH shuffle
    sequencer.setStep(3, 6, true);   // OH acentos
    sequencer.setStep(3, 14, true);
    sequencer.setStep(4, 9, true);   // Clap offbeat
    sequencer.setStep(5, 7, true);   // Cowbell groove
    sequencer.setStep(6, 3, true);
    sequencer.setStep(7, 11, true);
    sequencer.setStep(7, 15, true);
    
    // === PATRÓN 4: CHICAGO HOUSE ===
    sequencer.selectPattern(4);
    for(int i=0; i<16; i+=4) sequencer.setStep(0, i, true); // Four on floor
    sequencer.setStep(1, 4, true);   // Snare 2 y 4
    sequencer.setStep(1, 12, true);
    for(int i=2; i<16; i+=4) sequencer.setStep(2, i, true); // CH offbeat
    sequencer.setStep(3, 6, true);   // OH sincopado
    sequencer.setStep(3, 10, true);
    sequencer.setStep(3, 14, true);
    sequencer.setStep(4, 4, true);   // Clap dobla snare
    sequencer.setStep(4, 12, true);
    sequencer.setStep(5, 3, true);
    sequencer.setStep(5, 11, true);
    sequencer.setStep(7, 15, true);
    
    sequencer.selectPattern(0); // Empezar con Hip Hop
    sequencer.start();
    Serial.println("✓ Sequencer: 5 patrones cargados (Hip Hop, Techno, DnB, Breakbeat, House)");
    Serial.println("   Los patrones cambiarán automáticamente cada 4 compases");

    // 5. Web Interface (WiFi AP)
    if (webInterface.begin("RED808", "red808esp32")) {
        Serial.println("✓ RED808 WiFi AP activo");
        Serial.print("   Conecta tu dispositivo a la red: RED808");
        Serial.print("\n   Abre el navegador en: http://");
        Serial.println(webInterface.getIP());
    }

    // --- LANZAMIENTO DE TAREAS ---
    
    // Audio en Core 1 - Prioridad máxima (24)
    xTaskCreatePinnedToCore(
        audioTask,
        "AudioTask",
        10240, // Aumentado de 8192
        NULL,
        24,
        NULL,
        1
    );

    // Sistema (Seq + UI) en Core 0 - Prioridad media (5)
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
