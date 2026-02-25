/*
 * protocol.h
 * RED808 SPI Protocol — Shared command definitions
 * ESP32-S3 (Master) ↔ STM32 (Slave)
 */

#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>

// ═══════════════════════════════════════════════════════
// SPI PACKET STRUCTURE
// ═══════════════════════════════════════════════════════

#define SPI_MAGIC_CMD      0xA5   // Command from Master
#define SPI_MAGIC_RESP     0x5A   // Response from Slave
#define SPI_MAGIC_SAMPLE   0xDA   // Sample data transfer
#define SPI_MAGIC_BULK     0xBB   // Bulk multi-command

#define SPI_MAX_PAYLOAD    528    // 8 header + 520 max payload

// Packet header (8 bytes, packed)
typedef struct __attribute__((packed)) {
    uint8_t  magic;       // SPI_MAGIC_CMD / SPI_MAGIC_RESP / SPI_MAGIC_SAMPLE
    uint8_t  cmd;         // Command code
    uint16_t length;      // Payload length in bytes
    uint16_t sequence;    // Sequence number for verification
    uint16_t checksum;    // CRC16 of payload
} SPIPacketHeader;

// ═══════════════════════════════════════════════════════
// COMMANDS: TRIGGER (0x01 - 0x0F)
// ═══════════════════════════════════════════════════════
#define CMD_TRIGGER_SEQ       0x01  // Trigger from sequencer
#define CMD_TRIGGER_LIVE      0x02  // Trigger from live pad
#define CMD_TRIGGER_STOP      0x03  // Stop specific sample
#define CMD_TRIGGER_STOP_ALL  0x04  // Stop all samples
#define CMD_TRIGGER_SIDECHAIN 0x05  // Trigger sidechain envelope

// ═══════════════════════════════════════════════════════
// COMMANDS: VOLUME (0x10 - 0x1F)
// ═══════════════════════════════════════════════════════
#define CMD_MASTER_VOLUME     0x10  // Master volume (0-100)
#define CMD_SEQ_VOLUME        0x11  // Sequencer volume (0-150)
#define CMD_LIVE_VOLUME       0x12  // Live pad volume (0-180)
#define CMD_TRACK_VOLUME      0x13  // Per-track volume (0-150)
#define CMD_LIVE_PITCH        0x14  // Live pitch shift

// ═══════════════════════════════════════════════════════
// COMMANDS: GLOBAL FILTER (0x20 - 0x2F)
// ═══════════════════════════════════════════════════════
#define CMD_FILTER_SET        0x20  // Set global filter (full params)
#define CMD_FILTER_CUTOFF     0x21  // Cutoff frequency
#define CMD_FILTER_RESONANCE  0x22  // Resonance (Q)
#define CMD_FILTER_BITDEPTH   0x23  // Bit crush
#define CMD_FILTER_DISTORTION 0x24  // Global distortion amount
#define CMD_FILTER_DIST_MODE  0x25  // Distortion mode (soft/hard/tube/fuzz)
#define CMD_FILTER_SR_REDUCE  0x26  // Sample rate reduction

// ═══════════════════════════════════════════════════════
// COMMANDS: MASTER EFFECTS (0x30 - 0x4F)
// ═══════════════════════════════════════════════════════
#define CMD_DELAY_ACTIVE      0x30
#define CMD_DELAY_TIME        0x31
#define CMD_DELAY_FEEDBACK    0x32
#define CMD_DELAY_MIX         0x33

#define CMD_PHASER_ACTIVE     0x34
#define CMD_PHASER_RATE       0x35
#define CMD_PHASER_DEPTH      0x36
#define CMD_PHASER_FEEDBACK   0x37

#define CMD_FLANGER_ACTIVE    0x38
#define CMD_FLANGER_RATE      0x39
#define CMD_FLANGER_DEPTH     0x3A
#define CMD_FLANGER_FEEDBACK  0x3B
#define CMD_FLANGER_MIX       0x3C

