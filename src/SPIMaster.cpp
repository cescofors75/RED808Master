/*
 * SPIMaster.cpp
 * RED808 SPI Master — Full implementation
 * Sends commands to STM32 Audio Slave via SPI
 */

#include "SPIMaster.h"

// ═══════════════════════════════════════════════════════
// FILTER PRESETS (static, for UI compatibility)
// ═══════════════════════════════════════════════════════
static const FilterPreset filterPresets[] = {
    {FILTER_NONE,       0,    0,    0, "None"},
    {FILTER_LOWPASS,    1000, 1.0f, 0, "Low Pass"},
    {FILTER_HIGHPASS,   1000, 1.0f, 0, "High Pass"},
    {FILTER_BANDPASS,   1000, 1.0f, 0, "Band Pass"},
    {FILTER_NOTCH,      1000, 1.0f, 0, "Notch"},
    {FILTER_ALLPASS,    1000, 1.0f, 0, "All Pass"},
    {FILTER_PEAKING,    1000, 1.0f, 6, "Peaking EQ"},
    {FILTER_LOWSHELF,   200,  0.7f, 6, "Low Shelf"},
    {FILTER_HIGHSHELF,  4000, 0.7f, 6, "High Shelf"},
    {FILTER_RESONANT,   800,  8.0f, 0, "Resonant"},
    {FILTER_SCRATCH,    4000, 1.0f, 0, "Scratch"},
    {FILTER_TURNTABLISM,4000, 1.0f, 0, "Turntablism"},
    {FILTER_REVERSE,    0,    0,    0, "Reverse"},
    {FILTER_HALFSPEED,  0,    0,    0, "Half Speed"},
    {FILTER_STUTTER,    0,    0,    0, "Stutter"}
};

// ═══════════════════════════════════════════════════════
// CONSTRUCTOR / DESTRUCTOR
// ═══════════════════════════════════════════════════════

SPIMaster::SPIMaster() : spi(nullptr), seqNumber(0), spiErrorCount(0), stm32Connected(false) {
    // Initialize cached state
    cachedMasterVolume = 100;
    cachedSeqVolume = 10;
    cachedLiveVolume = 80;
    cachedLivePitch = 1.0f;
    
    for (int i = 0; i < MAX_AUDIO_TRACKS; i++) {
        cachedTrackFilter[i] = FILTER_NONE;
        trackFilterActive[i] = false;
        cachedTrackEchoActive[i] = false;
        cachedTrackFlangerActive[i] = false;
        cachedTrackCompActive[i] = false;
        cachedTrackPeaks[i] = 0.0f;
    }
    
    for (int i = 0; i < MAX_PADS; i++) {
        cachedPadFilter[i] = FILTER_NONE;
        padFilterActive[i] = false;
        cachedPadLoop[i] = false;
    }
    
    cachedMasterPeak = 0.0f;
    lastPeakRequest = 0;
    cachedActiveVoices = 0;
    cachedCpuLoad = 0.0f;
    
    memset(txBuffer, 0, sizeof(txBuffer));
    memset(rxBuffer, 0, sizeof(rxBuffer));
}

SPIMaster::~SPIMaster() {
    if (spi) {
        spi->end();
        delete spi;
        spi = nullptr;
    }
}

// ═══════════════════════════════════════════════════════
// INITIALIZATION
// ═══════════════════════════════════════════════════════

bool SPIMaster::begin() {
    // Configure control pins
    pinMode(STM32_SPI_CS, OUTPUT);
    digitalWrite(STM32_SPI_CS, HIGH);   // CS idle high
    
#ifdef USE_SPI_SYNC_IRQ
    pinMode(STM32_SPI_SYNC, OUTPUT);
    pinMode(STM32_SPI_IRQ, INPUT_PULLUP);
    digitalWrite(STM32_SPI_SYNC, LOW);  // SYNC idle low
#endif
    
    // Initialize HSPI (separate from PSRAM which uses SPI0)
    spi = new SPIClass(HSPI);
    spi->begin(STM32_SPI_SCK, STM32_SPI_MISO, STM32_SPI_MOSI, STM32_SPI_CS);
    
    Serial.println("[SPI] Master initialized on HSPI (4-wire mode)");
    Serial.printf("[SPI] Pins: MOSI=%d MISO=%d SCK=%d CS=%d\n",
                  STM32_SPI_MOSI, STM32_SPI_MISO, STM32_SPI_SCK, STM32_SPI_CS);
#ifdef USE_SPI_SYNC_IRQ
    Serial.printf("[SPI] SYNC=%d IRQ=%d\n", STM32_SPI_SYNC, STM32_SPI_IRQ);
#endif
    
    // Try to connect to STM32
    uint32_t rtt;
    for (int attempt = 0; attempt < 5; attempt++) {
        if (ping(rtt)) {
            stm32Connected = true;
            Serial.printf("[SPI] STM32 connected! RTT: %d us\n", rtt);
            return true;
        }
        delay(200);
        Serial.printf("[SPI] Ping attempt %d/5...\n", attempt + 1);
    }
    
    Serial.println("[SPI] WARNING: STM32 not responding - will retry in background");
    // Don't fail — allow system to boot and retry later
    return true;
}

