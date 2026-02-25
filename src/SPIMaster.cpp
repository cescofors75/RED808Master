/*
 * SPIMaster.cpp
 * RED808 SPI Master — Full implementation
 * Sends commands to STM32 Audio Slave via SPI
 */

#include "SPIMaster.h"

// ═══════════════════════════════════════════════════════
// SPI DEBUG — Activar/desactivar logs por Serial
// ═══════════════════════════════════════════════════════
#define SPI_DEBUG_ENABLED   true   // <<< cambiar a false para silenciar
#define SPI_DEBUG_PEAKS     false  // peaks cada 50ms = mucho spam
#define SPI_DEBUG_SAMPLE    false  // chunks de sample transfer

static const char* spiCmdName(uint8_t cmd) {
    switch(cmd) {
        case 0x01: return "TRIG_SEQ";
        case 0x02: return "TRIG_LIVE";
        case 0x03: return "TRIG_STOP";
        case 0x04: return "STOP_ALL";
        case 0x05: return "TRIG_SC";
        case 0x10: return "VOL_MASTER";
        case 0x11: return "VOL_SEQ";
        case 0x12: return "VOL_LIVE";
        case 0x13: return "VOL_TRACK";
        case 0x14: return "PITCH_LIVE";
        case 0x20: return "FILT_SET";
        case 0x21: return "FILT_CUT";
        case 0x22: return "FILT_RES";
        case 0x23: return "FILT_BIT";
        case 0x24: return "FILT_DIST";
        case 0x25: return "FILT_DMOD";
        case 0x26: return "FILT_SR";
        case 0x30: return "DLY_ACT";
        case 0x31: return "DLY_TIME";
        case 0x32: return "DLY_FB";
        case 0x33: return "DLY_MIX";
        case 0x34: return "PH_ACT";
        case 0x35: return "PH_RATE";
        case 0x36: return "PH_DEPTH";
        case 0x37: return "PH_FB";
        case 0x38: return "FL_ACT";
        case 0x39: return "FL_RATE";
        case 0x3A: return "FL_DEPTH";
        case 0x3B: return "FL_FB";
        case 0x3C: return "FL_MIX";
        case 0x3D: return "CMP_ACT";
        case 0x3E: return "CMP_THR";
        case 0x3F: return "CMP_RAT";
        case 0x40: return "CMP_ATK";
        case 0x41: return "CMP_REL";
        case 0x42: return "CMP_MKP";
        case 0x43: return "RVB_ACT";
        case 0x44: return "RVB_FB";
        case 0x45: return "RVB_LP";
        case 0x46: return "RVB_MIX";
        case 0x47: return "CHR_ACT";
        case 0x48: return "CHR_RATE";
        case 0x49: return "CHR_DEPT";
        case 0x4A: return "CHR_MIX";
        case 0x4B: return "TRM_ACT";
        case 0x4C: return "TRM_RATE";
        case 0x4D: return "TRM_DEPT";
        case 0x4E: return "WFOLD";
        case 0x4F: return "LIM_ACT";
        case 0x50: return "TK_FILT";
        case 0x51: return "TK_CLR_F";
        case 0x52: return "TK_DIST";
        case 0x53: return "TK_BITCR";
        case 0x54: return "TK_ECHO";
        case 0x55: return "TK_FLANG";
        case 0x56: return "TK_COMP";
        case 0x57: return "TK_CLR_L";
        case 0x58: return "TK_CLR_X";
        case 0x70: return "PD_FILT";
        case 0x71: return "PD_CLR_F";
        case 0x72: return "PD_DIST";
        case 0x73: return "PD_BITCR";
        case 0x74: return "PD_LOOP";
        case 0x75: return "PD_REV";
        case 0x76: return "PD_PITCH";
        case 0x77: return "PD_STUTT";
        case 0x78: return "PD_SCRAT";
        case 0x79: return "PD_TURNT";
        case 0x7A: return "PD_CLR";
        case 0x90: return "SC_SET";
        case 0x91: return "SC_CLR";
        case 0xA0: return "SMPL_BEG";
        case 0xA1: return "SMPL_DAT";
        case 0xA2: return "SMPL_END";
        case 0xA3: return "SMPL_UNL";
        case 0xA4: return "SMPL_UNA";
        case 0xB0: return "SD_DIRS";
        case 0xB1: return "SD_FILES";
        case 0xB2: return "SD_INFO";
        case 0xB3: return "SD_LOAD";
        case 0xB4: return "SD_LKIT";
        case 0xB5: return "SD_KLIST";
        case 0xB6: return "SD_STAT";
        case 0xB7: return "SD_UKIT";
        case 0xB8: return "SD_GLOAD";
        case 0xB9: return "SD_ABORT";
        case 0xE0: return "GET_STAT";
        case 0xE1: return "GET_PEAK";
        case 0xE2: return "GET_CPU";
        case 0xE3: return "GET_VOIC";
        case 0xE4: return "GET_EVTS";
        case 0xEE: return "PING";
        case 0xEF: return "RESET";
        case 0xF0: return "BULK_TRG";
        case 0xF1: return "BULK_FX";
        default:   return "???";
    }
}