#define CMD_COMP_ACTIVE       0x3D
#define CMD_COMP_THRESHOLD    0x3E
#define CMD_COMP_RATIO        0x3F
#define CMD_COMP_ATTACK       0x40
#define CMD_COMP_RELEASE      0x41
#define CMD_COMP_MAKEUP       0x42

// ═══════════════════════════════════════════════════════
// COMMANDS: REVERB (0x43 - 0x46)
// ═══════════════════════════════════════════════════════
#define CMD_REVERB_ACTIVE     0x43  // Master reverb on/off
#define CMD_REVERB_FEEDBACK   0x44  // Room size / decay (0.0-1.0)
#define CMD_REVERB_LPFREQ     0x45  // Damp: LP filter Hz (200-12000)
#define CMD_REVERB_MIX        0x46  // Dry/Wet mix (0.0-1.0)

// ═══════════════════════════════════════════════════════
// COMMANDS: CHORUS (0x47 - 0x4A)
// ═══════════════════════════════════════════════════════
#define CMD_CHORUS_ACTIVE     0x47  // Master chorus on/off
#define CMD_CHORUS_RATE       0x48  // LFO rate Hz (0.1-10.0)
#define CMD_CHORUS_DEPTH      0x49  // LFO depth (0.0-1.0)
#define CMD_CHORUS_MIX        0x4A  // Dry/Wet mix (0.0-1.0)

// ═══════════════════════════════════════════════════════
// COMMANDS: TREMOLO (0x4B - 0x4D)
// ═══════════════════════════════════════════════════════
#define CMD_TREMOLO_ACTIVE    0x4B  // Master tremolo on/off
#define CMD_TREMOLO_RATE      0x4C  // Rate Hz (0.1-20.0)
#define CMD_TREMOLO_DEPTH     0x4D  // Depth (0.0-1.0)

// ═══════════════════════════════════════════════════════
// COMMANDS: WAVEFOLDER + LIMITER (0x4E - 0x4F)
// ═══════════════════════════════════════════════════════
#define CMD_WAVEFOLDER_GAIN   0x4E  // WaveFolder input gain (1.0-10.0)
#define CMD_LIMITER_ACTIVE    0x4F  // Brick-wall limiter on/off (threshold = 0dBFS)

// ═══════════════════════════════════════════════════════
// COMMANDS: PER-TRACK FX (0x50 - 0x6F)
// ═══════════════════════════════════════════════════════
#define CMD_TRACK_FILTER      0x50  // Per-track filter
#define CMD_TRACK_CLEAR_FILTER 0x51 // Clear track filter
#define CMD_TRACK_DISTORTION  0x52  // Per-track distortion
#define CMD_TRACK_BITCRUSH    0x53  // Per-track bitcrush
#define CMD_TRACK_ECHO        0x54  // Per-track echo
#define CMD_TRACK_FLANGER_FX  0x55  // Per-track flanger
#define CMD_TRACK_COMPRESSOR  0x56  // Per-track compressor
#define CMD_TRACK_CLEAR_LIVE  0x57  // Clear per-track live FX
#define CMD_TRACK_CLEAR_FX    0x58  // Clear all per-track FX

// Per-track FX send levels + mixer
#define CMD_TRACK_REVERB_SEND 0x59  // Per-track reverb send (0-100)
#define CMD_TRACK_DELAY_SEND  0x5A  // Per-track delay send (0-100)
#define CMD_TRACK_CHORUS_SEND 0x5B  // Per-track chorus send (0-100)
#define CMD_TRACK_PAN         0x5C  // Per-track pan (-100 L .. 0 C .. +100 R)
#define CMD_TRACK_MUTE        0x5D  // Per-track mute (0/1)
#define CMD_TRACK_SOLO        0x5E  // Per-track solo (0/1)
#define CMD_TRACK_PHASER      0x5F  // Per-track phaser
#define CMD_TRACK_TREMOLO     0x60  // Per-track tremolo
#define CMD_TRACK_PITCH       0x61  // Per-track pitch shift (cents)
#define CMD_TRACK_GATE        0x62  // Per-track noise gate
#define CMD_TRACK_EQ_LOW      0x63  // Per-track 3-band EQ low  (-12..+12 dB)
#define CMD_TRACK_EQ_MID      0x64  // Per-track 3-band EQ mid  (-12..+12 dB)
#define CMD_TRACK_EQ_HIGH     0x65  // Per-track 3-band EQ high (-12..+12 dB)