// ═══════════════════════════════════════════════════════
// SPI LOW-LEVEL
// ═══════════════════════════════════════════════════════

void SPIMaster::csLow() {
    digitalWrite(STM32_SPI_CS, LOW);
}

void SPIMaster::csHigh() {
    digitalWrite(STM32_SPI_CS, HIGH);
}

void SPIMaster::syncPulse() {
#ifdef USE_SPI_SYNC_IRQ
    digitalWrite(STM32_SPI_SYNC, HIGH);
    delayMicroseconds(2);
    digitalWrite(STM32_SPI_SYNC, LOW);
#else
    // Sin SYNC: pequeño delay para que STM32 procese
    delayMicroseconds(5);
#endif
}

uint16_t SPIMaster::crc16(const uint8_t* data, uint16_t len) {
    uint16_t crc = 0xFFFF;
    for (uint16_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 1) crc = (crc >> 1) ^ 0xA001;
            else crc >>= 1;
        }
    }
    return crc;
}

bool SPIMaster::sendCommand(uint8_t cmd, const void* payload, uint16_t payloadLen) {
    SPIPacketHeader header;
    header.magic = SPI_MAGIC_CMD;
    header.cmd = cmd;
    header.length = payloadLen;
    header.sequence = seqNumber++;
    header.checksum = (payload && payloadLen > 0) ? crc16((const uint8_t*)payload, payloadLen) : 0;
    
    csLow();
    spi->beginTransaction(SPISettings(STM32_SPI_CLOCK, MSBFIRST, SPI_MODE0));
    
    // Send header (8 bytes)
    spi->transferBytes((uint8_t*)&header, nullptr, sizeof(SPIPacketHeader));
    
    // Send payload if present
    if (payload && payloadLen > 0) {
        spi->transferBytes((const uint8_t*)payload, nullptr, payloadLen);
    }
    
    spi->endTransaction();
    csHigh();
    
    // SYNC pulse to notify STM32
    syncPulse();
    
    return true;
}

bool SPIMaster::sendAndReceive(uint8_t cmd, const void* payload, uint16_t payloadLen,
                                void* response, uint16_t responseLen) {
    // Send command
    SPIPacketHeader header;
    header.magic = SPI_MAGIC_CMD;
    header.cmd = cmd;
    header.length = payloadLen;
    header.sequence = seqNumber++;
    header.checksum = (payload && payloadLen > 0) ? crc16((const uint8_t*)payload, payloadLen) : 0;
    
    csLow();
    spi->beginTransaction(SPISettings(STM32_SPI_CLOCK, MSBFIRST, SPI_MODE0));
    
    // Send header
    spi->transferBytes((uint8_t*)&header, nullptr, sizeof(SPIPacketHeader));
    
    // Send payload
    if (payload && payloadLen > 0) {
        spi->transferBytes((const uint8_t*)payload, nullptr, payloadLen);
    }
    
    // Small delay for STM32 to prepare response
    delayMicroseconds(10);
    
    // Read response header
    SPIPacketHeader respHeader;
    memset(&respHeader, 0, sizeof(respHeader));
    spi->transferBytes(nullptr, (uint8_t*)&respHeader, sizeof(SPIPacketHeader));
    
    bool success = false;
    if (respHeader.magic == SPI_MAGIC_RESP && respHeader.length <= responseLen) {
        // Read response payload
        if (respHeader.length > 0 && response) {
            spi->transferBytes(nullptr, (uint8_t*)response, respHeader.length);
        }
        success = true;
    } else {
        spiErrorCount++;
    }
    
    spi->endTransaction();
    csHigh();
    
    return success;
}

// ═══════════════════════════════════════════════════════
// PROCESS (called from task loop)
// ═══════════════════════════════════════════════════════

