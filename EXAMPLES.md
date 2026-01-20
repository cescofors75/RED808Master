# Exemples i Extensions - ESP32-S3 Drum Machine

## Exemple 1: Afegir el Sequencer

Per afegir funcionalitat de sequencer al projecte:

### 1. Afegir al fitxer principal

```cpp
#include "Sequencer.h"

// Global object
Sequencer sequencer;

// Callback per reproduir steps
void onSequencerStep(int track, uint8_t velocity) {
  audioEngine.triggerSample(track, velocity);
  display.flashPad(track);
}

void setup() {
  // ... codi existent ...
  
  // Setup sequencer
  sequencer.setStepCallback(onSequencerStep);
  sequencer.setTempo(120.0f); // 120 BPM
  
  // Exemple: Crear un pattern bàsic
  // Kick a 1, 5, 9, 13
  sequencer.setStep(0, 0, true, 127);
  sequencer.setStep(0, 4, true, 127);
  sequencer.setStep(0, 8, true, 127);
  sequencer.setStep(0, 12, true, 127);
  
  // Snare a 4, 12
  sequencer.setStep(1, 4, true, 100);
  sequencer.setStep(1, 12, true, 100);
  
  // Hihat cada 2 steps
  for (int i = 0; i < 16; i += 2) {
    sequencer.setStep(2, i, true, 80);
  }
}

void loop() {
  // Update sequencer
  sequencer.update();
  
  // ... resta del codi ...
}
```

### 2. Afegir controls per start/stop

```cpp
void InputManager::handleMainMode() {
  // ... codi existent ...
  
  // SELECT: Toggle sequencer play/stop
  if (stateSelect == BTN_PRESSED) {
    if (sequencer.isPlaying()) {
      sequencer.stop();
    } else {
      sequencer.start();
    }
  }
  
  // BACK held: Reset sequencer
  if (stateBack == BTN_HELD) {
    sequencer.reset();
  }
}
```

## Exemple 2: Velocity Sensible amb ADC

Si vols usar sensors piezo per detectar la força del cop:

```cpp
// Al setup()
void setupVelocitySensing() {
  // Configura pins ADC per pads
  analogReadResolution(12); // 12-bit resolution (0-4095)
  
  // Opcional: Calibració
  for (int i = 0; i < 16; i++) {
    pinMode(PAD_ADC_PINS[i], INPUT);
  }
}

// Al loop()
void checkPadsWithVelocity() {
  static uint16_t threshold = 100; // Mínim per triggerar
  static uint16_t maxVelocity = 4095;
  
  for (int i = 0; i < 16; i++) {
    uint16_t value = analogRead(PAD_ADC_PINS[i]);
    
    if (value > threshold) {
      // Converteix a MIDI velocity (0-127)
      uint8_t velocity = map(value, threshold, maxVelocity, 1, 127);
      velocity = constrain(velocity, 1, 127);
      
      audioEngine.triggerSample(i, velocity);
      display.flashPad(i);
      
      // Debounce
      delay(10);
    }
  }
}
```

## Exemple 3: MIDI Input (via Serial MIDI)

Afegeix MIDI input per controlar la drum machine externament:

```cpp
#include <MIDI.h>

MIDI_CREATE_INSTANCE(HardwareSerial, Serial2, MIDI);

void onNoteOn(byte channel, byte note, byte velocity) {
  // Mapeja MIDI notes a pads (36-51 = GM Drum Map)
  int pad = note - 36;
  
  if (pad >= 0 && pad < 16) {
    audioEngine.triggerSample(pad, velocity);
    display.flashPad(pad);
  }
}

void setup() {
  // ... codi existent ...
  
  MIDI.begin(MIDI_CHANNEL_OMNI);
  MIDI.setHandleNoteOn(onNoteOn);
}

void loop() {
  MIDI.read();
  
  // ... resta del codi ...
}
```

## Exemple 4: Efectes Simples (Low-Pass Filter)

Afegir un filtre passa-baixos simple:

```cpp
class SimpleLPF {
  private:
    float alpha;
    float lastOutput;
    
  public:
    SimpleLPF(float cutoff = 0.5f) : alpha(cutoff), lastOutput(0) {}
    
    int16_t process(int16_t input) {
      lastOutput = alpha * input + (1.0f - alpha) * lastOutput;
      return (int16_t)lastOutput;
    }
    
    void setCutoff(float cutoff) {
      alpha = constrain(cutoff, 0.0f, 1.0f);
    }
};

// A AudioEngine.cpp, afegir:
SimpleLPF filterLeft(0.8f);
SimpleLPF filterRight(0.8f);

void AudioEngine::fillBuffer(int16_t* buffer, size_t samples) {
  // ... codi mixing existent ...
  
  // Apply filter
  for (size_t i = 0; i < samples * 2; i += 2) {
    buffer[i] = filterLeft.process(buffer[i]);       // Left
    buffer[i + 1] = filterRight.process(buffer[i + 1]); // Right
  }
}
```

## Exemple 5: Save/Load Patterns a SPIFFS

Guardar i carregar patterns del sequencer:

```cpp
bool savePattern(int patternIndex, const char* filename) {
  File file = SPIFFS.open(filename, "w");
  if (!file) return false;
  
  // Header
  file.write((uint8_t*)"PTTN", 4);
  file.write((uint8_t*)&patternIndex, sizeof(int));
  
  // Pattern data
  for (int t = 0; t < MAX_TRACKS; t++) {
    for (int s = 0; s < STEPS_PER_PATTERN; s++) {
      bool active = sequencer.getStep(t, s);
      file.write((uint8_t*)&active, sizeof(bool));
      
      // Velocity
      uint8_t vel = 127; // Get from sequencer
      file.write((uint8_t*)&vel, sizeof(uint8_t));
    }
  }
  
  file.close();
  return true;
}

bool loadPattern(int patternIndex, const char* filename) {
  File file = SPIFFS.open(filename, "r");
  if (!file) return false;
  
  // Verify header
  char header[4];
  file.read((uint8_t*)header, 4);
  if (strncmp(header, "PTTN", 4) != 0) {
    file.close();
    return false;
  }
  
  int index;
  file.read((uint8_t*)&index, sizeof(int));
  
  // Load pattern data
  for (int t = 0; t < MAX_TRACKS; t++) {
    for (int s = 0; s < STEPS_PER_PATTERN; s++) {
      bool active;
      uint8_t vel;
      
      file.read((uint8_t*)&active, sizeof(bool));
      file.read((uint8_t*)&vel, sizeof(uint8_t));
      
      sequencer.setStep(t, s, active, vel);
    }
  }
  
  file.close();
  return true;
}
```

## Exemple 6: Pitch Shift en Temps Real

Afegir control de pitch per pad:

```cpp
// Al DisplayManager, afegir mode d'edició
void drawPitchEditor(int padIndex, float pitch) {
  tft.fillScreen(TFT_BLACK);
  tft.drawString("PITCH SHIFT", 60, 10, 2);
  
  char buf[32];
  sprintf(buf, "Pad %d: %.2f", padIndex + 1, pitch);
  tft.drawString(buf, 60, 60, 2);
  
  // Draw pitch bar
  int barY = 120;
  int barX = 20;
  int barW = 200;
  int barH = 30;
  
  tft.drawRect(barX, barY, barW, barH, TFT_WHITE);
  
  // Center line (1.0x pitch)
  int centerX = barX + barW / 2;
  tft.drawFastVLine(centerX, barY, barH, TFT_GREEN);
  
  // Current pitch position
  int pitchX = barX + (int)((pitch - 0.5f) * barW / 1.5f);
  tft.fillRect(pitchX - 2, barY, 4, barH, TFT_YELLOW);
}

// Al InputManager, afegir controls
void adjustPitch(int padIndex, int direction) {
  static float pitches[16] = {1.0f}; // Default 1.0x
  
  pitches[padIndex] += direction * 0.05f; // ±5%
  pitches[padIndex] = constrain(pitches[padIndex], 0.5f, 2.0f);
  
  // Apply to next voice that uses this pad
  // (caldria modificar AudioEngine per suportar pitch per pad)
  
  display.drawPitchEditor(padIndex, pitches[padIndex]);
}
```

## Exemple 7: Wi-Fi Web Interface

Control remot via navegador:

```cpp
#include <WiFi.h>
#include <WebServer.h>

WebServer server(80);

void handleRoot() {
  String html = R"(
    <!DOCTYPE html>
    <html>
    <head><title>Drum Machine</title></head>
    <body>
      <h1>ESP32-S3 Drum Machine</h1>
      <button onclick="fetch('/trigger?pad=0')">Kick</button>
      <button onclick="fetch('/trigger?pad=1')">Snare</button>
      <button onclick="fetch('/trigger?pad=2')">HiHat</button>
      <br><br>
      <button onclick="fetch('/seq/start')">Start</button>
      <button onclick="fetch('/seq/stop')">Stop</button>
    </body>
    </html>
  )";
  
  server.send(200, "text/html", html);
}

void handleTrigger() {
  if (server.hasArg("pad")) {
    int pad = server.arg("pad").toInt();
    if (pad >= 0 && pad < 16) {
      audioEngine.triggerSample(pad, 127);
      server.send(200, "text/plain", "OK");
      return;
    }
  }
  server.send(400, "text/plain", "Invalid pad");
}

void setupWiFi() {
  WiFi.begin("YOUR_SSID", "YOUR_PASSWORD");
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }
  
  server.on("/", handleRoot);
  server.on("/trigger", handleTrigger);
  server.begin();
  
  Serial.print("Web interface: http://");
  Serial.println(WiFi.localIP());
}
```

## Millores de Rendiment

### 1. Optimització de Mixing

```cpp
// Usar fixed-point math en comptes de float
void AudioEngine::fillBuffer(int16_t* buffer, size_t samples) {
  memset(buffer, 0, samples * sizeof(int16_t) * 2);
  
  for (int v = 0; v < MAX_VOICES; v++) {
    if (!voices[v].active) continue;
    
    Voice& voice = voices[v];
    int16_t* src = voice.buffer;
    
    // Unroll loop per millor rendiment
    for (size_t i = 0; i < samples; i += 4) {
      // Process 4 samples at once
      // ...
    }
  }
}
```

### 2. Usar DMA per I2S

```cpp
// Ja està implementat, però es pot afinar:
i2s_config_t i2s_config = {
  // ...
  .dma_buf_count = 16,  // Més buffers
  .dma_buf_len = 128,   // Buffers més petits
  // ...
};
```

## Recursos Addicionals

- **Sample Packs Gratis**: 
  - https://samples.kb6.de/
  - https://www.musicradar.com/news/tech/free-music-samples

- **Documentació ESP32-S3**:
  - https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/

- **Llibreries Audio**:
  - ESP32-audioI2S
  - ESP32-A2DP (Bluetooth audio)

- **Inspiració**:
  - Roland TR-808/909
  - Elektron Digitakt
  - Teenage Engineering PO series
```
