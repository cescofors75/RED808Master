/*
 * Sequencer.cpp
 * Implementació del sequencer de 16 steps
 */

#include "Sequencer.h"

Sequencer::Sequencer() : 
  playing(false), 
  currentPattern(0), 
  currentStep(0), 
  tempo(120.0f),
  lastStepTime(0),
  stepCallback(nullptr),
  stepChangeCallback(nullptr),
  patternChangeCallback(nullptr),
  songMode(false),
  songLength(1) {
  
  // Initialize all patterns to clean state
  memset(steps, 0, sizeof(steps));
  for (int p = 0; p < MAX_PATTERNS; p++) {
    for (int t = 0; t < MAX_TRACKS; t++) {
      for (int s = 0; s < STEPS_PER_PATTERN; s++) {
        velocities[p][t][s] = 127;
        noteLenDivs[p][t][s] = 1;  // Default: full note
      }
    }
  }
  
  for (int t = 0; t < MAX_TRACKS; t++) {
    trackMuted[t] = false;
    trackVolume[t] = 100;
    loopActive[t] = false;
    loopPaused[t] = false;
    loopType[t] = LOOP_EVERY_STEP;
    loopStepCounter[t] = 0;
  }
  
  calculateStepInterval();
}

Sequencer::~Sequencer() {
}

void Sequencer::start() {
  playing = true;
  lastStepTime = micros();
  Serial.println("Sequencer started");
}

void Sequencer::stop() {
  playing = false;
  Serial.println("Sequencer stopped");
}

void Sequencer::reset() {
  currentStep = 0;
  lastStepTime = micros();
}

bool Sequencer::isPlaying() {
  return playing;
}

void Sequencer::setTempo(float bpm) {
  if (bpm < 40.0f) bpm = 40.0f;
  if (bpm > 300.0f) bpm = 300.0f;
  tempo = bpm;
  calculateStepInterval();
}

float Sequencer::getTempo() {
  return tempo;
}

void Sequencer::calculateStepInterval() {
  // 16th notes
  // 1 beat = 60/BPM seconds
  // 1 16th note = (60/BPM) / 4 seconds
  // Convert to microseconds
  stepInterval = (uint32_t)((60.0f / tempo / 4.0f) * 1000000.0f);
}

void Sequencer::update() {
  if (!playing) return;
  
  uint32_t now = micros();
  
  // Check if it's time for next step
  if (now - lastStepTime >= stepInterval) {
    lastStepTime = now;
    
    // PRIMERO: Notificar el step ACTUAL (antes de avanzar)
    // Esto sincroniza la visualización con el audio
    if (stepChangeCallback != nullptr) {
      stepChangeCallback(currentStep);
    }
    
    // SEGUNDO: Procesar el audio del step actual
    processStep();
    
    // TERCERO: Avanzar al siguiente step para la próxima iteración
    currentStep++;
    if (currentStep >= STEPS_PER_PATTERN) {
      currentStep = 0;
      
      // Song mode: auto-advance to next pattern
      if (songMode && songLength > 1) {
        int nextPattern = currentPattern + 1;
        if (nextPattern >= songLength) {
          nextPattern = 0; // Loop back to start
        }
        currentPattern = nextPattern;
        Serial.printf("[Song] Advanced to pattern %d/%d\n", currentPattern + 1, songLength);
        if (patternChangeCallback != nullptr) {
          patternChangeCallback(currentPattern, songLength);
        }
      }
    }
  }
}

void Sequencer::processStep() {
  // First: Process looped tracks
  processLoops();
  
  // Trigger all active tracks at current step
  for (int track = 0; track < MAX_TRACKS; track++) {
    // Check sequencer steps
    if (steps[currentPattern][track][currentStep] && !trackMuted[track]) {
      uint8_t velocity = velocities[currentPattern][track][currentStep];
      uint8_t div = noteLenDivs[currentPattern][track][currentStep];
      
      // Compute max samples for note length (0 = full sample)
      // stepInterval is in microseconds, SAMPLE_RATE = 44100
      uint32_t noteLenSamples = 0;
      if (div > 1) {
        // samples = (stepInterval_us / div) * 44100 / 1000000
        noteLenSamples = (uint32_t)(((uint64_t)stepInterval * 44100UL) / ((uint32_t)div * 1000000UL));
        if (noteLenSamples < 64) noteLenSamples = 64;  // minimum
      }
      
      // Call callback if set
      if (stepCallback != nullptr) {
        stepCallback(track, velocity, trackVolume[track], noteLenSamples);
      }
    }
  }
}

void Sequencer::setStep(int track, int step, bool active, uint8_t velocity) {
  if (track < 0 || track >= MAX_TRACKS) return;
  if (step < 0 || step >= STEPS_PER_PATTERN) return;
  
  steps[currentPattern][track][step] = active;
  velocities[currentPattern][track][step] = velocity;
}