void SPIMaster::process() {
    // Poll peaks every 50ms
    if (millis() - lastPeakRequest > 50) {
        requestPeaks();
        lastPeakRequest = millis();
    }
    
    // Retry connection if not connected
    if (!stm32Connected) {
        static uint32_t lastRetry = 0;
        if (millis() - lastRetry > 5000) {
            uint32_t rtt;
            if (ping(rtt)) {
                stm32Connected = true;
                Serial.printf("[SPI] STM32 reconnected! RTT: %d us\n", rtt);
            }
            lastRetry = millis();
        }
    }
}

// ═══════════════════════════════════════════════════════
// TRIGGERS
// ═══════════════════════════════════════════════════════

void SPIMaster::triggerSampleSequencer(int padIndex, uint8_t velocity, uint8_t trackVolume, uint32_t maxSamples) {
    if (padIndex < 0 || padIndex >= MAX_PADS) return;
    
    TriggerSeqPayload payload;
    payload.padIndex = (uint8_t)padIndex;
    payload.velocity = velocity;
    payload.trackVolume = trackVolume;
    payload.reserved = 0;
    payload.maxSamples = maxSamples;
    
    sendCommand(CMD_TRIGGER_SEQ, &payload, sizeof(payload));
}

void SPIMaster::triggerSampleLive(int padIndex, uint8_t velocity) {
    if (padIndex < 0 || padIndex >= MAX_PADS) return;
    
    TriggerLivePayload payload;
    payload.padIndex = (uint8_t)padIndex;
    payload.velocity = velocity;
    
    sendCommand(CMD_TRIGGER_LIVE, &payload, sizeof(payload));
}

void SPIMaster::triggerSample(int padIndex, uint8_t velocity) {
    triggerSampleLive(padIndex, velocity);
}

void SPIMaster::stopSample(int padIndex) {
    uint8_t pad = (uint8_t)padIndex;
    sendCommand(CMD_TRIGGER_STOP, &pad, 1);
}

void SPIMaster::stopAll() {
    sendCommand(CMD_TRIGGER_STOP_ALL, nullptr, 0);
}

// ═══════════════════════════════════════════════════════
// VOLUME CONTROL
// ═══════════════════════════════════════════════════════

void SPIMaster::setMasterVolume(uint8_t volume) {
    cachedMasterVolume = volume;
    VolumePayload p = {volume};
    sendCommand(CMD_MASTER_VOLUME, &p, sizeof(p));
}

uint8_t SPIMaster::getMasterVolume() {
    return cachedMasterVolume;
}

void SPIMaster::setSequencerVolume(uint8_t volume) {
    cachedSeqVolume = volume;
    VolumePayload p = {volume};
    sendCommand(CMD_SEQ_VOLUME, &p, sizeof(p));
}

uint8_t SPIMaster::getSequencerVolume() {
    return cachedSeqVolume;
}

void SPIMaster::setLiveVolume(uint8_t volume) {
    cachedLiveVolume = volume;
    VolumePayload p = {volume};
    sendCommand(CMD_LIVE_VOLUME, &p, sizeof(p));
}

uint8_t SPIMaster::getLiveVolume() {
    return cachedLiveVolume;
}

void SPIMaster::setLivePitchShift(float pitch) {
    cachedLivePitch = constrain(pitch, 0.25f, 3.0f);
    PitchPayload p = {cachedLivePitch};
    sendCommand(CMD_LIVE_PITCH, &p, sizeof(p));
}

float SPIMaster::getLivePitchShift() {
    return cachedLivePitch;
}

// ═══════════════════════════════════════════════════════
// GLOBAL FILTER
// ═══════════════════════════════════════════════════════

void SPIMaster::setFilterType(FilterType type) {
    GlobalFilterPayload p = {};
    p.filterType = (uint8_t)type;
    sendCommand(CMD_FILTER_SET, &p, sizeof(p));
}

void SPIMaster::setFilterCutoff(float cutoff) {
    FloatPayload p = {cutoff};
    sendCommand(CMD_FILTER_CUTOFF, &p, sizeof(p));
}

void SPIMaster::setFilterResonance(float resonance) {
    FloatPayload p = {resonance};
    sendCommand(CMD_FILTER_RESONANCE, &p, sizeof(p));
}

void SPIMaster::setBitDepth(uint8_t bits) {
    sendCommand(CMD_FILTER_BITDEPTH, &bits, 1);
}

void SPIMaster::setDistortion(float amount) {
    FloatPayload p = {amount};
    sendCommand(CMD_FILTER_DISTORTION, &p, sizeof(p));
}

void SPIMaster::setDistortionMode(DistortionMode mode) {
    uint8_t m = (uint8_t)mode;
    sendCommand(CMD_FILTER_DIST_MODE, &m, 1);
}

