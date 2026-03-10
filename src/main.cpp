#include <Arduino.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <Adafruit_NeoPixel.h>
#include <esp_task_wdt.h>
#include "SPIMaster.h"
#include "SampleManager.h"
#include "Sequencer.h"
#include "WebInterface.h"
#include "MIDIController.h"
#include "LFOEngine.h"
#if ENABLE_PHYSICAL_BUTTONS
#include "PhysControlButtons.h"
#endif

#ifndef ENABLE_PHYSICAL_BUTTONS
#define ENABLE_PHYSICAL_BUTTONS 1
#endif

// LED RGB integrado ESP32-S3
#define RGB_LED_PIN  48
#define RGB_LED_NUM  1



// --- WiFi: Red Doméstica (modo STA) ---
// Pon aquí tu SSID y contraseña WiFi de casa.
// Si se deja vacío (""), usará solo modo AP (red propia RED808).
#define HOME_WIFI_SSID     ""         // vacío = solo modo AP (RED808)
#define HOME_WIFI_PASS     ""   

#define HOME_WIFI_TIMEOUT  12000      // ms para intentar conectar (12s)

// AP fallback (siempre disponible si STA falla)
#define AP_SSID     "RED808"
#define AP_PASSWORD "red808esp32"

// Daisy-first workflow: no precarga de samples locales en boot de ESP32.
// Los samples se gestionan desde SD en Daisy vía comandos SD_*.
#define BOOT_PRELOAD_LOCAL_SAMPLES false

// --- OBJETOS GLOBALES ---
// NOTE: Sequencer's large pattern arrays (~229 KB) are allocated from PSRAM
// via ps_calloc() inside the Sequencer constructor — see Sequencer.cpp.
SPIMaster spiMaster;
SampleManager sampleManager;
Sequencer sequencer;
LFOEngine lfoEngine;
WebInterface webInterface;
MIDIController midiController;
Adafruit_NeoPixel rgbLed(RGB_LED_NUM, RGB_LED_PIN, NEO_GRB + NEO_KHZ800);

#if ENABLE_PHYSICAL_BUTTONS
// ── Botones físicos con LED RGB ──────────────────────────────────
PhysControlButtons ctrlButtons;
bool gMultiviewActive = false;   // estado actual del panel multiview
// Estado de FX master para toggles desde botones físicos
bool gDelayActive    = false;
bool gReverbActive   = false;
bool gChorusActive   = false;
bool gPhaserActive   = false;
bool gFlangerActive  = false;
bool gCompActive     = false;
bool gTremoloActive  = false;
bool gLimiterActive  = true;
bool gDistActive     = false;
#endif
// Track synth engine map for sequencer tracks:
// -1 = sample, 0 = 808, 1 = 909, 2 = 505, 3 = 303
int8_t gTrackSynthEngine[16] = {
    -1, -1, -1, -1,
    -1, -1, -1, -1,
    -1, -1, -1, -1,
    -1, -1, -1, -1
};

void setTrackSynthEngine(int track, int8_t engine) {
    if (track < 0 || track >= 16) return;
    if (engine < -1 || engine > 6) return;  // 0=808,1=909,2=505,3=303,4=WT,5=SH101,6=FM2Op
    gTrackSynthEngine[track] = engine;
}

void setAllTrackSynthEngines(int8_t engine) {
    if (engine < -1 || engine > 6) return;  // 0-6 valid engine IDs
    for (int i = 0; i < 16; i++) {
        gTrackSynthEngine[i] = engine;
    }
}

int8_t getTrackSynthEngine(int track) {
    if (track < 0 || track >= 16) return -1;
    return gTrackSynthEngine[track];
}

static const uint8_t PAD_303_NOTES[16] = {
    48, 50, 52, 53, 55, 57, 59, 60,
    62, 64, 65, 67, 69, 71, 72, 74
};

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
}

// --- TASKS (CORE PINNING) ---

// ── Triggers eliminados: el secuenciador ahora corre en Daisy Seed ──
// Daisy dispara los samples sample-accurately internamente (DsqFireStep).
// ESP32 solo sube el patrón una vez y envía CMD_DSQ_CONTROL play/stop.