bool Sequencer::getStep(int track, int step) {
  if (track < 0 || track >= MAX_TRACKS) return false;
  if (step < 0 || step >= STEPS_PER_PATTERN) return false;
  
  return steps[currentPattern][track][step];
}

bool Sequencer::getStep(int pattern, int track, int step) {
  if (pattern < 0 || pattern >= MAX_PATTERNS) return false;
  if (track < 0 || track >= MAX_TRACKS) return false;
  if (step < 0 || step >= STEPS_PER_PATTERN) return false;
  
  return steps[pattern][track][step];
}

void Sequencer::clearPattern(int pattern) {
  if (pattern < 0 || pattern >= MAX_PATTERNS) return;
  
  for (int t = 0; t < MAX_TRACKS; t++) {
    for (int s = 0; s < STEPS_PER_PATTERN; s++) {
      steps[pattern][t][s] = false;
      velocities[pattern][t][s] = 127;
    }
  }
  
  Serial.printf("Pattern %d cleared\n", pattern);
}

void Sequencer::clearPattern() {
  clearPattern(currentPattern);
}

void Sequencer::clearTrack(int track) {
  if (track < 0 || track >= MAX_TRACKS) return;
  
  for (int s = 0; s < STEPS_PER_PATTERN; s++) {
    steps[currentPattern][track][s] = false;
  }
  
  Serial.printf("Track %d cleared\n", track);
}

// ============= VELOCITY EDITING =============

void Sequencer::setStepVelocity(int track, int step, uint8_t velocity) {
  if (track < 0 || track >= MAX_TRACKS) return;
  if (step < 0 || step >= STEPS_PER_PATTERN) return;
  velocities[currentPattern][track][step] = constrain(velocity, 1, 127);
}

void Sequencer::setStepVelocity(int pattern, int track, int step, uint8_t velocity) {
  if (pattern < 0 || pattern >= MAX_PATTERNS) return;
  if (track < 0 || track >= MAX_TRACKS) return;
  if (step < 0 || step >= STEPS_PER_PATTERN) return;
  velocities[pattern][track][step] = constrain(velocity, 1, 127);
}

uint8_t Sequencer::getStepVelocity(int track, int step) {
  if (track < 0 || track >= MAX_TRACKS) return 127;
  if (step < 0 || step >= STEPS_PER_PATTERN) return 127;
  
  return velocities[currentPattern][track][step];
}

uint8_t Sequencer::getStepVelocity(int pattern, int track, int step) {
  if (pattern < 0 || pattern >= MAX_PATTERNS) return 127;
  if (track < 0 || track >= MAX_TRACKS) return 127;
  if (step < 0 || step >= STEPS_PER_PATTERN) return 127;
  
  return velocities[pattern][track][step];
}

void Sequencer::setStepNoteLen(int track, int step, uint8_t div) {
  if (track < 0 || track >= MAX_TRACKS) return;
  if (step < 0 || step >= STEPS_PER_PATTERN) return;
  if (div == 0) div = 1;  // Sanitize
  noteLenDivs[currentPattern][track][step] = div;
}

uint8_t Sequencer::getStepNoteLen(int track, int step) {
  if (track < 0 || track >= MAX_TRACKS) return 1;
  if (step < 0 || step >= STEPS_PER_PATTERN) return 1;
  return noteLenDivs[currentPattern][track][step];
}

uint8_t Sequencer::getStepNoteLen(int pattern, int track, int step) {
  if (pattern < 0 || pattern >= MAX_PATTERNS) return 1;
  if (track < 0 || track >= MAX_TRACKS) return 1;
  if (step < 0 || step >= STEPS_PER_PATTERN) return 1;
  return noteLenDivs[pattern][track][step];
}

void Sequencer::setPatternBulk(int pattern, const bool stepsData[16][16], const uint8_t velsData[16][16]) {
  if (pattern < 0 || pattern >= MAX_PATTERNS) return;
  
  for (int t = 0; t < MAX_TRACKS; t++) {
    for (int s = 0; s < STEPS_PER_PATTERN; s++) {
      steps[pattern][t][s] = stepsData[t][s];
      velocities[pattern][t][s] = velsData[t][s];
    }
  }
  Serial.printf("[Bulk] Pattern %d written (16x16)\n", pattern);
}

void Sequencer::selectPattern(int pattern) {
  if (pattern < 0 || pattern >= MAX_PATTERNS) return;
  
  currentPattern = pattern;
  Serial.printf("Pattern %d selected\n", pattern);
}

void Sequencer::muteTrack(int track, bool muted) {
  if (track >= 0 && track < MAX_TRACKS) {
    trackMuted[track] = muted;
  }
}

bool Sequencer::isTrackMuted(int track) {
  if (track >= 0 && track < MAX_TRACKS) {
    return trackMuted[track];
  }
  return false;
}