void SPIMaster::setSampleRateReduction(uint32_t rate) {
    Uint32Payload p = {rate};
    sendCommand(CMD_FILTER_SR_REDUCE, &p, sizeof(p));
}

// ═══════════════════════════════════════════════════════
// MASTER EFFECTS — DELAY
// ═══════════════════════════════════════════════════════

void SPIMaster::setDelayActive(bool active) {
    BoolPayload p = {(uint8_t)(active ? 1 : 0)};
    sendCommand(CMD_DELAY_ACTIVE, &p, sizeof(p));
}

void SPIMaster::setDelayTime(float ms) {
    FloatPayload p = {ms};
    sendCommand(CMD_DELAY_TIME, &p, sizeof(p));
}

void SPIMaster::setDelayFeedback(float feedback) {
    FloatPayload p = {feedback};
    sendCommand(CMD_DELAY_FEEDBACK, &p, sizeof(p));
}

void SPIMaster::setDelayMix(float mix) {
    FloatPayload p = {mix};
    sendCommand(CMD_DELAY_MIX, &p, sizeof(p));
}

// ═══════════════════════════════════════════════════════
// MASTER EFFECTS — PHASER
// ═══════════════════════════════════════════════════════

void SPIMaster::setPhaserActive(bool active) {
    BoolPayload p = {(uint8_t)(active ? 1 : 0)};
    sendCommand(CMD_PHASER_ACTIVE, &p, sizeof(p));
}

void SPIMaster::setPhaserRate(float hz) {
    FloatPayload p = {hz};
    sendCommand(CMD_PHASER_RATE, &p, sizeof(p));
}

void SPIMaster::setPhaserDepth(float depth) {
    FloatPayload p = {depth};
    sendCommand(CMD_PHASER_DEPTH, &p, sizeof(p));
}

void SPIMaster::setPhaserFeedback(float feedback) {
    FloatPayload p = {feedback};
    sendCommand(CMD_PHASER_FEEDBACK, &p, sizeof(p));
}

// ═══════════════════════════════════════════════════════
// MASTER EFFECTS — FLANGER
// ═══════════════════════════════════════════════════════

void SPIMaster::setFlangerActive(bool active) {
    BoolPayload p = {(uint8_t)(active ? 1 : 0)};
    sendCommand(CMD_FLANGER_ACTIVE, &p, sizeof(p));
}

void SPIMaster::setFlangerRate(float hz) {
    FloatPayload p = {hz};
    sendCommand(CMD_FLANGER_RATE, &p, sizeof(p));
}

void SPIMaster::setFlangerDepth(float depth) {
    FloatPayload p = {depth};
    sendCommand(CMD_FLANGER_DEPTH, &p, sizeof(p));
}

void SPIMaster::setFlangerFeedback(float feedback) {
    FloatPayload p = {feedback};
    sendCommand(CMD_FLANGER_FEEDBACK, &p, sizeof(p));
}

void SPIMaster::setFlangerMix(float mix) {
    FloatPayload p = {mix};
    sendCommand(CMD_FLANGER_MIX, &p, sizeof(p));
}

// ═══════════════════════════════════════════════════════
// MASTER EFFECTS — COMPRESSOR
// ═══════════════════════════════════════════════════════

void SPIMaster::setCompressorActive(bool active) {
    BoolPayload p = {(uint8_t)(active ? 1 : 0)};
    sendCommand(CMD_COMP_ACTIVE, &p, sizeof(p));
}

void SPIMaster::setCompressorThreshold(float threshold) {
    FloatPayload p = {threshold};
    sendCommand(CMD_COMP_THRESHOLD, &p, sizeof(p));
}

void SPIMaster::setCompressorRatio(float ratio) {
    FloatPayload p = {ratio};
    sendCommand(CMD_COMP_RATIO, &p, sizeof(p));
}

void SPIMaster::setCompressorAttack(float ms) {
    FloatPayload p = {ms};
    sendCommand(CMD_COMP_ATTACK, &p, sizeof(p));
}

void SPIMaster::setCompressorRelease(float ms) {
    FloatPayload p = {ms};
    sendCommand(CMD_COMP_RELEASE, &p, sizeof(p));
}

void SPIMaster::setCompressorMakeupGain(float db) {
    FloatPayload p = {db};
    sendCommand(CMD_COMP_MAKEUP, &p, sizeof(p));
}

// ═══════════════════════════════════════════════════════
// PER-TRACK FILTER
// ═══════════════════════════════════════════════════════