// ═══════════════════════════════════════════════════════
// COMMANDS: PER-PAD FX (0x70 - 0x8F)
// ═══════════════════════════════════════════════════════
#define CMD_PAD_FILTER        0x70  // Per-pad filter
#define CMD_PAD_CLEAR_FILTER  0x71  // Clear pad filter
#define CMD_PAD_DISTORTION    0x72  // Per-pad distortion
#define CMD_PAD_BITCRUSH      0x73  // Per-pad bitcrush
#define CMD_PAD_LOOP          0x74  // Pad continuous loop
#define CMD_PAD_REVERSE       0x75  // Reverse sample
#define CMD_PAD_PITCH         0x76  // Per-pad pitch shift
#define CMD_PAD_STUTTER       0x77  // Stutter effect
#define CMD_PAD_SCRATCH       0x78  // Vinyl scratch
#define CMD_PAD_TURNTABLISM   0x79  // DJ turntablism
#define CMD_PAD_CLEAR_FX      0x7A  // Clear all pad FX

// ═══════════════════════════════════════════════════════
// COMMANDS: SIDECHAIN (0x90 - 0x9F)
// ═══════════════════════════════════════════════════════
#define CMD_SIDECHAIN_SET     0x90  // Configure sidechain
#define CMD_SIDECHAIN_CLEAR   0x91  // Deactivate sidechain

// ═══════════════════════════════════════════════════════
// COMMANDS: SAMPLE TRANSFER (0xA0 - 0xAF)
// ═══════════════════════════════════════════════════════
#define CMD_SAMPLE_BEGIN      0xA0  // Begin sample transfer
#define CMD_SAMPLE_DATA       0xA1  // Sample data chunk
#define CMD_SAMPLE_END        0xA2  // End sample transfer
#define CMD_SAMPLE_UNLOAD     0xA3  // Unload one sample
#define CMD_SAMPLE_UNLOAD_ALL 0xA4  // Unload all samples

// ═══════════════════════════════════════════════════════
// COMMANDS: DAISY SD FILE SYSTEM (0xB0 - 0xBF)
//   Permite al ESP32 explorar y cargar samples desde
//   la SD card conectada al SPI2 de la Daisy Seed.
//   La Daisy carga directo a SDRAM — mucho más rápido
//   que transferir PCM por SPI.
// ═══════════════════════════════════════════════════════
#define CMD_SD_LIST_FOLDERS   0xB0  // List root-level folders (kit dirs)
#define CMD_SD_LIST_FILES     0xB1  // List WAV files in a folder
#define CMD_SD_FILE_INFO      0xB2  // Get sample info (length, SR, bits)
#define CMD_SD_LOAD_SAMPLE    0xB3  // Load one WAV from SD → SDRAM pad slot
#define CMD_SD_LOAD_KIT       0xB4  // Load entire kit folder → all pads
#define CMD_SD_KIT_LIST       0xB5  // Get list of available kit names
#define CMD_SD_STATUS         0xB6  // SD card status (present, size, free)
#define CMD_SD_UNLOAD_KIT     0xB7  // Unload current kit from SDRAM
#define CMD_SD_GET_LOADED     0xB8  // Get currently loaded kit name + pad map
#define CMD_SD_ABORT          0xB9  // Abort ongoing SD load operation

// ═══════════════════════════════════════════════════════
// COMMANDS: STATUS / QUERY (0xE0 - 0xEF)
// ═══════════════════════════════════════════════════════
#define CMD_GET_STATUS        0xE0  // Get general status
#define CMD_GET_PEAKS         0xE1  // Get VU meters (16 tracks + master)
#define CMD_GET_CPU_LOAD      0xE2  // Get CPU load
#define CMD_GET_VOICES        0xE3  // Get active voices
#define CMD_PING              0xEE  // Ping/Pong
#define CMD_RESET             0xEF  // Full DSP reset