void Sequencer::setTrackVolume(int track, uint8_t volume) {
  if (track >= 0 && track < MAX_TRACKS) {
    trackVolume[track] = constrain(volume, 0, 150);
  }
}

uint8_t Sequencer::getTrackVolume(int track) {
  if (track >= 0 && track < MAX_TRACKS) {
    return trackVolume[track];
  }
  return 100; // Default volume
}

int Sequencer::getCurrentPattern() {
  return currentPattern;
}

void Sequencer::copyPattern(int src, int dst) {
  if (src < 0 || src >= MAX_PATTERNS) return;
  if (dst < 0 || dst >= MAX_PATTERNS) return;
  
  for (int t = 0; t < MAX_TRACKS; t++) {
    for (int s = 0; s < STEPS_PER_PATTERN; s++) {
      steps[dst][t][s] = steps[src][t][s];
      velocities[dst][t][s] = velocities[src][t][s];
    }
  }
  
  Serial.printf("Pattern %d copied to %d\n", src, dst);
}

int Sequencer::getCurrentStep() {
  return currentStep;
}

void Sequencer::setStepCallback(StepCallback callback) {
  stepCallback = callback;
}

void Sequencer::setStepChangeCallback(StepChangeCallback callback) {
  stepChangeCallback = callback;
}

void Sequencer::setPatternChangeCallback(PatternChangeCallback callback) {
  patternChangeCallback = callback;
}

// ============= SONG MODE =============

void Sequencer::setSongMode(bool enabled) {
  songMode = enabled;
  if (songMode) {
    // When entering song mode, start from pattern 0
    currentPattern = 0;
    Serial.printf("[Song] Song mode ON, length=%d patterns\n", songLength);
  } else {
    Serial.println("[Song] Song mode OFF");
  }
}

bool Sequencer::isSongMode() {
  return songMode;
}

void Sequencer::setSongLength(int length) {
  if (length < 1) length = 1;
  if (length > MAX_PATTERNS) length = MAX_PATTERNS;
  songLength = length;
  Serial.printf("[Song] Song length set to %d patterns\n", songLength);
}

int Sequencer::getSongLength() {
  return songLength;
}

// ============= LOOP SYSTEM =============

void Sequencer::toggleLoop(int track) {
  if (track >= 0 && track < MAX_TRACKS) {
    loopActive[track] = !loopActive[track];
    loopPaused[track] = false; // Reset pause state
    loopStepCounter[track] = 0; // Reset counter
    Serial.printf("[Loop] Track %d: %s (type=%d)\n", track, loopActive[track] ? "ACTIVE" : "INACTIVE", loopType[track]);
  }
}

void Sequencer::setLoopType(int track, LoopType type) {
  if (track >= 0 && track < MAX_TRACKS) {
    loopType[track] = type;
    loopStepCounter[track] = 0;
    Serial.printf("[Loop] Track %d type set to %d\n", track, type);
  }
}

LoopType Sequencer::getLoopType(int track) {
  if (track >= 0 && track < MAX_TRACKS) {
    return loopType[track];
  }
  return LOOP_EVERY_STEP;
}

void Sequencer::pauseLoop(int track) {
  if (track >= 0 && track < MAX_TRACKS) {
    if (loopActive[track]) {
      loopPaused[track] = !loopPaused[track];
      Serial.printf("[Loop] Track %d: %s\n", track, loopPaused[track] ? "PAUSED" : "RESUMED");
    }
  }
}

bool Sequencer::isLooping(int track) {
  if (track >= 0 && track < MAX_TRACKS) {
    return loopActive[track];
  }
  return false;
}

bool Sequencer::isLoopPaused(int track) {
  if (track >= 0 && track < MAX_TRACKS) {
    return loopPaused[track];
  }
  return false;
}

void Sequencer::processLoops() {
  // Process looped tracks every step
  for (int track = 0; track < MAX_TRACKS; track++) {
    if (loopActive[track] && !loopPaused[track] && !trackMuted[track]) {
      bool shouldTrigger = false;
      
      switch (loopType[track]) {
        case LOOP_EVERY_STEP:
          // Trigger on every step (16th note)
          shouldTrigger = true;
          break;
        case LOOP_EVERY_BEAT:
          // Trigger every 4 steps (quarter note)
          shouldTrigger = (loopStepCounter[track] % 4 == 0);
          break;
        case LOOP_HALF_BEAT:
          // Trigger every 2 steps (8th note)
          shouldTrigger = (loopStepCounter[track] % 2 == 0);
          break;
        case LOOP_ARRHYTHMIC:
          // Random trigger ~40% chance per step
          shouldTrigger = (random(100) < 40);
          break;
      }
      
      if (shouldTrigger && stepCallback != nullptr) {
        stepCallback(track, 100, trackVolume[track], 0);  // 0 = full note for loops
      }
      
      loopStepCounter[track] = (loopStepCounter[track] + 1) % 16;
    }
  }
}