bool SPIMaster::setTrackFilter(int track, FilterType type, float cutoff, float resonance, float gain) {
    if (track < 0 || track >= MAX_AUDIO_TRACKS) return false;
    
    cachedTrackFilter[track] = type;
    trackFilterActive[track] = (type != FILTER_NONE);
    
    TrackFilterPayload p = {};
    p.track = (uint8_t)track;
    p.filterType = (uint8_t)type;
    p.cutoff = cutoff;
    p.resonance = resonance;
    p.gain = gain;
    
    sendCommand(CMD_TRACK_FILTER, &p, sizeof(p));
    return true;
}

void SPIMaster::clearTrackFilter(int track) {
    if (track < 0 || track >= MAX_AUDIO_TRACKS) return;
    
    cachedTrackFilter[track] = FILTER_NONE;
    trackFilterActive[track] = false;
    
    uint8_t t = (uint8_t)track;
    sendCommand(CMD_TRACK_CLEAR_FILTER, &t, 1);
}

FilterType SPIMaster::getTrackFilter(int track) {
    if (track < 0 || track >= MAX_AUDIO_TRACKS) return FILTER_NONE;
    return cachedTrackFilter[track];
}

int SPIMaster::getActiveTrackFiltersCount() {
    int count = 0;
    for (int i = 0; i < MAX_AUDIO_TRACKS; i++) {
        if (trackFilterActive[i]) count++;
    }
    return count;
}

// ═══════════════════════════════════════════════════════
// PER-PAD FILTER
// ═══════════════════════════════════════════════════════

bool SPIMaster::setPadFilter(int pad, FilterType type, float cutoff, float resonance, float gain) {
    if (pad < 0 || pad >= MAX_PADS) return false;
    
    cachedPadFilter[pad] = type;
    padFilterActive[pad] = (type != FILTER_NONE);
    
    PadFilterPayload p = {};
    p.pad = (uint8_t)pad;
    p.filterType = (uint8_t)type;
    p.cutoff = cutoff;
    p.resonance = resonance;
    p.gain = gain;
    
    sendCommand(CMD_PAD_FILTER, &p, sizeof(p));
    return true;
}

void SPIMaster::clearPadFilter(int pad) {
    if (pad < 0 || pad >= MAX_PADS) return;
    
    cachedPadFilter[pad] = FILTER_NONE;
    padFilterActive[pad] = false;
    
    uint8_t p = (uint8_t)pad;
    sendCommand(CMD_PAD_CLEAR_FILTER, &p, 1);
}

FilterType SPIMaster::getPadFilter(int pad) {
    if (pad < 0 || pad >= MAX_PADS) return FILTER_NONE;
    return cachedPadFilter[pad];
}

int SPIMaster::getActivePadFiltersCount() {
    int count = 0;
    for (int i = 0; i < MAX_PADS; i++) {
        if (padFilterActive[i]) count++;
    }
    return count;
}

// ═══════════════════════════════════════════════════════
// PER-PAD / PER-TRACK FX
// ═══════════════════════════════════════════════════════

void SPIMaster::setPadDistortion(int pad, float amount, DistortionMode mode) {
    if (pad < 0 || pad >= MAX_PADS) return;
    
    PadDistortionPayload p = {};
    p.pad = (uint8_t)pad;
    p.distMode = (uint8_t)mode;
    p.amount = amount;
    
    sendCommand(CMD_PAD_DISTORTION, &p, sizeof(p));
}

void SPIMaster::setPadBitCrush(int pad, uint8_t bits) {
    if (pad < 0 || pad >= MAX_PADS) return;
    
    PadBitCrushPayload p = {(uint8_t)pad, bits};
    sendCommand(CMD_PAD_BITCRUSH, &p, sizeof(p));
}

void SPIMaster::clearPadFX(int pad) {
    if (pad < 0 || pad >= MAX_PADS) return;
    
    uint8_t p = (uint8_t)pad;
    sendCommand(CMD_PAD_CLEAR_FX, &p, 1);
}

void SPIMaster::setTrackDistortion(int track, float amount, DistortionMode mode) {
    if (track < 0 || track >= MAX_AUDIO_TRACKS) return;
    
    PadDistortionPayload p = {};
    p.pad = (uint8_t)track;
    p.distMode = (uint8_t)mode;
    p.amount = amount;
    
    sendCommand(CMD_TRACK_DISTORTION, &p, sizeof(p));
}

void SPIMaster::setTrackBitCrush(int track, uint8_t bits) {
    if (track < 0 || track >= MAX_AUDIO_TRACKS) return;
    
    PadBitCrushPayload p = {(uint8_t)track, bits};
    sendCommand(CMD_TRACK_BITCRUSH, &p, sizeof(p));
}