// ═══════════════════════════════════════════════════════
// COMMANDS: BULK (0xF0 - 0xFF)
// ═══════════════════════════════════════════════════════
#define CMD_BULK_TRIGGERS     0xF0  // Multiple triggers in one packet
#define CMD_BULK_FX           0xF1  // Multiple FX changes

// ═══════════════════════════════════════════════════════
// PAYLOAD STRUCTURES
// ═══════════════════════════════════════════════════════

// --- Triggers ---
typedef struct __attribute__((packed)) {
    uint8_t  padIndex;       // 0-15 (sequencer track)
    uint8_t  velocity;       // 1-127
    uint8_t  trackVolume;    // 0-150
    uint8_t  reserved;
    uint32_t maxSamples;     // 0 = full sample, >0 = cut after N samples
} TriggerSeqPayload;

typedef struct __attribute__((packed)) {
    uint8_t  padIndex;       // 0-23 (16 seq + 8 XTRA)
    uint8_t  velocity;       // 1-127
} TriggerLivePayload;

typedef struct __attribute__((packed)) {
    uint8_t  count;          // Number of triggers (1-16)
    uint8_t  reserved;
    TriggerSeqPayload triggers[16];
} BulkTriggersPayload;

// --- Volume ---
typedef struct __attribute__((packed)) {
    uint8_t  volume;         // 0-150
} VolumePayload;

typedef struct __attribute__((packed)) {
    uint8_t  track;          // 0-15
    uint8_t  volume;         // 0-150
} TrackVolumePayload;

typedef struct __attribute__((packed)) {
    float    pitch;          // 0.25 - 3.0
} PitchPayload;

// --- Global Filter ---
typedef struct __attribute__((packed)) {
    uint8_t  filterType;     // enum FilterType (0-14)
    uint8_t  distMode;       // enum DistortionMode (0-3)
    uint8_t  bitDepth;       // 1-16
    uint8_t  reserved;
    float    cutoff;         // Hz (20.0 - 20000.0)
    float    resonance;      // Q (0.1 - 30.0)
    float    distortion;     // 0.0 - 100.0
    uint32_t sampleRateReduce; // Hz (for SR reduction)
} GlobalFilterPayload;

// --- Single float param (for individual FX params) ---
typedef struct __attribute__((packed)) {
    float    value;
} FloatPayload;

// --- Single bool param ---
typedef struct __attribute__((packed)) {
    uint8_t  active;         // 0/1
} BoolPayload;

// --- Single uint32 param ---
typedef struct __attribute__((packed)) {
    uint32_t value;
} Uint32Payload;

// --- Per-Track Filter ---
typedef struct __attribute__((packed)) {
    uint8_t  track;          // 0-15
    uint8_t  filterType;     // enum FilterType
    uint8_t  reserved[2];
    float    cutoff;         // Hz
    float    resonance;      // Q
    float    gain;           // dB (for peaking/shelf)
} TrackFilterPayload;

// --- Per-Track Echo ---
typedef struct __attribute__((packed)) {
    uint8_t  track;          // 0-15
    uint8_t  active;         // 0/1
    uint8_t  reserved[2];
    float    time;           // ms (10-200)
    float    feedback;       // 0.0-90.0 (%)
    float    mix;            // 0.0-100.0 (%)
} TrackEchoPayload;

// --- Per-Track Flanger ---
typedef struct __attribute__((packed)) {
    uint8_t  track;          // 0-15
    uint8_t  active;         // 0/1
    uint8_t  reserved[2];
    float    rate;           // 0.0-100.0 (%)
    float    depth;          // 0.0-100.0 (%)
    float    feedback;       // 0.0-100.0 (%)
} TrackFlangerPayload;

// --- Per-Track Compressor ---
typedef struct __attribute__((packed)) {
    uint8_t  track;          // 0-15
    uint8_t  active;         // 0/1
    uint8_t  reserved[2];
    float    threshold;      // dB
    float    ratio;          // 1.0-20.0
} TrackCompressorPayload;