static bool shouldLogCmd(uint8_t cmd) {
    if (!SPI_DEBUG_ENABLED) return false;
    if (cmd == CMD_GET_PEAKS && !SPI_DEBUG_PEAKS) return false;
    if (cmd == CMD_SAMPLE_DATA && !SPI_DEBUG_SAMPLE) return false;
    return true;
}

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

SPIMaster::SPIMaster() : spi(nullptr), seqNumber(0), spiErrorCount(0), stm32Connected(false), spiMutex(nullptr) {
    spiMutex = xSemaphoreCreateMutex();
    // Initialize cached state
    cachedMasterVolume = 100;
    cachedSeqVolume = 10;
    cachedLiveVolume = 80;
    cachedLivePitch = 1.0f;
    
    // New FX cached state
    cachedReverbActive = false;
    cachedReverbFeedback = 0.85f;
    cachedReverbLpFreq = 8000.0f;
    cachedReverbMix = 0.3f;
    cachedChorusActive = false;
    cachedChorusRate = 0.5f;
    cachedChorusDepth = 0.5f;
    cachedChorusMix = 0.4f;
    cachedTremoloActive = false;
    cachedTremoloRate = 4.0f;
    cachedTremoloDepth = 0.7f;
    cachedWaveFolderGain = 1.0f;
    cachedLimiterActive = false;
    memset(&cachedStatus, 0, sizeof(cachedStatus));
    
    for (int i = 0; i < MAX_AUDIO_TRACKS; i++) {
        cachedTrackFilter[i] = FILTER_NONE;
        trackFilterActive[i] = false;
        cachedTrackEchoActive[i] = false;
        cachedTrackFlangerActive[i] = false;
        cachedTrackCompActive[i] = false;
        cachedTrackReverbSend[i] = 0;
        cachedTrackDelaySend[i] = 0;
        cachedTrackChorusSend[i] = 0;
        cachedTrackPan[i] = 0;
        cachedTrackMute[i] = false;
        cachedTrackSolo[i] = false;
        cachedTrackPeaks[i] = 0.0f;
    }
    
    for (int i = 0; i < MAX_PADS; i++) {
        cachedPadFilter[i] = FILTER_NONE;
        padFilterActive[i] = false;
        cachedPadLoop[i] = false;
    }
    
    cachedMasterPeak = 0.0f;
    lastPeakRequest = 0;
    lastStatusPoll = 0;
    eventCallback = nullptr;
    eventUserData = nullptr;
    
    memset(txBuffer, 0, sizeof(txBuffer));
    memset(rxBuffer, 0, sizeof(rxBuffer));
}