void SPIMaster::clearTrackFX(int track) {
    if (track < 0 || track >= MAX_AUDIO_TRACKS) return;
    
    uint8_t t = (uint8_t)track;
    sendCommand(CMD_TRACK_CLEAR_FX, &t, 1);
}

// ═══════════════════════════════════════════════════════
// PER-TRACK LIVE FX (Echo, Flanger, Compressor)
// ═══════════════════════════════════════════════════════

void SPIMaster::setTrackEcho(int track, bool active, float time, float feedback, float mix) {
    if (track < 0 || track >= MAX_AUDIO_TRACKS) return;
    
    cachedTrackEchoActive[track] = active;
    
    TrackEchoPayload p = {};
    p.track = (uint8_t)track;
    p.active = active ? 1 : 0;
    p.time = time;
    p.feedback = feedback;
    p.mix = mix;
    
    sendCommand(CMD_TRACK_ECHO, &p, sizeof(p));
}

void SPIMaster::setTrackFlanger(int track, bool active, float rate, float depth, float feedback) {
    if (track < 0 || track >= MAX_AUDIO_TRACKS) return;
    
    cachedTrackFlangerActive[track] = active;
    
    TrackFlangerPayload p = {};
    p.track = (uint8_t)track;
    p.active = active ? 1 : 0;
    p.rate = rate;
    p.depth = depth;
    p.feedback = feedback;
    
    sendCommand(CMD_TRACK_FLANGER_FX, &p, sizeof(p));
}

void SPIMaster::setTrackCompressor(int track, bool active, float threshold, float ratio) {
    if (track < 0 || track >= MAX_AUDIO_TRACKS) return;
    
    cachedTrackCompActive[track] = active;
    
    TrackCompressorPayload p = {};
    p.track = (uint8_t)track;
    p.active = active ? 1 : 0;
    p.threshold = threshold;
    p.ratio = ratio;
    
    sendCommand(CMD_TRACK_COMPRESSOR, &p, sizeof(p));
}

void SPIMaster::clearTrackLiveFX(int track) {
    if (track < 0 || track >= MAX_AUDIO_TRACKS) return;
    
    cachedTrackEchoActive[track] = false;
    cachedTrackFlangerActive[track] = false;
    cachedTrackCompActive[track] = false;
    
    uint8_t t = (uint8_t)track;
    sendCommand(CMD_TRACK_CLEAR_LIVE, &t, 1);
}

bool SPIMaster::getTrackEchoActive(int track) const {
    if (track < 0 || track >= MAX_AUDIO_TRACKS) return false;
    return cachedTrackEchoActive[track];
}

bool SPIMaster::getTrackFlangerActive(int track) const {
    if (track < 0 || track >= MAX_AUDIO_TRACKS) return false;
    return cachedTrackFlangerActive[track];
}

bool SPIMaster::getTrackCompressorActive(int track) const {
    if (track < 0 || track >= MAX_AUDIO_TRACKS) return false;
    return cachedTrackCompActive[track];
}

// ═══════════════════════════════════════════════════════
// SIDECHAIN
// ═══════════════════════════════════════════════════════

void SPIMaster::setSidechain(bool active, int sourceTrack, uint16_t destinationMask,
                              float amount, float attackMs, float releaseMs, float knee) {
    SidechainPayload p = {};
    p.active = active ? 1 : 0;
    p.sourceTrack = (uint8_t)sourceTrack;
    p.destMask = destinationMask;
    p.amount = amount;
    p.attackMs = attackMs;
    p.releaseMs = releaseMs;
    p.knee = knee;
    
    sendCommand(CMD_SIDECHAIN_SET, &p, sizeof(p));
}

void SPIMaster::clearSidechain() {
    sendCommand(CMD_SIDECHAIN_CLEAR, nullptr, 0);
}

// ═══════════════════════════════════════════════════════
// PAD CONTROL (loop, reverse, pitch, stutter, scratch)
// ═══════════════════════════════════════════════════════

void SPIMaster::setPadLoop(int padIndex, bool enabled) {
    if (padIndex < 0 || padIndex >= MAX_PADS) return;
    
    cachedPadLoop[padIndex] = enabled;
    
    PadLoopPayload p = {(uint8_t)padIndex, (uint8_t)(enabled ? 1 : 0)};
    sendCommand(CMD_PAD_LOOP, &p, sizeof(p));
    
    Serial.printf("[SPI] Pad %d loop: %s\n", padIndex, enabled ? "ON" : "OFF");
}