// --- Per-Pad Filter ---
typedef struct __attribute__((packed)) {
    uint8_t  pad;            // 0-23
    uint8_t  filterType;     // enum FilterType
    uint8_t  reserved[2];
    float    cutoff;         // Hz
    float    resonance;      // Q
    float    gain;           // dB
} PadFilterPayload;

// --- Per-Pad Distortion ---
typedef struct __attribute__((packed)) {
    uint8_t  pad;            // 0-23
    uint8_t  distMode;       // enum DistortionMode
    uint8_t  reserved[2];
    float    amount;         // 0.0-100.0
} PadDistortionPayload;

// --- Per-Pad BitCrush ---
typedef struct __attribute__((packed)) {
    uint8_t  pad;            // 0-23
    uint8_t  bits;           // 1-16
} PadBitCrushPayload;

// --- Per-Pad Loop ---
typedef struct __attribute__((packed)) {
    uint8_t  pad;            // 0-23
    uint8_t  enabled;        // 0/1
} PadLoopPayload;

// --- Per-Pad Reverse ---
typedef struct __attribute__((packed)) {
    uint8_t  pad;            // 0-23
    uint8_t  reverse;        // 0/1
} PadReversePayload;

// --- Per-Pad Pitch ---
typedef struct __attribute__((packed)) {
    uint8_t  pad;            // 0-23
    uint8_t  reserved;
    uint16_t reserved2;
    float    pitch;          // 0.25-3.0
} PadPitchPayload;

// --- Per-Pad Stutter ---
typedef struct __attribute__((packed)) {
    uint8_t  pad;            // 0-23
    uint8_t  active;         // 0/1
    uint16_t intervalMs;     // ms
} PadStutterPayload;

// --- Per-Pad Scratch ---
typedef struct __attribute__((packed)) {
    uint8_t  pad;            // 0-23
    uint8_t  active;         // 0/1
    uint8_t  reserved[2];
    float    rate;           // Hz (0.5-20.0)
    float    depth;          // 0.0-1.0
    float    filterCutoff;   // Hz (500-12000)
    float    crackle;        // 0.0-1.0
} PadScratchPayload;

// --- Per-Pad Turntablism ---
typedef struct __attribute__((packed)) {
    uint8_t  pad;            // 0-23
    uint8_t  active;         // 0/1
    uint8_t  autoMode;       // 0/1
    int8_t   mode;           // -1=auto, 0-3=manual
    uint16_t brakeMs;
    uint16_t backspinMs;
    float    transformRate;  // Hz
    float    vinylNoise;     // 0.0-1.0
} PadTurntablismPayload;

// --- Sidechain ---
typedef struct __attribute__((packed)) {
    uint8_t  active;         // 0/1
    uint8_t  sourceTrack;    // 0-15
    uint16_t destMask;       // Bitmask of destination tracks
    float    amount;         // 0.0-1.0
    float    attackMs;       // 0.1-80.0 ms
    float    releaseMs;      // 10.0-1200.0 ms
    float    knee;           // 0.0-1.0
} SidechainPayload;

// --- Sidechain Trigger ---
typedef struct __attribute__((packed)) {
    uint8_t  sourceTrack;    // 0-15  (which track triggers the ducking)
    uint8_t  reserved;
} SidechainTriggerPayload;

// --- Reverb ---
typedef struct __attribute__((packed)) {
    uint8_t  active;         // 0/1
    uint8_t  reserved[3];
    float    feedback;       // 0.0-1.0 (room size / decay time)
    float    lpFreq;         // Hz (200-12000, damp color)
    float    mix;            // 0.0-1.0 (dry/wet)
} ReverbPayload;

// --- Chorus ---
typedef struct __attribute__((packed)) {
    uint8_t  active;         // 0/1
    uint8_t  reserved[3];
    float    rate;           // Hz (0.1-10.0)
    float    depth;          // 0.0-1.0
    float    mix;            // 0.0-1.0
} ChorusPayload;