// Helper: convierte un patrón del Sequencer ESP32 → DsqStepPkt y lo sube a Daisy
void dsqUploadPattern(int pattern) {
    const int stepCount = sequencer.getPatternLength();  // longitud global
    const int clampedLen = (stepCount >= 64) ? 64 : (stepCount >= 32) ? 32 : 16;
    DsqStepPkt pkt[DSQ_MAX_STEPS];
    for (int trk = 0; trk < DSQ_TRACKS; trk++) {
        for (int s = 0; s < clampedLen; s++) {
            pkt[s].active      = sequencer.getStep(pattern, trk, s) ? 1 : 0;
            pkt[s].velocity    = sequencer.getStepVelocity(pattern, trk, s);
            pkt[s].noteLenDiv  = sequencer.getStepNoteLen(pattern, trk, s);
            pkt[s].probability = sequencer.getStepProbability(pattern, trk, s);
        }
        spiMaster.dsqSetLength((uint8_t)clampedLen);
        spiMaster.dsqUploadTrack((uint8_t)pattern, (uint8_t)trk, pkt, (uint8_t)clampedLen);
        // Param locks
        for (int s = 0; s < clampedLen; s++) {
            uint16_t ch = sequencer.getStepCutoffLock(pattern, trk, s);
            bool ce = (ch != 0 && ch != 1000);
            uint8_t rv = sequencer.getStepReverbSendLock(pattern, trk, s);
            bool re = (rv != 0);
            uint8_t vl = sequencer.getStepVolumeLock(pattern, trk, s);
            bool ve = (vl != 0);
            if (ce || re || ve) {
                spiMaster.dsqSetParamLock(
                    (uint8_t)pattern, (uint8_t)trk, (uint8_t)s,
                    ce, ch, re, rv, ve, vl
                );
            }
        }
    }
}