bool SPIMaster::isPadLooping(int padIndex) {
    if (padIndex < 0 || padIndex >= MAX_PADS) return false;
    return cachedPadLoop[padIndex];
}

void SPIMaster::setReverseSample(int padIndex, bool reverse) {
    if (padIndex < 0 || padIndex >= MAX_PADS) return;
    
    PadReversePayload p = {(uint8_t)padIndex, (uint8_t)(reverse ? 1 : 0)};
    sendCommand(CMD_PAD_REVERSE, &p, sizeof(p));
}

void SPIMaster::setTrackPitchShift(int padIndex, float pitch) {
    if (padIndex < 0 || padIndex >= MAX_PADS) return;
    
    PadPitchPayload p = {};
    p.pad = (uint8_t)padIndex;
    p.pitch = pitch;
    
    sendCommand(CMD_PAD_PITCH, &p, sizeof(p));
}

void SPIMaster::setStutter(int padIndex, bool active, int intervalMs) {
    if (padIndex < 0 || padIndex >= MAX_PADS) return;
    
    PadStutterPayload p = {};
    p.pad = (uint8_t)padIndex;
    p.active = active ? 1 : 0;
    p.intervalMs = (uint16_t)intervalMs;
    
    sendCommand(CMD_PAD_STUTTER, &p, sizeof(p));
}

void SPIMaster::setScratchParams(int padIndex, bool active, float rate, float depth, 
                                  float filterCutoff, float crackle) {
    if (padIndex < 0 || padIndex >= MAX_PADS) return;
    
    PadScratchPayload p = {};
    p.pad = (uint8_t)padIndex;
    p.active = active ? 1 : 0;
    p.rate = rate;
    p.depth = depth;
    p.filterCutoff = filterCutoff;
    p.crackle = crackle;
    
    sendCommand(CMD_PAD_SCRATCH, &p, sizeof(p));
}

void SPIMaster::setTurntablismParams(int padIndex, bool active, bool autoMode, int mode, 
                                      int brakeMs, int backspinMs, float transformRate, 
                                      float vinylNoise) {
    if (padIndex < 0 || padIndex >= MAX_PADS) return;
    
    PadTurntablismPayload p = {};
    p.pad = (uint8_t)padIndex;
    p.active = active ? 1 : 0;
    p.autoMode = autoMode ? 1 : 0;
    p.mode = (int8_t)mode;
    p.brakeMs = (uint16_t)brakeMs;
    p.backspinMs = (uint16_t)backspinMs;
    p.transformRate = transformRate;
    p.vinylNoise = vinylNoise;
    
    sendCommand(CMD_PAD_TURNTABLISM, &p, sizeof(p));
}

// ═══════════════════════════════════════════════════════
// SAMPLE TRANSFER
// ═══════════════════════════════════════════════════════

bool SPIMaster::setSampleBuffer(int padIndex, int16_t* buffer, uint32_t length) {
    if (padIndex < 0 || padIndex >= MAX_PADS) return false;
    
    // Transfer sample data to STM32
    if (buffer && length > 0) {
        return transferSample(padIndex, buffer, length);
    } else {
        // Unload
        unloadSample(padIndex);
        return true;
    }
}

bool SPIMaster::transferSample(int padIndex, int16_t* buffer, uint32_t numSamples) {
    if (!buffer || numSamples == 0) return false;
    
    uint32_t totalBytes = numSamples * sizeof(int16_t);
    
    Serial.printf("[SPI] Transferring sample %d: %d samples (%d bytes)...\n",
                  padIndex, numSamples, totalBytes);
    
    // 1. BEGIN
    SampleBeginPayload beginP = {};
    beginP.padIndex = (uint8_t)padIndex;
    beginP.bitsPerSample = 16;
    beginP.sampleRate = 44100;
    beginP.totalBytes = totalBytes;
    beginP.totalSamples = numSamples;
    
    sendCommand(CMD_SAMPLE_BEGIN, &beginP, sizeof(beginP));
    delayMicroseconds(200);  // Give STM32 time to allocate
    
    // 2. DATA chunks (max 512 bytes = 256 samples per chunk)
    const uint16_t CHUNK_BYTES = 512;
    uint32_t offset = 0;
    uint32_t chunkCount = 0;
    
    // Build data packet: SampleDataHeader + raw audio data
    uint8_t dataPkt[8 + CHUNK_BYTES];
    
    while (offset < totalBytes) {
        uint16_t chunkSize = (uint16_t)min((uint32_t)CHUNK_BYTES, totalBytes - offset);
        
        SampleDataHeader* hdr = (SampleDataHeader*)dataPkt;
        hdr->padIndex = (uint8_t)padIndex;
        hdr->reserved = 0;
        hdr->chunkSize = chunkSize;
        hdr->offset = offset;
        
        memcpy(dataPkt + sizeof(SampleDataHeader), ((uint8_t*)buffer) + offset, chunkSize);
        
        sendCommand(CMD_SAMPLE_DATA, dataPkt, sizeof(SampleDataHeader) + chunkSize);
        
        offset += chunkSize;
        chunkCount++;
        
        // Throttle slightly to avoid overwhelming STM32
        if (chunkCount % 16 == 0) {
            delayMicroseconds(100);
        }
    }
    
    // 3. END
    SampleEndPayload endP = {};
    endP.padIndex = (uint8_t)padIndex;
    endP.status = 0;
    endP.checksum = crc16((uint8_t*)buffer, totalBytes > 65535 ? 65535 : (uint16_t)totalBytes);
    
    sendCommand(CMD_SAMPLE_END, &endP, sizeof(endP));
    
    Serial.printf("[SPI] Sample %d transfer complete: %d chunks, %d bytes\n",
                  padIndex, chunkCount, totalBytes);
    return true;
}