// --- Tremolo ---
typedef struct __attribute__((packed)) {
    uint8_t  active;         // 0/1
    uint8_t  reserved[3];
    float    rate;           // Hz (0.1-20.0)
    float    depth;          // 0.0-1.0
} TremoloPayload;

// --- Per-Track FX Send ---
typedef struct __attribute__((packed)) {
    uint8_t  track;          // 0-15
    uint8_t  sendLevel;      // 0-100 (percentage)
} TrackSendPayload;

// --- Per-Track Pan ---
typedef struct __attribute__((packed)) {
    uint8_t  track;          // 0-15
    int8_t   pan;            // -100 (full L) .. 0 (center) .. +100 (full R)
} TrackPanPayload;

// --- Per-Track Mute/Solo ---
typedef struct __attribute__((packed)) {
    uint8_t  track;          // 0-15
    uint8_t  enabled;        // 0/1
} TrackMuteSoloPayload;

// --- Per-Track 3-band EQ ---
typedef struct __attribute__((packed)) {
    uint8_t  track;          // 0-15
    int8_t   gainLow;        // -12..+12 dB
    int8_t   gainMid;        // -12..+12 dB
    int8_t   gainHigh;       // -12..+12 dB
} TrackEqPayload;

// --- Per-Track Gate ---
typedef struct __attribute__((packed)) {
    uint8_t  track;          // 0-15
    uint8_t  active;         // 0/1
    uint8_t  reserved[2];
    float    threshold;      // dB (-60..-6)
    float    attackMs;       // 0.1-50
    float    releaseMs;      // 10-500
} TrackGatePayload;

// --- SD: List Folders Request (no payload, response below) ---
// --- SD: List Files Request ---
typedef struct __attribute__((packed)) {
    char     folderName[32]; // null-terminated folder name
} SdListFilesPayload;

// --- SD: File Info Request ---
typedef struct __attribute__((packed)) {
    uint8_t  padIndex;       // target pad slot
    uint8_t  reserved[3];
    char     folderName[32]; // kit folder
    char     fileName[32];   // WAV filename
} SdFileInfoPayload;

// --- SD: Load Sample ---
typedef struct __attribute__((packed)) {
    uint8_t  padIndex;       // 0-23 destination pad slot
    uint8_t  reserved[3];
    char     folderName[32]; // kit folder on SD
    char     fileName[32];   // WAV file name
} SdLoadSamplePayload;

// --- SD: Load Kit (all WAVs in a folder) ---
typedef struct __attribute__((packed)) {
    char     kitName[32];    // folder name = kit name
    uint8_t  startPad;       // first pad slot to fill (usually 0)
    uint8_t  maxPads;        // max pads to load (usually 16 or 24)
    uint8_t  reserved[2];
} SdLoadKitPayload;

// --- SD: Folder List Response (from Daisy → ESP32) ---
typedef struct __attribute__((packed)) {
    uint8_t  count;          // number of folders (max 16)
    uint8_t  reserved[3];
    char     names[16][32];  // up to 16 folder names, null-terminated
} SdFolderListResponse;

// --- SD: File List Response ---
typedef struct __attribute__((packed)) {
    uint8_t  count;          // number of files (max 24)
    uint8_t  reserved[3];
    struct __attribute__((packed)) {
        char     name[24];   // filename (truncated to 24 chars)
        uint32_t sizeBytes;  // file size in bytes
    } files[24];
} SdFileListResponse;

// --- SD: File Info Response ---
typedef struct __attribute__((packed)) {
    uint32_t totalSamples;   // number of int16_t samples
    uint16_t sampleRate;     // Hz (44100, 48000...)
    uint8_t  bitsPerSample;  // 16
    uint8_t  channels;       // 1=mono, 2=stereo
    uint32_t durationMs;     // duration in milliseconds
    char     name[32];       // filename
} SdFileInfoResponse;

// --- SD: Status Response ---
typedef struct __attribute__((packed)) {
    uint8_t  present;        // 0=no SD, 1=SD present
    uint8_t  reserved[3];
    uint32_t totalMB;        // total capacity in MB
    uint32_t freeMB;         // free space in MB
    uint32_t samplesLoaded;  // bitmask of loaded pad slots
    char     currentKit[32]; // currently loaded kit name
} SdStatusResponse;