SPIMaster::~SPIMaster() {
    if (spi) {
        spi->end();
        delete spi;
        spi = nullptr;
    }
    if (spiMutex) {
        vSemaphoreDelete(spiMutex);
        spiMutex = nullptr;
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
    
    if (shouldLogCmd(cmd)) {
        Serial.printf("[SPI TX] #%03d %-9s cmd=0x%02X len=%d crc=0x%04X\n",
                      header.sequence, spiCmdName(cmd), cmd, payloadLen, header.checksum);
        // Print first bytes of payload for debugging
        if (payload && payloadLen > 0 && payloadLen <= 16) {
            Serial.print("         data: ");
            for (int i = 0; i < payloadLen && i < 16; i++)
                Serial.printf("%02X ", ((const uint8_t*)payload)[i]);
            Serial.println();
        }
    }
    
    // Acquire SPI mutex (thread safety Core0 ↔ Core1)
    if (xSemaphoreTake(spiMutex, pdMS_TO_TICKS(10)) != pdTRUE) {
        Serial.printf("[SPI] MUTEX TIMEOUT cmd=0x%02X\n", cmd);
        return false;
    }
    
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
    
    xSemaphoreGive(spiMutex);
    
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
    
    if (shouldLogCmd(cmd)) {
        Serial.printf("[SPI TX] #%03d %-9s cmd=0x%02X len=%d (espera resp %d bytes)\n",
                      header.sequence, spiCmdName(cmd), cmd, payloadLen, responseLen);
    }
    
    // Acquire SPI mutex (thread safety Core0 ↔ Core1)
    if (xSemaphoreTake(spiMutex, pdMS_TO_TICKS(10)) != pdTRUE) {
        Serial.printf("[SPI] MUTEX TIMEOUT cmd=0x%02X (sendAndReceive)\n", cmd);
        return false;
    }
    
    // ── PHASE 1: enviar comando ──────────────────────────
    csLow();
    spi->beginTransaction(SPISettings(STM32_SPI_CLOCK, MSBFIRST, SPI_MODE0));
    spi->transferBytes((uint8_t*)&header, nullptr, sizeof(SPIPacketHeader));
    if (payload && payloadLen > 0) {
        spi->transferBytes((const uint8_t*)payload, nullptr, payloadLen);
    }
    spi->endTransaction();
    csHigh();   // <-- CS HIGH: NSS rising edge dispara la ISR del STM32

    // Dar tiempo al slave para parsear el comando y cargar su TX buffer.
    // A 20MHz 8 bytes = ~3.2us; 100us es más que suficiente para Daisy Seed.
    delayMicroseconds(100);

    // ── PHASE 2: leer respuesta ──────────────────────────
    SPIPacketHeader respHeader;
    memset(&respHeader, 0, sizeof(respHeader));

    csLow();
    spi->beginTransaction(SPISettings(STM32_SPI_CLOCK, MSBFIRST, SPI_MODE0));
    spi->transferBytes(nullptr, (uint8_t*)&respHeader, sizeof(SPIPacketHeader));
    
    bool success = false;
    if (respHeader.magic == SPI_MAGIC_RESP && respHeader.length <= responseLen) {
        // Leer payload de respuesta
        if (respHeader.length > 0 && response) {
            spi->transferBytes(nullptr, (uint8_t*)response, respHeader.length);
        }
        success = true;
        if (shouldLogCmd(cmd)) {
            Serial.printf("[SPI RX] #%03d %-9s OK len=%d\n",
                          respHeader.sequence, spiCmdName(cmd), respHeader.length);
        }
    } else {
        spiErrorCount++;
        if (shouldLogCmd(cmd)) {
            // Volcar los 8 bytes raw para diagnóstico STM32
            const uint8_t* raw = (const uint8_t*)&respHeader;
            Serial.printf("[SPI RX] #%03d %-9s FAIL magic=0x%02X cmd=0x%02X len=%d seq=%d (err_total=%d)\n",
                          header.sequence, spiCmdName(cmd),
                          respHeader.magic, respHeader.cmd,
                          respHeader.length, respHeader.sequence,
                          spiErrorCount);
            Serial.printf("         raw: %02X %02X %02X %02X %02X %02X %02X %02X\n",
                          raw[0], raw[1], raw[2], raw[3],
                          raw[4], raw[5], raw[6], raw[7]);
        }
    }
    
    spi->endTransaction();
    csHigh();
    
    xSemaphoreGive(spiMutex);
    
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
    
    // Poll status every 500ms (includes evtCount check)
    if (millis() - lastStatusPoll > 500) {
        requestStatus();
        lastStatusPoll = millis();
        
        // Auto-drain events if any pending
        if (cachedStatus.evtCount > 0) {
            drainEvents();
        }
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

void SPIMaster::triggerSidechain(int sourceTrack) {
    if (sourceTrack < 0 || sourceTrack >= MAX_AUDIO_TRACKS) return;
    SidechainTriggerPayload p = {(uint8_t)sourceTrack, 0};
    sendCommand(CMD_TRIGGER_SIDECHAIN, &p, sizeof(p));
}

bool SPIMaster::triggerBulk(const TriggerSeqPayload* triggers, uint8_t count) {
    if (!triggers || count == 0 || count > 16) return false;
    BulkTriggersPayload p = {};
    p.count = count;
    p.reserved = 0;
    memcpy(p.triggers, triggers, count * sizeof(TriggerSeqPayload));
    // Only send header + actual trigger data (not the full 16-slot array)
    uint16_t payloadLen = (uint16_t)(2 + count * sizeof(TriggerSeqPayload));
    return sendCommand(CMD_BULK_TRIGGERS, &p, payloadLen);
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
// PER-TRACK FX SEND LEVELS
// ═══════════════════════════════════════════════════════

void SPIMaster::setTrackReverbSend(int track, uint8_t level) {
    if (track < 0 || track >= MAX_AUDIO_TRACKS) return;
    cachedTrackReverbSend[track] = level;
    TrackSendPayload p = {};
    p.track = (uint8_t)track;
    p.sendLevel = level;
    sendCommand(CMD_TRACK_REVERB_SEND, &p, sizeof(p));
}

void SPIMaster::setTrackDelaySend(int track, uint8_t level) {
    if (track < 0 || track >= MAX_AUDIO_TRACKS) return;
    cachedTrackDelaySend[track] = level;
    TrackSendPayload p = {};
    p.track = (uint8_t)track;
    p.sendLevel = level;
    sendCommand(CMD_TRACK_DELAY_SEND, &p, sizeof(p));
}

void SPIMaster::setTrackChorusSend(int track, uint8_t level) {
    if (track < 0 || track >= MAX_AUDIO_TRACKS) return;
    cachedTrackChorusSend[track] = level;
    TrackSendPayload p = {};
    p.track = (uint8_t)track;
    p.sendLevel = level;
    sendCommand(CMD_TRACK_CHORUS_SEND, &p, sizeof(p));
}

// ═══════════════════════════════════════════════════════
// PER-TRACK MIXER (PAN, MUTE, SOLO)
// ═══════════════════════════════════════════════════════

void SPIMaster::setTrackPan(int track, int8_t pan) {
    if (track < 0 || track >= MAX_AUDIO_TRACKS) return;
    cachedTrackPan[track] = pan;
    TrackPanPayload p = {};
    p.track = (uint8_t)track;
    p.pan = pan;
    sendCommand(CMD_TRACK_PAN, &p, sizeof(p));
}

void SPIMaster::setTrackMute(int track, bool mute) {
    if (track < 0 || track >= MAX_AUDIO_TRACKS) return;
    cachedTrackMute[track] = mute;
    TrackMuteSoloPayload p = {};
    p.track = (uint8_t)track;
    p.enabled = mute ? 1 : 0;
    sendCommand(CMD_TRACK_MUTE, &p, sizeof(p));
}

void SPIMaster::setTrackSolo(int track, bool solo) {
    if (track < 0 || track >= MAX_AUDIO_TRACKS) return;
    cachedTrackSolo[track] = solo;
    TrackMuteSoloPayload p = {};
    p.track = (uint8_t)track;
    p.enabled = solo ? 1 : 0;
    sendCommand(CMD_TRACK_SOLO, &p, sizeof(p));
}

// ═══════════════════════════════════════════════════════
// PER-TRACK EXTENDED FX (phaser, tremolo, pitch, gate, EQ)
// ═══════════════════════════════════════════════════════

void SPIMaster::setTrackPhaser(int track, bool active, float rate, float depth, float feedback) {
    if (track < 0 || track >= MAX_AUDIO_TRACKS) return;
    TrackFlangerPayload p = {};  // reuse same struct layout
    p.track = (uint8_t)track;
    p.active = active ? 1 : 0;
    p.rate = rate;
    p.depth = depth;
    p.feedback = feedback;
    sendCommand(CMD_TRACK_PHASER, &p, sizeof(p));
}

void SPIMaster::setTrackTremolo(int track, bool active, float rate, float depth) {
    if (track < 0 || track >= MAX_AUDIO_TRACKS) return;
    struct __attribute__((packed)) { uint8_t track; uint8_t active; uint8_t reserved[2]; float rate; float depth; } p = {};
    p.track = (uint8_t)track;
    p.active = active ? 1 : 0;
    p.rate = rate;
    p.depth = depth;
    sendCommand(CMD_TRACK_TREMOLO, &p, sizeof(p));
}

void SPIMaster::setTrackPitch(int track, int16_t cents) {
    if (track < 0 || track >= MAX_AUDIO_TRACKS) return;
    struct __attribute__((packed)) { uint8_t track; uint8_t reserved; int16_t cents; } p = {};
    p.track = (uint8_t)track;
    p.cents = cents;
    sendCommand(CMD_TRACK_PITCH, &p, sizeof(p));
}

void SPIMaster::setTrackGate(int track, bool active, float thresholdDb, float attackMs, float releaseMs) {
    if (track < 0 || track >= MAX_AUDIO_TRACKS) return;
    TrackGatePayload p = {};
    p.track = (uint8_t)track;
    p.active = active ? 1 : 0;
    p.threshold = thresholdDb;
    p.attackMs = attackMs;
    p.releaseMs = releaseMs;
    sendCommand(CMD_TRACK_GATE, &p, sizeof(p));
}

void SPIMaster::setTrackEqLow(int track, int8_t gainDb) {
    if (track < 0 || track >= MAX_AUDIO_TRACKS) return;
    struct __attribute__((packed)) { uint8_t track; int8_t gain; } p = {};
    p.track = (uint8_t)track;
    p.gain = gainDb;
    sendCommand(CMD_TRACK_EQ_LOW, &p, sizeof(p));
}

void SPIMaster::setTrackEqMid(int track, int8_t gainDb) {
    if (track < 0 || track >= MAX_AUDIO_TRACKS) return;
    struct __attribute__((packed)) { uint8_t track; int8_t gain; } p = {};
    p.track = (uint8_t)track;
    p.gain = gainDb;
    sendCommand(CMD_TRACK_EQ_MID, &p, sizeof(p));
}

void SPIMaster::setTrackEqHigh(int track, int8_t gainDb) {
    if (track < 0 || track >= MAX_AUDIO_TRACKS) return;
    struct __attribute__((packed)) { uint8_t track; int8_t gain; } p = {};
    p.track = (uint8_t)track;
    p.gain = gainDb;
    sendCommand(CMD_TRACK_EQ_HIGH, &p, sizeof(p));
}

void SPIMaster::setTrackEq(int track, int8_t low, int8_t mid, int8_t high) {
    if (track < 0 || track >= MAX_AUDIO_TRACKS) return;
    TrackEqPayload p = {};
    p.track = (uint8_t)track;
    p.gainLow = low;
    p.gainMid = mid;
    p.gainHigh = high;
    // Send as 3 individual commands (no single CMD for full EQ band set)
    setTrackEqLow(track, low);
    setTrackEqMid(track, mid);
    setTrackEqHigh(track, high);
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
    beginP.sampleRate = SAMPLE_RATE;
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
// DAISY SD CARD FILE SYSTEM
// ═══════════════════════════════════════════════════════

bool SPIMaster::sdListFolders(SdFolderListResponse& out) {
    return sendAndReceive(CMD_SD_LIST_FOLDERS, nullptr, 0, &out, sizeof(out));
}

bool SPIMaster::sdListFiles(const char* folder, SdFileListResponse& out) {
    SdListFilesPayload p = {};
    strncpy(p.folderName, folder, sizeof(p.folderName) - 1);
    return sendAndReceive(CMD_SD_LIST_FILES, &p, sizeof(p), &out, sizeof(out));
}

bool SPIMaster::sdGetFileInfo(const char* folder, const char* file, SdFileInfoResponse& out) {
    SdFileInfoPayload p = {};
    p.padIndex = 0;
    strncpy(p.folderName, folder, sizeof(p.folderName) - 1);
    strncpy(p.fileName, file, sizeof(p.fileName) - 1);
    return sendAndReceive(CMD_SD_FILE_INFO, &p, sizeof(p), &out, sizeof(out));
}

bool SPIMaster::sdLoadSample(int padIndex, const char* folder, const char* file) {
    SdLoadSamplePayload p = {};
    strncpy(p.folderName, folder, sizeof(p.folderName) - 1);
    strncpy(p.fileName, file, sizeof(p.fileName) - 1);
    p.padIndex = (uint8_t)padIndex;
    return sendCommand(CMD_SD_LOAD_SAMPLE, &p, sizeof(p));
}

bool SPIMaster::sdLoadKit(const char* kitName, uint8_t startPad, uint8_t maxPads) {
    SdLoadKitPayload p = {};
    strncpy(p.kitName, kitName, sizeof(p.kitName) - 1);
    p.startPad = startPad;
    p.maxPads = maxPads;
    return sendCommand(CMD_SD_LOAD_KIT, &p, sizeof(p));
}

bool SPIMaster::sdGetKitList(SdKitListResponse& out) {
    return sendAndReceive(CMD_SD_KIT_LIST, nullptr, 0, &out, sizeof(out));
}

bool SPIMaster::sdGetStatus(SdStatusResponse& out) {
    return sendAndReceive(CMD_SD_STATUS, nullptr, 0, &out, sizeof(out));
}

void SPIMaster::sdUnloadKit() {
    sendCommand(CMD_SD_UNLOAD_KIT, nullptr, 0);
}

bool SPIMaster::sdGetLoadedKit(SdStatusResponse& out) {
    return sendAndReceive(CMD_SD_GET_LOADED, nullptr, 0, &out, sizeof(out));
}

void SPIMaster::sdAbortLoad() {
    sendCommand(CMD_SD_ABORT, nullptr, 0);
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
    return (int)cachedStatus.activeVoices;
}

float SPIMaster::getCpuLoad() {
    return (float)cachedStatus.cpuLoadPercent;
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
    cachedReverbActive = false;
    cachedChorusActive = false;
    cachedTremoloActive = false;
    cachedWaveFolderGain = 1.0f;
    cachedLimiterActive = false;
    memset(&cachedStatus, 0, sizeof(cachedStatus));
    
    Serial.println("[SPI] DSP Reset sent");
}

// ═══════════════════════════════════════════════════════
// MASTER FX — REVERB
// ═══════════════════════════════════════════════════════

void SPIMaster::setReverbActive(bool active) {
    cachedReverbActive = active;
    BoolPayload p = {(uint8_t)(active ? 1 : 0)};
    sendCommand(CMD_REVERB_ACTIVE, &p, sizeof(p));
}

void SPIMaster::setReverbFeedback(float feedback) {
    cachedReverbFeedback = constrain(feedback, 0.0f, 0.99f);
    FloatPayload p = {cachedReverbFeedback};
    sendCommand(CMD_REVERB_FEEDBACK, &p, sizeof(p));
}

void SPIMaster::setReverbLpFreq(float hz) {
    cachedReverbLpFreq = constrain(hz, 200.0f, 12000.0f);
    FloatPayload p = {cachedReverbLpFreq};
    sendCommand(CMD_REVERB_LPFREQ, &p, sizeof(p));
}

void SPIMaster::setReverbMix(float mix) {
    cachedReverbMix = constrain(mix, 0.0f, 1.0f);
    FloatPayload p = {cachedReverbMix};
    sendCommand(CMD_REVERB_MIX, &p, sizeof(p));
}

void SPIMaster::setReverb(bool active, float feedback, float lpFreq, float mix) {
    cachedReverbActive   = active;
    cachedReverbFeedback = constrain(feedback, 0.0f, 0.99f);
    cachedReverbLpFreq   = constrain(lpFreq, 200.0f, 12000.0f);
    cachedReverbMix      = constrain(mix, 0.0f, 1.0f);
    // Send full payload in one command
    ReverbPayload p = {};
    p.active   = active ? 1 : 0;
    p.feedback = cachedReverbFeedback;
    p.lpFreq   = cachedReverbLpFreq;
    p.mix      = cachedReverbMix;
    sendCommand(CMD_REVERB_ACTIVE, &p, sizeof(p));
}

// ═══════════════════════════════════════════════════════
// MASTER FX — CHORUS
// ═══════════════════════════════════════════════════════

void SPIMaster::setChorusActive(bool active) {
    cachedChorusActive = active;
    BoolPayload p = {(uint8_t)(active ? 1 : 0)};
    sendCommand(CMD_CHORUS_ACTIVE, &p, sizeof(p));
}

void SPIMaster::setChorusRate(float hz) {
    cachedChorusRate = constrain(hz, 0.1f, 10.0f);
    FloatPayload p = {cachedChorusRate};
    sendCommand(CMD_CHORUS_RATE, &p, sizeof(p));
}

void SPIMaster::setChorusDepth(float depth) {
    cachedChorusDepth = constrain(depth, 0.0f, 1.0f);
    FloatPayload p = {cachedChorusDepth};
    sendCommand(CMD_CHORUS_DEPTH, &p, sizeof(p));
}

void SPIMaster::setChorusMix(float mix) {
    cachedChorusMix = constrain(mix, 0.0f, 1.0f);
    FloatPayload p = {cachedChorusMix};
    sendCommand(CMD_CHORUS_MIX, &p, sizeof(p));
}

void SPIMaster::setChorus(bool active, float rate, float depth, float mix) {
    cachedChorusActive = active;
    cachedChorusRate   = constrain(rate,  0.1f, 10.0f);
    cachedChorusDepth  = constrain(depth, 0.0f, 1.0f);
    cachedChorusMix    = constrain(mix,   0.0f, 1.0f);
    ChorusPayload p = {};
    p.active = active ? 1 : 0;
    p.rate   = cachedChorusRate;
    p.depth  = cachedChorusDepth;
    p.mix    = cachedChorusMix;
    sendCommand(CMD_CHORUS_ACTIVE, &p, sizeof(p));
}

// ═══════════════════════════════════════════════════════
// MASTER FX — TREMOLO
// ═══════════════════════════════════════════════════════

void SPIMaster::setTremoloActive(bool active) {
    cachedTremoloActive = active;
    BoolPayload p = {(uint8_t)(active ? 1 : 0)};
    sendCommand(CMD_TREMOLO_ACTIVE, &p, sizeof(p));
}

void SPIMaster::setTremoloRate(float hz) {
    cachedTremoloRate = constrain(hz, 0.1f, 20.0f);
    FloatPayload p = {cachedTremoloRate};
    sendCommand(CMD_TREMOLO_RATE, &p, sizeof(p));
}

void SPIMaster::setTremoloDepth(float depth) {
    cachedTremoloDepth = constrain(depth, 0.0f, 1.0f);
    FloatPayload p = {cachedTremoloDepth};
    sendCommand(CMD_TREMOLO_DEPTH, &p, sizeof(p));
}

void SPIMaster::setTremolo(bool active, float rate, float depth) {
    cachedTremoloActive = active;
    cachedTremoloRate   = constrain(rate,  0.1f, 20.0f);
    cachedTremoloDepth  = constrain(depth, 0.0f, 1.0f);
    TremoloPayload p = {};
    p.active = active ? 1 : 0;
    p.rate   = cachedTremoloRate;
    p.depth  = cachedTremoloDepth;
    sendCommand(CMD_TREMOLO_ACTIVE, &p, sizeof(p));
}

// ═══════════════════════════════════════════════════════
// MASTER FX — WAVEFOLDER + LIMITER
// ═══════════════════════════════════════════════════════

void SPIMaster::setWaveFolderGain(float gain) {
    cachedWaveFolderGain = constrain(gain, 1.0f, 10.0f);
    FloatPayload p = {cachedWaveFolderGain};
    sendCommand(CMD_WAVEFOLDER_GAIN, &p, sizeof(p));
}

void SPIMaster::setLimiterActive(bool active) {
    cachedLimiterActive = active;
    BoolPayload p = {(uint8_t)(active ? 1 : 0)};
    sendCommand(CMD_LIMITER_ACTIVE, &p, sizeof(p));
}

// ═══════════════════════════════════════════════════════
// STATUS QUERIES — SPI round-trips
// ═══════════════════════════════════════════════════════

bool SPIMaster::requestActiveVoices() {
    if (!stm32Connected) return false;
    VoicesResponse resp = {};
    if (sendAndReceive(CMD_GET_VOICES, nullptr, 0, &resp, sizeof(resp))) {
        cachedStatus.activeVoices = resp.activeVoices;
        return true;
    }
    return false;
}

bool SPIMaster::requestCpuLoad() {
    if (!stm32Connected) return false;
    CpuLoadResponse resp = {};
    if (sendAndReceive(CMD_GET_CPU_LOAD, nullptr, 0, &resp, sizeof(resp))) {
        cachedStatus.cpuLoadPercent = (uint8_t)constrain((int)resp.cpuLoad, 0, 100);
        cachedStatus.uptime = resp.uptime / 1000;  // ms → s
        return true;
    }
    return false;
}

bool SPIMaster::requestStatus() {
    if (!stm32Connected) return false;
    StatusResponse resp = {};
    if (sendAndReceive(CMD_GET_STATUS, nullptr, 0, &resp, sizeof(resp))) {
        cachedStatus = resp;
        if (SPI_DEBUG_ENABLED) {
            Serial.printf("[SPI] Status: voices=%d cpu=%d%% kit='%s' pads=%d sd=%d evt=%d\n",
                resp.activeVoices, resp.cpuLoadPercent, resp.currentKitName,
                resp.totalPadsLoaded, resp.sdPresent, resp.evtCount);
        }
        return true;
    }
    return false;
}

bool SPIMaster::getStatusSnapshot(StatusResponse& out) {
    out = cachedStatus;
    return stm32Connected;
}

bool SPIMaster::requestEvents(EventsResponse& out) {
    if (!stm32Connected) return false;
    memset(&out, 0, sizeof(out));
    return sendAndReceive(CMD_GET_EVENTS, nullptr, 0, &out, sizeof(out));
}

bool SPIMaster::drainEvents() {
    if (!stm32Connected) return false;
    uint8_t remaining = cachedStatus.evtCount;
    int totalDrained = 0;
    
    while (remaining > 0) {
        EventsResponse evtResp = {};
        if (!requestEvents(evtResp)) break;
        if (evtResp.count == 0) break;
        
        for (int i = 0; i < evtResp.count; i++) {
            const NotifyEvent& evt = evtResp.events[i];
            totalDrained++;
            
            // Log the event
            const char* evtName = "???";
            switch (evt.type) {
                case EVT_SD_BOOT_DONE:     evtName = "BOOT_DONE"; break;
                case EVT_SD_KIT_LOADED:    evtName = "KIT_LOADED"; break;
                case EVT_SD_SAMPLE_LOADED: evtName = "SAMPLE_LOADED"; break;
                case EVT_SD_KIT_UNLOADED:  evtName = "KIT_UNLOADED"; break;
                case EVT_SD_ERROR:         evtName = "SD_ERROR"; break;
                case EVT_SD_XTRA_LOADED:   evtName = "XTRA_LOADED"; break;
            }
            Serial.printf("[SPI EVT] %s: pads=%d name='%s'\n", evtName, evt.padCount, evt.name);
            
            // Fire callback if registered
            if (eventCallback) {
                eventCallback(evt, eventUserData);
            }
        }
        
        remaining -= evtResp.count;
    }
    
    if (totalDrained > 0) {
        Serial.printf("[SPI] Drained %d events\n", totalDrained);
    }
    return totalDrained > 0;
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