void SPIMaster::unloadSample(int padIndex) {
    SampleUnloadPayload p = {(uint8_t)padIndex};
    sendCommand(CMD_SAMPLE_UNLOAD, &p, sizeof(p));
}

void SPIMaster::unloadAllSamples() {
    sendCommand(CMD_SAMPLE_UNLOAD_ALL, nullptr, 0);
}

// ═══════════════════════════════════════════════════════
// STATUS / METERING
// ═══════════════════════════════════════════════════════

float SPIMaster::getTrackPeak(int track) {
    if (track < 0 || track >= MAX_AUDIO_TRACKS) return 0.0f;
    return cachedTrackPeaks[track];
}

float SPIMaster::getMasterPeak() {
    return cachedMasterPeak;
}

void SPIMaster::getTrackPeaks(float* outPeaks, int count) {
    int n = min(count, (int)MAX_AUDIO_TRACKS);
    memcpy(outPeaks, cachedTrackPeaks, n * sizeof(float));
}

bool SPIMaster::requestPeaks() {
    if (!stm32Connected) return false;
    
    PeaksResponse resp;
    if (sendAndReceive(CMD_GET_PEAKS, nullptr, 0, &resp, sizeof(resp))) {
        memcpy(cachedTrackPeaks, resp.trackPeaks, sizeof(cachedTrackPeaks));
        cachedMasterPeak = resp.masterPeak;
        return true;
    }
    return false;
}

int SPIMaster::getActiveVoices() {
    return cachedActiveVoices;
}

float SPIMaster::getCpuLoad() {
    return cachedCpuLoad;
}

bool SPIMaster::ping(uint32_t& roundtripUs) {
    PingPayload pingP = {(uint32_t)micros()};
    PongResponse pong;
    
    uint32_t start = micros();
    if (sendAndReceive(CMD_PING, &pingP, sizeof(pingP), &pong, sizeof(pong))) {
        roundtripUs = micros() - start;
        return (pong.echoTimestamp == pingP.timestamp);
    }
    return false;
}

void SPIMaster::resetDSP() {
    sendCommand(CMD_RESET, nullptr, 0);
    
    // Reset cached state
    for (int i = 0; i < MAX_AUDIO_TRACKS; i++) {
        cachedTrackFilter[i] = FILTER_NONE;
        trackFilterActive[i] = false;
        cachedTrackEchoActive[i] = false;
        cachedTrackFlangerActive[i] = false;
        cachedTrackCompActive[i] = false;
        cachedTrackPeaks[i] = 0.0f;
    }
    for (int i = 0; i < MAX_PADS; i++) {
        cachedPadFilter[i] = FILTER_NONE;
        padFilterActive[i] = false;
        cachedPadLoop[i] = false;
    }
    cachedMasterPeak = 0.0f;
    cachedActiveVoices = 0;
    cachedCpuLoad = 0.0f;
    
    Serial.println("[SPI] DSP Reset sent");
}

// ═══════════════════════════════════════════════════════
// FILTER PRESETS (static, for UI)
// ═══════════════════════════════════════════════════════

const FilterPreset* SPIMaster::getFilterPreset(FilterType type) {
    if (type >= 0 && type <= FILTER_STUTTER) {
        return &filterPresets[type];
    }
    return &filterPresets[0];
}

const char* SPIMaster::getFilterName(FilterType type) {
    const FilterPreset* fp = getFilterPreset(type);
    return fp ? fp->name : "Unknown";
}