// --- SD: Kit List Response ---
typedef struct __attribute__((packed)) {
    uint8_t  count;          // number of kits found
    uint8_t  reserved[3];
    char     kits[16][32];   // up to 16 kit names
} SdKitListResponse;

// --- CPU Load Response (CMD_GET_CPU_LOAD 0xE2) ---
typedef struct __attribute__((packed)) {
    float    cpuLoad;        // 0.0-100.0 percent
    uint32_t uptime;         // milliseconds since boot
} CpuLoadResponse;

// --- Active Voices Response (CMD_GET_VOICES 0xE3) ---
typedef struct __attribute__((packed)) {
    uint8_t  activeVoices;   // 0-MAX_VOICES
    uint8_t  reserved[3];
} VoicesResponse;

// --- Sample Transfer ---
typedef struct __attribute__((packed)) {
    uint8_t  padIndex;       // 0-23
    uint8_t  bitsPerSample;  // 16
    uint16_t sampleRate;     // 44100
    uint32_t totalBytes;     // Total PCM data size
    uint32_t totalSamples;   // Total int16_t samples
} SampleBeginPayload;

typedef struct __attribute__((packed)) {
    uint8_t  padIndex;       // 0-23
    uint8_t  reserved;
    uint16_t chunkSize;      // Bytes in this chunk (max 512)
    uint32_t offset;         // Byte offset from start
    // int16_t data[] follows (up to 256 samples = 512 bytes)
} SampleDataHeader;

typedef struct __attribute__((packed)) {
    uint8_t  padIndex;       // 0-23
    uint8_t  status;         // 0=OK, 1=error
    uint16_t reserved;
    uint32_t checksum;       // CRC32 of all data
} SampleEndPayload;

typedef struct __attribute__((packed)) {
    uint8_t  padIndex;       // 0-23
} SampleUnloadPayload;

// --- Status Responses ---
typedef struct __attribute__((packed)) {
    float trackPeaks[16];    // 0.0-1.0 per track
    float masterPeak;        // 0.0-1.0 master
} PeaksResponse;

typedef struct __attribute__((packed)) {
    uint8_t  activeVoices;
    uint8_t  cpuLoadPercent;
    uint16_t freeSRAM;       // KB free
    uint32_t samplesLoaded;  // Bitmask of loaded pads
    uint32_t uptime;         // Seconds since boot
    uint16_t spiErrors;
    uint16_t bufferUnderruns;
} StatusResponse;

typedef struct __attribute__((packed)) {
    uint32_t timestamp;      // millis() from ESP32
} PingPayload;

typedef struct __attribute__((packed)) {
    uint32_t echoTimestamp;  // Same timestamp returned
    uint32_t stm32Uptime;   // millis() from STM32
} PongResponse;

// ═══════════════════════════════════════════════════════
// FILTER & DISTORTION ENUMS (shared between ESP32 & STM32)
// ═══════════════════════════════════════════════════════

// Filter types
#define FTYPE_NONE         0
#define FTYPE_LOWPASS      1
#define FTYPE_HIGHPASS     2
#define FTYPE_BANDPASS     3
#define FTYPE_NOTCH        4
#define FTYPE_ALLPASS      5
#define FTYPE_PEAKING      6
#define FTYPE_LOWSHELF     7
#define FTYPE_HIGHSHELF    8
#define FTYPE_RESONANT     9
#define FTYPE_SCRATCH      10
#define FTYPE_TURNTABLISM  11
#define FTYPE_REVERSE      12
#define FTYPE_HALFSPEED    13
#define FTYPE_STUTTER      14

// Distortion modes
#define DMODE_SOFT   0
#define DMODE_HARD   1
#define DMODE_TUBE   2
#define DMODE_FUZZ   3

// Filter preset structure (for ESP32 side UI)
typedef struct {
    uint8_t type;
    float cutoff;
    float resonance;
    float gain;
    const char* name;
} FilterPresetInfo;

#endif // PROTOCOL_H