// CORE 1: Sequencer UI + SPI Master
// El secuenciador corre en Daisy Seed; aquí solo actualizamos estado UI y LFO.
void spiAudioTask(void *pvParameters) {
    esp_task_wdt_add(NULL);  // subscribe to TWDT

    // Subir todos los patrones a Daisy Seed al arrancar
    // (el SPI ya está listo porque spiMaster.begin() fue llamado en setup)
    for (int pat = 0; pat < DSQ_PATTERNS; pat++) {
        dsqUploadPattern(pat);
        vTaskDelay(pdMS_TO_TICKS(2));   // pequeño gap entre patrones
    }
    // Seleccionar patrón activo en Daisy
    spiMaster.dsqSelectPattern((uint8_t)sequencer.getCurrentPattern());
    // Sync tempo inicial a secuenciador Daisy
    spiMaster.setTempo((float)sequencer.getTempo());

    while (true) {
        sequencer.update();   // Mantiene internos del secuenciador (beat UI, song mode)
        lfoEngine.update(sequencer.getTempo(), spiMaster);
        spiMaster.process();
        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

// CORE 0: WiFi, Web Server, MIDI, LED (Prioridad Media)
void systemTask(void *pvParameters) {
    esp_task_wdt_add(NULL);  // subscribe to TWDT

    uint32_t lastLedUpdate = 0;
#if ENABLE_PHYSICAL_BUTTONS
    uint32_t lastBtnUpdate  = 0;
#endif
    
    while (true) {
        midiController.update();
        webInterface.update();
        webInterface.handleUdp();
#if ENABLE_PHYSICAL_BUTTONS
        // Botones físicos — Core 0 (mismo que rgbLed para evitar conflicto RMT)
        if (millis() - lastBtnUpdate >= 5) {
            lastBtnUpdate = millis();
            ctrlButtons.update();
        }
#endif
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
        esp_task_wdt_reset();
    }
}

// onStepTrigger eliminado: el secuenciador corre en Daisy Seed.
// Los triggers sample se disparan en DsqFireStep() dentro del AudioCallback.
// Los synth triggers (808/909/505 sintéticos) siguen siendo controlados por la UI.

// Función para triggers manuales desde live pads (web interface)
// Esta SÍ enciende el LED RGB
void triggerPadWithLED(int track, uint8_t velocity) {
    int8_t engine = getTrackSynthEngine(track);
    if (track >= 0 && track < 16 && engine >= 0 && engine <= 6) {
        uint8_t liveVol = spiMaster.getLiveVolume();
        float scaled = (velocity / 127.0f) * (liveVol / 100.0f);
        uint8_t synthVelocity = (uint8_t)constrain((int)roundf(scaled * 127.0f), 1, 127);
        if (engine == 3) {
            uint8_t midiNote = PAD_303_NOTES[track];
            spiMaster.synth303NoteOn(midiNote, false, false);
        } else {
            spiMaster.synthTrigger((uint8_t)engine, (uint8_t)track, synthVelocity);
        }
    } else {
        spiMaster.triggerSampleLive(track, velocity);
    }
    lfoEngine.onPadTrigger(track);  // LFO retrigger
    
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

static void applyProfessionalMixBaseline() {
    // Global post-master defaults: clean and controlled
    spiMaster.setMasterVolume(100);
    spiMaster.setSequencerVolume(100);
    spiMaster.setLiveVolume(100);
    spiMaster.setLivePitchShift(1.0f);

    spiMaster.setFilterType(FILTER_NONE);
    spiMaster.setDelayActive(false);
    spiMaster.setPhaserActive(false);
    spiMaster.setFlangerActive(false);
    spiMaster.setCompressorActive(false);
    spiMaster.setReverbActive(false);
    spiMaster.setChorusActive(false);
    spiMaster.setTremoloActive(false);
    spiMaster.setWaveFolderGain(1.0f);
    spiMaster.setLimiterActive(true);

    for (int track = 0; track < 16; track++) {
        spiMaster.clearTrackFilter(track);
        spiMaster.clearTrackFX(track);
        spiMaster.clearTrackLiveFX(track);
        spiMaster.setTrackReverbSend(track, 0);
        spiMaster.setTrackDelaySend(track, 0);
        spiMaster.setTrackChorusSend(track, 0);
        spiMaster.setTrackPan(track, 0);
        spiMaster.setTrackMute(track, false);
        spiMaster.setTrackSolo(track, false);
        spiMaster.setTrackEq(track, 0, 0, 0);
        spiMaster.setTrackVolume(track, 100);
        sequencer.setTrackVolume(track, 100);
    }

    for (int pad = 0; pad < 24; pad++) {
        spiMaster.clearPadFilter(pad);
        spiMaster.clearPadFX(pad);
    }
}

void setup() {
    Serial.begin(115200);
    rgbLed.begin();
    rgbLed.setBrightness(255);
    showBootLED();
    delay(500);

    // ── Reset reason logging ──
    esp_reset_reason_t reason = esp_reset_reason();
    Serial.printf("[BOOT] Reset reason: %d\n", (int)reason);

    // 1. Filesystem
    if (!LittleFS.begin(true)) {
        rgbLed.setPixelColor(0, 0xFF0000);
        rgbLed.show();
        delay(3000);
        ESP.restart();
    }

    // NVS ya esta disponible en setup: cargar mapeos MIDI persistidos aqui.
    midiController.loadMappings();

    // 2. SPI Master — connects to STM32 for audio DSP
    if (!spiMaster.begin()) {
        rgbLed.setPixelColor(0, 0xFF0000);
        rgbLed.show();
        delay(3000);
        ESP.restart();
    }
    
    
    showLoadingSamplesLED();
    delay(300);

    // 3. Sample Manager (modo Daisy-first: sin precarga local en boot)
    sampleManager.begin();
    bool shouldPreloadLocalSamples = BOOT_PRELOAD_LOCAL_SAMPLES;
    if (shouldPreloadLocalSamples) {
        SdStatusResponse sdStatus = {};
        if (spiMaster.sdGetStatus(sdStatus) && sdStatus.present) {
            int loadedMainPads = 0;
            for (int i = 0; i < 16; i++) {
                if (sdStatus.samplesLoaded & (1UL << i)) loadedMainPads++;
            }

            if (loadedMainPads >= 16) {
                shouldPreloadLocalSamples = false;
            } else {
            }
        } else {
        }
    } else {
        SdStatusResponse sdStatus = {};
        if (spiMaster.sdGetStatus(sdStatus) && sdStatus.present) {
            int loadedMainPads = 0;
            for (int i = 0; i < 16; i++) {
                if (sdStatus.samplesLoaded & (1UL << i)) loadedMainPads++;
            }

            if (loadedMainPads == 0) {
                const char* defaultKit = "RED 808 KARZ";
                bool sent = spiMaster.sdLoadKit(defaultKit, 0, 16);
            } else {
            }
        } else {
        }
    }

    if (shouldPreloadLocalSamples) {
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

                                if (sampleManager.loadSample(fullPath.c_str(), karzMap[m].padIndex)) {
                                    karzLoaded[karzMap[m].padIndex] = true;
                                    karzCount++;
                                } else {
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
        } else {
        }

        // Fallback: load missing instruments from individual family folders
        for (int i = 0; i < 16; i++) {
            if (karzLoaded[i]) continue;  // Already loaded from KARZ

            String path = String("/") + String(families[i]);

            File dir = LittleFS.open(path, "r");

            if (dir && dir.isDirectory()) {
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

                            if (sampleManager.loadSample(fullPath.c_str(), i)) {
                                loaded = true;
                            } else {
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
                }
            } else {
            }
        }

    } else {
    }

    // 4. Sequencer Setup
    // Trigger + automation callbacks eliminados → secuenciador interno en Daisy.
    // Solo mantenemos step change para indicador de beat en la web UI.

    // Callback para sincronización en tiempo real con la web
    sequencer.setStepChangeCallback([](int newStep) {
        webInterface.broadcastStep(newStep);
    });
    // Callback para cambio de patrón en song mode
    sequencer.setPatternChangeCallback([](int newPattern, int songLength) {
        webInterface.broadcastSongPattern(newPattern, songLength);
    });
    sequencer.setTempo(110); // BPM inicial
    spiMaster.setTempo(110.0f); // Sync BPM to Daisy transport
    applyProfessionalMixBaseline();
    
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
    // Extra percusión para ocupar más tracks (8-15)
    sequencer.setStep(8, 2, true);   // LT
    sequencer.setStep(8, 11, true);
    sequencer.setStep(9, 6, true);   // MT
    sequencer.setStep(10, 14, true); // HT
    sequencer.setStep(11, 1, true);  // MA
    sequencer.setStep(11, 9, true);
    sequencer.setStep(12, 3, true);  // CL
    sequencer.setStep(12, 15, true);
    sequencer.setStep(13, 10, true); // HC
    sequencer.setStep(14, 12, true); // MC
    sequencer.setStep(15, 0, true);  // LC
    
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
    // Toms/congas para expandir instrumentación completa
    sequencer.setStep(8, 8, true);   // LT
    sequencer.setStep(9, 10, true);  // MT
    sequencer.setStep(10, 14, true); // HT
    sequencer.setStep(11, 3, true);  // MA
    sequencer.setStep(11, 11, true);
    sequencer.setStep(13, 6, true);  // HC
    sequencer.setStep(14, 13, true); // MC
    sequencer.setStep(15, 15, true); // LC
    sequencer.setStepCutoffLock(1, 0, true, 340);
    sequencer.setStepCutoffLock(1, 4, true, 950);
    sequencer.setStepCutoffLock(1, 8, true, 2600);
    sequencer.setStepCutoffLock(1, 12, true, 7000);
    sequencer.setStepReverbSendLock(1, 4, true, 35);
    sequencer.setStepReverbSendLock(1, 12, true, 55);
    sequencer.setStepVolumeLock(1, 4, true, 112);
    sequencer.setStepVolumeLock(1, 12, true, 120);
    
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
    // Más capas para 16 tracks
    sequencer.setStep(7, 3, true);   // CB
    sequencer.setStep(7, 11, true);
    sequencer.setStep(8, 5, true);   // LT
    sequencer.setStep(9, 9, true);   // MT
    sequencer.setStep(10, 13, true); // HT
    sequencer.setStep(11, 2, true);  // MA
    sequencer.setStep(11, 6, true);
    sequencer.setStep(12, 1, true);  // CL
    sequencer.setStep(12, 9, true);
    sequencer.setStep(13, 7, true);  // HC
    sequencer.setStep(14, 14, true); // MC
    sequencer.setStep(15, 12, true); // LC
    
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
    // Refuerzo de kit completo (cymbal/toms)
    sequencer.setStep(3, 15, true);  // OH cierre
    sequencer.setStep(4, 0, true);   // CY
    sequencer.setStep(4, 8, true);
    sequencer.setStep(8, 4, true);   // LT
    sequencer.setStep(9, 8, true);   // MT
    sequencer.setStep(10, 12, true); // HT
    
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
    // Añadir instrumentos restantes para full groove 16-track
    sequencer.setStep(7, 3, true);   // CB
    sequencer.setStep(7, 11, true);
    sequencer.setStep(8, 6, true);   // LT
    sequencer.setStep(9, 10, true);  // MT
    sequencer.setStep(10, 14, true); // HT
    sequencer.setStep(11, 1, true);  // MA
    sequencer.setStep(11, 5, true);
    sequencer.setStep(12, 7, true);  // CL
    sequencer.setStep(13, 9, true);  // HC
    sequencer.setStep(14, 13, true); // MC
    sequencer.setStep(15, 15, true); // LC

    // === PATRÓN 5: CINEMATIC HYBRID (16 tracks) ===
    sequencer.selectPattern(5);
    sequencer.setStep(0, 0, true);
    sequencer.setStep(0, 7, true);
    sequencer.setStep(0, 10, true);
    sequencer.setStep(0, 14, true);
    sequencer.setStep(1, 4, true);
    sequencer.setStep(1, 11, true);
    sequencer.setStep(1, 12, true);
    for (int i = 0; i < 16; i += 2) sequencer.setStep(2, i, true);
    sequencer.setStep(2, 13, true);
    sequencer.setStep(2, 15, true);
    sequencer.setStep(3, 3, true);
    sequencer.setStep(3, 9, true);
    sequencer.setStep(3, 15, true);
    sequencer.setStep(4, 0, true);
    sequencer.setStep(4, 15, true);
    sequencer.setStep(5, 4, true);
    sequencer.setStep(5, 12, true);
    sequencer.setStep(6, 6, true);
    sequencer.setStep(6, 14, true);
    sequencer.setStep(7, 5, true);
    sequencer.setStep(7, 13, true);
    sequencer.setStep(8, 2, true);
    sequencer.setStep(8, 10, true);
    sequencer.setStep(9, 8, true);
    sequencer.setStep(10, 12, true);
    sequencer.setStep(11, 1, true);
    sequencer.setStep(11, 5, true);
    sequencer.setStep(11, 9, true);
    sequencer.setStep(11, 13, true);
    sequencer.setStep(12, 7, true);
    sequencer.setStep(12, 15, true);
    sequencer.setStep(13, 3, true);
    sequencer.setStep(13, 11, true);
    sequencer.setStep(14, 6, true);
    sequencer.setStep(14, 14, true);
    sequencer.setStep(15, 0, true);
    sequencer.setStep(15, 8, true);
    // Professional transitions through automation locks
    sequencer.setStepCutoffLock(0, 0, true, 320);
    sequencer.setStepCutoffLock(0, 8, true, 1400);
    sequencer.setStepCutoffLock(0, 12, true, 4200);
    sequencer.setStepCutoffLock(0, 15, true, 9000);
    sequencer.setStepReverbSendLock(1, 4, true, 22);
    sequencer.setStepReverbSendLock(1, 12, true, 45);
    sequencer.setStepReverbSendLock(4, 0, true, 60);
    sequencer.setStepReverbSendLock(4, 15, true, 70);
    sequencer.setStepVolumeLock(2, 0, true, 105);
    sequencer.setStepVolumeLock(2, 8, true, 118);
    sequencer.setStepVolumeLock(2, 15, true, 112);

    sequencer.selectPattern(0); // Empezar con Hip Hop
    // sequencer.start(); // DISABLED: User must press PLAY

    // 5. WiFi: solo modo AP (RED808)
    
    showWiFiLED();
    delay(500);
    
    if (webInterface.begin(AP_SSID, AP_PASSWORD, nullptr, nullptr, 0)) {
        showWebServerLED();
        delay(500);
    }

    webInterface.setMIDIController(&midiController);
    midiController.setMessageCallback([](const MIDIMessage& msg) {
        webInterface.broadcastMIDIMessage(msg);
        if (msg.type == MIDI_NOTE_ON && msg.data2 > 0) {
            int8_t pad = midiController.getMappedPad(msg.data1);
            if (pad >= 0) {
                triggerPadWithLED(pad, msg.data2);
            }
        }
    });
    midiController.setDeviceCallback([](bool connected, const MIDIDeviceInfo& info) {
        webInterface.broadcastMIDIDeviceStatus(connected, info);
    });
    midiController.begin();

#if ENABLE_PHYSICAL_BUTTONS
    // Callback: WebInterface notifica cuando llega POST /api/buttons
    // Aplica nueva config en tiempo real sin reiniciar
    webInterface.setBtnConfigCallback([](const String& json) {
        StaticJsonDocument<1024> doc;
        if (deserializeJson(doc, json)) return;
        // Acepta array plano [...] o {"buttons":[...]}
        JsonArray arr = doc.is<JsonArray>() ? doc.as<JsonArray>() : doc["buttons"].as<JsonArray>();
        if (arr.isNull()) return;
        for (int i = 0; i < 4 && i < (int)arr.size(); i++) {
            BtnCfg cfg;
            cfg.funcId   = arr[i]["funcId"]   | (uint8_t)ctrlButtons.getCfg(i).funcId;
            cfg.colorOff = arr[i]["colorOff"]  | (uint32_t)CTRL_CLR_RED;
            cfg.colorOn  = arr[i]["colorOn"]   | (uint32_t)CTRL_CLR_GREEN;
            const char* lbl = arr[i]["label"];
            if (lbl) strncpy(cfg.label, lbl, 19);
            cfg.label[19] = '\0';
            ctrlButtons.setCfg(i, cfg);
        }
    });

    // --- BOTONES FÍSICOS CON LED RGB ---
    // Cargar configuración guardada antes de begin()
    {
        File f;
        if (LittleFS.exists("/buttons.json")) {
            f = LittleFS.open("/buttons.json", "r");
        }
        if (f) {
            StaticJsonDocument<1024> doc;
            if (!deserializeJson(doc, f)) {
                // Acepta array plano [...] o {"buttons":[...]}
                JsonArray arr = doc.is<JsonArray>() ? doc.as<JsonArray>() : doc["buttons"].as<JsonArray>();
                if (!arr.isNull()) {
                    for (int i = 0; i < 4 && i < (int)arr.size(); i++) {
                        BtnCfg cfg;
                        cfg.funcId   = arr[i]["funcId"]   | (uint8_t)ctrlButtons.getCfg(i).funcId;
                        cfg.colorOff = arr[i]["colorOff"]  | (uint32_t)CTRL_CLR_RED;
                        cfg.colorOn  = arr[i]["colorOn"]   | (uint32_t)CTRL_CLR_GREEN;
                        const char* lbl = arr[i]["label"];
                        if (lbl) strncpy(cfg.label, lbl, 19);
                        cfg.label[19] = '\0';
                        ctrlButtons.setCfg(i, cfg);
                    }
                }
            }
            f.close();
        }
    }
    ctrlButtons.begin();

    // Callback genérico — dispatcher de todas las funciones
    ctrlButtons.onAction = [](int btnIdx, uint8_t funcId) {
        char buf[128];
        switch (funcId) {
            /* ── Transporte ── */
            case BTN_FUNC_PLAY_PAUSE: {
                bool nowPlaying = !sequencer.isPlaying();
                if (nowPlaying) { sequencer.start(); spiMaster.dsqControl(1); }
                else            { sequencer.stop();  spiMaster.dsqControl(0); }
                ctrlButtons.setLedState(btnIdx, nowPlaying);
                webInterface.broadcastSequencerState();
                break;
            }
            case BTN_FUNC_STOP:
                sequencer.stop(); spiMaster.dsqControl(0);
                ctrlButtons.setLedState(btnIdx, false);
                webInterface.broadcastSequencerState();
                break;
            case BTN_FUNC_NEXT_PATTERN:
            case BTN_FUNC_NEXT_PAT_PLAY: {
                int next = (sequencer.getCurrentPattern() + 1) % DSQ_PATTERNS;
                sequencer.selectPattern(next);
                spiMaster.dsqSelectPattern((uint8_t)next);
                if (funcId == BTN_FUNC_NEXT_PAT_PLAY) { sequencer.start(); spiMaster.dsqControl(1); }
                ctrlButtons.flashLed(btnIdx);
                snprintf(buf, sizeof(buf),
                    "{\"type\":\"physButton\",\"action\":\"nextPattern\",\"pattern\":%d}", next);
                webInterface.broadcastRaw(buf);
                break;
            }
            case BTN_FUNC_PREV_PATTERN:
            case BTN_FUNC_PREV_PAT_PLAY: {
                int prev = (sequencer.getCurrentPattern() + DSQ_PATTERNS - 1) % DSQ_PATTERNS;
                sequencer.selectPattern(prev);
                spiMaster.dsqSelectPattern((uint8_t)prev);
                if (funcId == BTN_FUNC_PREV_PAT_PLAY) { sequencer.start(); spiMaster.dsqControl(1); }
                ctrlButtons.flashLed(btnIdx);
                snprintf(buf, sizeof(buf),
                    "{\"type\":\"physButton\",\"action\":\"prevPattern\",\"pattern\":%d}", prev);
                webInterface.broadcastRaw(buf);
                break;
            }
            case BTN_FUNC_TAP_TEMPO:
                ctrlButtons.flashLed(btnIdx, CTRL_CLR_YELLOW);
                // Tap tempo no implementado aquí (requiere multipress timing)
                break;
            /* ── Navegación ── */
            case BTN_FUNC_MULTIVIEW:
                gMultiviewActive = !gMultiviewActive;
                ctrlButtons.setLedState(btnIdx, gMultiviewActive);
                snprintf(buf, sizeof(buf),
                    "{\"type\":\"physButton\",\"action\":\"multiview\",\"active\":%s}",
                    gMultiviewActive ? "true" : "false");
                webInterface.broadcastRaw(buf);
                break;
            /* ── Volumen ── */
            case BTN_FUNC_MASTER_VOL_UP: {
                uint8_t v = min(150, (int)spiMaster.getMasterVolume() + 5);
                spiMaster.setMasterVolume(v);
                ctrlButtons.flashLed(btnIdx, CTRL_CLR_GREEN);
                break;
            }
            case BTN_FUNC_MASTER_VOL_DN: {
                uint8_t v = (uint8_t)max(0, (int)spiMaster.getMasterVolume() - 5);
                spiMaster.setMasterVolume(v);
                ctrlButtons.flashLed(btnIdx, CTRL_CLR_RED);
                break;
            }
            case BTN_FUNC_LIVE_VOL_UP: {
                uint8_t v = min(180, (int)spiMaster.getLiveVolume() + 5);
                spiMaster.setLiveVolume(v);
                ctrlButtons.flashLed(btnIdx, CTRL_CLR_GREEN);
                break;
            }
            case BTN_FUNC_LIVE_VOL_DN: {
                uint8_t v = (uint8_t)max(0, (int)spiMaster.getLiveVolume() - 5);
                spiMaster.setLiveVolume(v);
                ctrlButtons.flashLed(btnIdx, CTRL_CLR_RED);
                break;
            }
            /* ── Tempo ── */
            case BTN_FUNC_TEMPO_UP1: spiMaster.setTempo(sequencer.getTempo() + 1);  sequencer.setTempo(sequencer.getTempo() + 1);  ctrlButtons.flashLed(btnIdx, CTRL_CLR_YELLOW); break;
            case BTN_FUNC_TEMPO_DN1: spiMaster.setTempo(max(20.f, sequencer.getTempo() - 1)); sequencer.setTempo(max(20.f, sequencer.getTempo() - 1)); ctrlButtons.flashLed(btnIdx, CTRL_CLR_YELLOW); break;
            case BTN_FUNC_TEMPO_UP5: spiMaster.setTempo(sequencer.getTempo() + 5);  sequencer.setTempo(sequencer.getTempo() + 5);  ctrlButtons.flashLed(btnIdx, CTRL_CLR_YELLOW); break;
            case BTN_FUNC_TEMPO_DN5: spiMaster.setTempo(max(20.f, sequencer.getTempo() - 5)); sequencer.setTempo(max(20.f, sequencer.getTempo() - 5)); ctrlButtons.flashLed(btnIdx, CTRL_CLR_YELLOW); break;
            /* ── FX Master toggles ── */
            case BTN_FUNC_DELAY_TOGGLE:   gDelayActive   = !gDelayActive;   spiMaster.setDelayActive(gDelayActive);    ctrlButtons.setLedState(btnIdx, gDelayActive);   break;
            case BTN_FUNC_REVERB_TOGGLE:  gReverbActive  = !gReverbActive;  spiMaster.setReverbActive(gReverbActive);  ctrlButtons.setLedState(btnIdx, gReverbActive);  break;
            case BTN_FUNC_CHORUS_TOGGLE:  gChorusActive  = !gChorusActive;  spiMaster.setChorusActive(gChorusActive);  ctrlButtons.setLedState(btnIdx, gChorusActive);  break;
            case BTN_FUNC_PHASER_TOGGLE:  gPhaserActive  = !gPhaserActive;  spiMaster.setPhaserActive(gPhaserActive);  ctrlButtons.setLedState(btnIdx, gPhaserActive);  break;
            case BTN_FUNC_FLANGER_TOGGLE: gFlangerActive = !gFlangerActive; spiMaster.setFlangerActive(gFlangerActive); ctrlButtons.setLedState(btnIdx, gFlangerActive); break;
            case BTN_FUNC_COMP_TOGGLE:    gCompActive    = !gCompActive;    spiMaster.setCompressorActive(gCompActive);  ctrlButtons.setLedState(btnIdx, gCompActive);    break;
            case BTN_FUNC_TREMOLO_TOGGLE: gTremoloActive = !gTremoloActive; spiMaster.setTremoloActive(gTremoloActive); ctrlButtons.setLedState(btnIdx, gTremoloActive); break;
            case BTN_FUNC_LIMITER_TOGGLE: gLimiterActive = !gLimiterActive; spiMaster.setLimiterActive(gLimiterActive); ctrlButtons.setLedState(btnIdx, gLimiterActive); break;
            case BTN_FUNC_DIST_TOGGLE:    gDistActive    = !gDistActive; ctrlButtons.setLedState(btnIdx, gDistActive); break;
            /* ── Mute/Solo ── */
            case BTN_FUNC_MUTE_ALL:
                for (int t=0;t<16;t++) spiMaster.setTrackMute(t, true);
                ctrlButtons.setLedState(btnIdx, true);
                break;
            case BTN_FUNC_UNMUTE_ALL:
                for (int t=0;t<16;t++) spiMaster.setTrackMute(t, false);
                ctrlButtons.setLedState(btnIdx, false);
                break;
            /* ── Longitud patrón ── */
            case BTN_FUNC_PAT_LEN_CYCLE: {
                int cur = sequencer.getPatternLength();
                int next = (cur == 16) ? 32 : (cur == 32) ? 64 : 16;
                sequencer.setPatternLength(next);
                spiMaster.dsqSetLength((uint8_t)next);
                ctrlButtons.flashLed(btnIdx, CTRL_CLR_PURPLE);
                break;
            }
            /* ── Ir a patrón N ── */
            case BTN_FUNC_PATTERN_0: case BTN_FUNC_PATTERN_1: case BTN_FUNC_PATTERN_2:
            case BTN_FUNC_PATTERN_3: case BTN_FUNC_PATTERN_4: case BTN_FUNC_PATTERN_5:
            case BTN_FUNC_PATTERN_6: case BTN_FUNC_PATTERN_7: {
                int pIdx = funcId - BTN_FUNC_PATTERN_0;
                sequencer.selectPattern(pIdx);
                spiMaster.dsqSelectPattern((uint8_t)pIdx);
                ctrlButtons.flashLed(btnIdx, CTRL_CLR_CYAN);
                snprintf(buf, sizeof(buf),
                    "{\"type\":\"physButton\",\"action\":\"nextPattern\",\"pattern\":%d}", pIdx);
                webInterface.broadcastRaw(buf);
                break;
            }
            /* ── Live Pads (disparo directo vía SPI) ── */
            case BTN_FUNC_LIVE_PAD_0:  case BTN_FUNC_LIVE_PAD_1:  case BTN_FUNC_LIVE_PAD_2:
            case BTN_FUNC_LIVE_PAD_3:  case BTN_FUNC_LIVE_PAD_4:  case BTN_FUNC_LIVE_PAD_5:
            case BTN_FUNC_LIVE_PAD_6:  case BTN_FUNC_LIVE_PAD_7:  case BTN_FUNC_LIVE_PAD_8:
            case BTN_FUNC_LIVE_PAD_9:  case BTN_FUNC_LIVE_PAD_10: case BTN_FUNC_LIVE_PAD_11:
            case BTN_FUNC_LIVE_PAD_12: case BTN_FUNC_LIVE_PAD_13: case BTN_FUNC_LIVE_PAD_14:
            case BTN_FUNC_LIVE_PAD_15: {
                uint8_t padIdx = (uint8_t)(funcId - BTN_FUNC_LIVE_PAD_0);
                spiMaster.triggerSampleLive(padIdx, 127);
                ctrlButtons.flashLed(btnIdx, CTRL_CLR_GREEN);
                snprintf(buf, sizeof(buf),
                    "{\"type\":\"physButton\",\"action\":\"triggerPad\",\"pad\":%d}", padIdx);
                webInterface.broadcastRaw(buf);
                break;
            }
            /* ── XTRA Pads (índices 16-23) ── */
            case BTN_FUNC_XTRA_PAD_0: case BTN_FUNC_XTRA_PAD_1: case BTN_FUNC_XTRA_PAD_2:
            case BTN_FUNC_XTRA_PAD_3: case BTN_FUNC_XTRA_PAD_4: case BTN_FUNC_XTRA_PAD_5:
            case BTN_FUNC_XTRA_PAD_6: case BTN_FUNC_XTRA_PAD_7: {
                uint8_t padIdx = (uint8_t)(16 + (funcId - BTN_FUNC_XTRA_PAD_0));
                spiMaster.triggerSampleLive(padIdx, 127);
                ctrlButtons.flashLed(btnIdx, CTRL_CLR_ORANGE);
                snprintf(buf, sizeof(buf),
                    "{\"type\":\"physButton\",\"action\":\"triggerPad\",\"pad\":%d}", padIdx);
                webInterface.broadcastRaw(buf);
                break;
            }
        }
    };
#endif

    // --- SD EVENT CALLBACK (Daisy → WebSocket) ---
    spiMaster.setEventCallback([](const NotifyEvent& evt, void* /*ud*/) {
        StaticJsonDocument<256> doc;
        doc["type"]     = "sdEvent";
        doc["event"]    = evt.type;       // EVT_SD_*
        doc["padCount"] = evt.padCount;
        doc["maskLo"]   = evt.padMaskLo;
        doc["maskHi"]   = evt.padMaskHi;
        doc["maskXtra"] = evt.padMaskXtra;
        doc["name"]     = String(evt.name);
        String out;
        serializeJson(doc, out);
        webInterface.broadcastRaw(out.c_str());
    });

    // --- DUAL-CORE TASKS ---
    
    // CORE 1: SPI Audio Task (Sequencer + SPI) - Prioridad máxima
    xTaskCreatePinnedToCore(
        spiAudioTask,
        "SPIAudioTask",
        20480,  // 20KB stack — patrón upload en boot + DSQ upload (16 tracks × 8 patrones)
        NULL,
        24,     // Prioridad máxima
        NULL,
        1       // CORE 1: Sequencer + SPI Master
    );
    
    // CORE 0: System Task - Prioridad media
    xTaskCreatePinnedToCore(
        systemTask,
        "SystemTask",
        24576,  // 24KB stack - WiFi/JSON/UDP (increased for safety)
        NULL,
        5,
        NULL,
        0       // CORE 0: WiFi, Web, MIDI, LED
    );

    // ── Task Watchdog: 10s timeout, panic on timeout ──
    // Initialized AFTER setup completes to avoid WDT reset during sample loading
    esp_task_wdt_init(10, true);
    esp_task_wdt_add(NULL);  // subscribe loopTask

    showReadyLED();
}

void loop() {
    esp_task_wdt_reset();   // feed TWDT every iteration

    // Heap monitor cada 10 segundos
    static uint32_t lastStats = 0;
    if (millis() - lastStats > 10000) {
        uint32_t heap = ESP.getFreeHeap();
        uint32_t minHeap = ESP.getMinFreeHeap();
        Serial.printf("[HEAP] free=%u min=%u psram=%u\n", heap, minHeap, (uint32_t)ESP.getFreePsram());
        if (heap < 30000) {
            webInterface.broadcastRaw("{\"type\":\"warning\",\"msg\":\"low_heap\"}");
        }
        lastStats = millis();
    }
    vTaskDelay(pdMS_TO_TICKS(100)); // loop() no hace nada crítico
}
