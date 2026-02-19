/*
 * Sequencer.h
 * Sequencer de 16 steps per Drum Machine (8 tracks)
 * (OPCIONAL - per afegir funcionalitat de sequencer)
 */

#ifndef SEQUENCER_H
#define SEQUENCER_H

#include <Arduino.h>

#define MAX_PATTERNS 128
#define STEPS_PER_PATTERN 16
#define MAX_TRACKS 16

// Loop types for pads
enum LoopType {
  LOOP_EVERY_STEP = 0,   // Trigger every step (16th note)
  LOOP_EVERY_BEAT = 1,   // Trigger every beat (4 steps = quarter note)
  LOOP_HALF_BEAT = 2,    // Trigger 2x per beat (8th note = every 2 steps)
  LOOP_ARRHYTHMIC = 3    // Random timing
};

class Sequencer {
public:
  Sequencer();
  ~Sequencer();
  
  // Control
  void start();
  void stop();
  void reset();
  bool isPlaying();
  
  // Timing
  void setTempo(float bpm);
  float getTempo();
  void update(); // Call from loop
  
  // Pattern editing
  void setStep(int track, int step, bool active, uint8_t velocity = 127);
  bool getStep(int track, int step);
  bool getStep(int pattern, int track, int step);  // Get step from specific pattern
  void clearPattern(int pattern);
  void clearPattern(); // Clear current pattern
  void clearTrack(int track);
  
  // Velocity editing per step
  void setStepVelocity(int track, int step, uint8_t velocity);
  void setStepVelocity(int pattern, int track, int step, uint8_t velocity);
  uint8_t getStepVelocity(int track, int step);
  uint8_t getStepVelocity(int pattern, int track, int step);
  
  // Note length per step (divider: 1=full, 2=half, 4=quarter, 8=eighth)
  void setStepNoteLen(int track, int step, uint8_t div);
  uint8_t getStepNoteLen(int track, int step);
  uint8_t getStepNoteLen(int pattern, int track, int step);
  
  // Bulk pattern writing (for reliable MIDI import)
  void setPatternBulk(int pattern, const bool stepsData[16][16], const uint8_t velsData[16][16]);
  
  // Pattern management
  void selectPattern(int pattern);
  int getCurrentPattern();
  void copyPattern(int src, int dst);
  
  // Mute tracks
  void muteTrack(int track, bool muted);
  bool isTrackMuted(int track);
  
  // Track volumes (0-150)
  void setTrackVolume(int track, uint8_t volume);
  uint8_t getTrackVolume(int track);
  
  // Song mode (auto-chain patterns)
  void setSongMode(bool enabled);
  bool isSongMode();
  void setSongLength(int length);
  int getSongLength();
  
  // Playback
  int getCurrentStep();
  
  // Loop system for live pads
  void toggleLoop(int track);
  void setLoopType(int track, LoopType type);
  LoopType getLoopType(int track);
  void pauseLoop(int track);
  bool isLooping(int track);
  bool isLoopPaused(int track);
  void processLoops(); // Called internally
  
  // Callbacks
  // noteLenSamples: 0 = full sample, >0 = cut after N samples (note length)
  typedef void (*StepCallback)(int track, uint8_t velocity, uint8_t trackVolume, uint32_t noteLenSamples);
  typedef void (*StepChangeCallback)(int newStep);
  typedef void (*PatternChangeCallback)(int newPattern, int songLength);
  void setStepCallback(StepCallback callback);
  void setStepChangeCallback(StepChangeCallback callback);
  void setPatternChangeCallback(PatternChangeCallback callback);
  
private:
  // Pattern data: [pattern][track][step]
  bool steps[MAX_PATTERNS][MAX_TRACKS][STEPS_PER_PATTERN];
  uint8_t velocities[MAX_PATTERNS][MAX_TRACKS][STEPS_PER_PATTERN];
  uint8_t noteLenDivs[MAX_PATTERNS][MAX_TRACKS][STEPS_PER_PATTERN]; // 1=full,2=half,4=quarter,8=eighth
  
  bool playing;
  int currentPattern;
  int currentStep;
  float tempo; // BPM
  uint32_t lastStepTime;
  uint32_t stepInterval; // microseconds
  bool trackMuted[MAX_TRACKS];
  uint8_t trackVolume[MAX_TRACKS]; // Volume per track (0-150)
  
  StepCallback stepCallback;
  StepChangeCallback stepChangeCallback;
  PatternChangeCallback patternChangeCallback;
  
  // Song mode
  bool songMode;
  int songLength; // 1-16 pattern count for song
  
  // Loop system
  bool loopActive[MAX_TRACKS];
  bool loopPaused[MAX_TRACKS];
  LoopType loopType[MAX_TRACKS];
  uint8_t loopStepCounter[MAX_TRACKS];
  
  void calculateStepInterval();
  void processStep();
};

#endif // SEQUENCER_H
