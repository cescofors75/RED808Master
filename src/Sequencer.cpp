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
  stepChangeCallback(nullptr) {
  
  // Initialize all patterns
  for (int p = 0; p < MAX_PATTERNS; p++) {
    for (int t = 0; t < MAX_TRACKS; t++) {
      for (int s = 0; s < STEPS_PER_PATTERN; s++) {
        steps[p][t][s] = false;
        velocities[p][t][s] = 127;
      }
      if (p == 0) {
        trackMuted[t] = false;
        loopActive[t] = false;
        loopPaused[t] = false;
      }
    }
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
  
  Serial.printf("Tempo set to %.1f BPM\n", tempo);
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
      
      // Call callback if set
      if (stepCallback != nullptr) {
        stepCallback(track, velocity);
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

void Sequencer::selectPattern(int pattern) {
  if (pattern < 0 || pattern >= MAX_PATTERNS) return;
  
  currentPattern = pattern;
  Serial.printf("Pattern %d selected\n", pattern);
}

void Sequencer::muteTrack(int track, bool muted) {
  if (track >= 0 && track < MAX_TRACKS) {
    trackMuted[track] = muted;
    Serial.printf("Track %d %s\n", track, muted ? "MUTED" : "UNMUTED");
  }
}

bool Sequencer::isTrackMuted(int track) {
  if (track >= 0 && track < MAX_TRACKS) {
    return trackMuted[track];
  }
  return false;
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

// ============= LOOP SYSTEM =============

void Sequencer::toggleLoop(int track) {
  if (track >= 0 && track < MAX_TRACKS) {
    loopActive[track] = !loopActive[track];
    loopPaused[track] = false; // Reset pause state
    Serial.printf("[Loop] Track %d: %s\n", track, loopActive[track] ? "ACTIVE" : "INACTIVE");
  }
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
      if (stepCallback != nullptr) {
        stepCallback(track, 100); // Loop triggers at consistent velocity
      }
    }
  }
}

