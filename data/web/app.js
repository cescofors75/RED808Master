// RED808 Drum Machine - JavaScript Application

let ws = null;
let isConnected = false;
let currentStep = 0;
let tremoloIntervals = {};
let padLoopState = {};
let padFxState = new Array(24).fill(null); // Per-pad FX state (16 main + 8 xtra)
let trackFxState = new Array(16).fill(null); // Per-track FX state
let isPlaying = false;

// Sync LEDs: when ON, live pads flash in rhythm with sequencer
let syncLedsEnabled = false;

// Sequencer caches
let stepDots = [];
let stepColumns = Array.from({ length: 16 }, () => []);
let lastCurrentStep = null;

// Sequencer view mode
let sequencerViewMode = 'grid'; // 'grid' or 'circular'
let circularCanvas = null;
let circularCtx = null;
let circularAnimationFrame = null;
let circularSequencerData = Array.from({ length: 16 }, () => Array(16).fill(false));

// Sample counts per family
let sampleCounts = {};

// Keyboard state
let keyboardPadsActive = {};
let keyboardHoldTimers = {};
let keyboardTremoloState = {};
let lastPadTriggerMs = new Array(24).fill(0);
const PAD_TEST_MIN_TRIGGER_MS = 80;

// Pad hold timers for long press detection
let padHoldTimers = {};
let trackMutedState = new Array(16).fill(false);
let trackSoloState = -1;        // -1 = none, 0-15 = índice del track en solo
let preSoloMuteState = null;    // estado de mutes guardado antes de entrar en solo

// Pad filter state (stores active filter type for each pad)
let padFilterState = new Array(24).fill(0); // 0 = FILTER_NONE (16 main + 8 xtra)
let trackFilterState = new Array(16).fill(0); // 0 = FILTER_NONE

// Per-track live FX state (Echo, Flanger, Compressor)
let trackLiveFxState = new Array(16).fill(null).map(() => ({
    echo:       { active: false, time: 100, feedback: 40, mix: 50 },
    flanger:    { active: false, rate: 50, depth: 50, feedback: 30 },
    compressor: { active: false, threshold: -20, ratio: 4 }
}));
// Per-pad live FX (Reverse, Pitch, Stutter — same keys as trackFxEffects)
let padLiveFxState = new Array(24).fill(null).map(() => ({
    reverse: false, pitch: 1.0, stutter: false, stutterMs: 100
}));

// Pad <-> Sequencer sync state (ALWAYS synced)
const padSeqSyncEnabled = true;

// 16 instrumentos principales (4x4 grid)
const padNames = ['BD', 'SD', 'CH', 'OH', 'CY', 'CP', 'RS', 'CB', 'LT', 'MT', 'HT', 'MA', 'CL', 'HC', 'MC', 'LC'];

// Tecla asociada a cada pad (1-8 para pads 0-7, 9-0 y U-F para pads 8-15)
const padKeyBindings = ['1', '2', '3', '4', '5', '6', '7', '8', '9', '0', 'U', 'I', 'O', 'P', 'D', 'F'];

// Descripción completa de cada instrumento
const padDescriptions = [
    'Bass Drum (Bombo)',
    'Snare Drum (Caja)',
    'Closed Hi-Hat',
    'Open Hi-Hat',
    'Cymbal (Platillo)',
    'Hand Clap (Palmas)',
    'Rim Shot (Aro)',
    'Cowbell (Cencerro)',
    'Low Tom',
    'Mid Tom',
    'High Tom',
    'Maracas',
    'Claves',
    'High Conga',
    'Mid Conga',
    'Low Conga'
];

// Filter types for track filter panel
const FILTER_TYPES = [
    { icon: '🚫', name: 'OFF' },
    { icon: '🔥', name: 'LOW PASS' },
    { icon: '✨', name: 'HIGH PASS' },
    { icon: '📞', name: 'BAND PASS' },
    { icon: '🕳️', name: 'NOTCH CUT' },
    { icon: '🔊', name: 'BASS BOOST' },
    { icon: '🌟', name: 'TREBLE BOOST' },
    { icon: '⛰️', name: 'PEAK BOOST' },
    { icon: '🌀', name: 'PHASE' },
    { icon: '⚡', name: 'RESONANT' }
];
window.FILTER_TYPES = FILTER_TYPES;

const instrumentPalette = [
    '#ff0000', '#ffa500', '#ffff00', '#00ffff',
    '#e6194b', '#ff00ff', '#00ff00', '#f58231',
    '#911eb4', '#46f0f0', '#f032e6', '#bcf60c',
    '#38ceff', '#fabebe', '#008080', '#484dff'
];

const padSampleMetadata = new Array(16).fill(null);
const DEFAULT_SAMPLE_QUALITY = '44.1kHz • 16-bit mono';
const sampleCatalog = {};
let sampleSelectorContext = null;
let pendingAutoPlayPad = null;
let activeSampleFilter = 'ALL';
let sampleBrowserRenderTimer = null;
let sampleRequestTimers = [];
let sampleRetryTimer = null;

// Simple notification function (stub)
function showNotification(message) {}

// Initialize
document.addEventListener('DOMContentLoaded', () => {
    initWebSocket();
    createPads();
    createSequencer();
    setupControls();
    initHeaderMeters();
    initVolumesSection();
    initLivePadsX();
    initFxSubtabs();
    
    // Sync FX state from patchbay (if available)
    syncFxFromPatchbay();
    
    // Initialize keyboard system from keyboard-controls.js first
    if (window.initKeyboardControls) {
        window.initKeyboardControls();
    }
    
    setupKeyboardControls(); // Then setup pad handlers in app.js
    initSampleBrowser();
    initInstrumentTabs();
    initTabSystem(); // Tab navigation system
    initSyncLeds(); // Sync LEDs toggle
    // initAiToggle(); // AI Chat DISABLED
});

// WebSocket Connection
function initWebSocket() {
    const wsScheme = window.location.protocol === 'https:' ? 'wss' : 'ws';
    const wsUrl = `${wsScheme}://${window.location.host}/ws`;
    console.log('[WS] Connecting to', wsUrl);
    ws = new WebSocket(wsUrl);
    window.ws = ws; // Expose for midi-import.js
    
    ws.onopen = () => {
        console.log('[WS] Connected', wsUrl);
        isConnected = true;
        updateStatus(true);
        syncLedMonoMode();
        
        setTimeout(() => { sendWebSocket({ cmd: 'init' }); }, 100);
        setTimeout(() => { sendWebSocket({ cmd: 'getPattern' }); }, 300);
        setTimeout(() => { requestSampleCounts(); }, 1000);
    };
    
    ws.onclose = () => {
        console.warn('[WS] Closed, retrying in 3s', wsUrl);
        isConnected = false;
        updateStatus(false);
        setTimeout(initWebSocket, 3000);
    };
    
    ws.onerror = (error) => {
        console.error('WebSocket Error:', error);
    };
    
    ws.binaryType = 'arraybuffer';  // Enable binary messages for audio levels
    ws.onmessage = (event) => {
        // Handle binary audio level data (0xAA header) and LFO scope (0xBB)
        if (event.data instanceof ArrayBuffer) {
            const v = new Uint8Array(event.data);
            if (v.length >= 28 && v[0] === 0xBB) {
                handleLfoScopeBinary(v);
                return;
            }
            if (typeof handleWaveformBinaryMessage === 'function') {
                handleWaveformBinaryMessage(event);
            }
            return;
        }
        if (typeof event.data !== 'string') return;
        const data = JSON.parse(event.data);
        // Handle bulk ACK for MIDI import
        if (data.type === 'bulkAck' && typeof window._bulkAckCallback === 'function') {
            window._bulkAckCallback(data.p);
            return;
        }
        handleWebSocketMessage(data);
    };
}

function handleWebSocketMessage(data) {
    switch(data.type) {
        case 'loopState':
            padLoopState[data.track] = {
                active: data.active,
                paused: data.paused,
                loopType: data.loopType !== undefined ? data.loopType : 0
            };
            updatePadLoopVisual(data.track);
            break;
        case 'padFxSet':
        case 'padFxCleared':
            if (data.pad !== undefined) {
                if (data.type === 'padFxCleared') {
                    padFxState[data.pad] = null;
                } else {
                    if (!padFxState[data.pad]) padFxState[data.pad] = {};
                    if (data.fx === 'distortion') { padFxState[data.pad].distortion = data.amount; padFxState[data.pad].distMode = data.mode; }
                    if (data.fx === 'bitcrush') padFxState[data.pad].bitcrush = data.value;
                }
                updatePadFxIndicator(data.pad);
            }
            break;
        case 'trackFxSet':
        case 'trackFxCleared':
            if (data.track !== undefined) {
                if (data.type === 'trackFxCleared') {
                    trackFxState[data.track] = null;
                } else {
                    if (!trackFxState[data.track]) trackFxState[data.track] = {};
                    if (data.fx === 'distortion') { trackFxState[data.track].distortion = data.amount; trackFxState[data.track].distMode = data.mode; }
                    if (data.fx === 'bitcrush') trackFxState[data.track].bitcrush = data.value;
                }
                updateTrackStepDots(data.track);
            }
            break;
        case 'state':
            updateSequencerState(data);
            updateDeviceStats(data);
            if (Array.isArray(data.samples)) {
                applySampleMetadataFromState(data.samples);
            }
            // Load pad filter states (only update DOM if changed)
            if (Array.isArray(data.padFilters)) {
                data.padFilters.forEach((filterType, padIndex) => {
                    if (padIndex < 16 && padFilterState[padIndex] !== filterType) {
                        padFilterState[padIndex] = filterType;
                        updatePadFilterIndicator(padIndex);
                    }
                });
            }
            // Load track filter states (only update if changed)
            if (Array.isArray(data.trackFilters)) {
                data.trackFilters.forEach((filterType, trackIndex) => {
                    if (trackIndex < 16 && trackFilterState[trackIndex] !== filterType) {
                        trackFilterState[trackIndex] = filterType;
                        updateTrackStepDots(trackIndex);
                    }
                });
            }
            break;
        case 'step':
            updateCurrentStep(data.step);
            break;
        case 'songPattern':
            handleSongPatternChange(data.pattern, data.songLength);
            break;
        case 'pad':
            flashPad(data.pad);
            break;
        case 'pattern':
            loadPatternData(data);
            // Actualizar botón activo y nombre del patrón si viene el índice
            if (data.index !== undefined) {
                const patternButtons = document.querySelectorAll('.btn-pattern');
                patternButtons.forEach((btn, idx) => {
                    if (idx === data.index) {
                        btn.classList.add('active');
                        const patternName = btn.textContent.trim();
                        document.getElementById('currentPatternName').textContent = patternName;
                        // Update circular pattern name
                        const circularPatternName = document.getElementById('circularPatternName');
                        if (circularPatternName) {
                            circularPatternName.textContent = patternName;
                        }
                    } else {
                        btn.classList.remove('active');
                    }
                });
            }
            break;
        case 'sampleCounts':
            handleSampleCountsMessage(data);
            break;
        case 'sampleList':
            displaySampleList(data);
            break;
        case 'sampleLoaded':
            updatePadInfo(data);
            // Invalidate waveform cache for this pad
            if (typeof SampleWaveform !== 'undefined' && data.pad !== undefined) {
                SampleWaveform.clearCache(data.pad);
            }
            break;
        case 'trackFilterSet':
            if (data.success) {
                const trackName = padNames[data.track] || `Track ${data.track + 1}`;
                if (window.showToast) {
                    window.showToast(`✅ Filtro aplicado a ${trackName}`, window.TOAST_TYPES.SUCCESS, 1500);
                }
                
                // Update step filter dots
                if (data.filterType !== undefined) {
                    trackFilterState[data.track] = data.filterType;
                    updateTrackStepDots(data.track);
                    saveSeqFxToShared();
                }
            }
            break;
        case 'trackFilterCleared':
            if (window.showToast) {
                const trackName = padNames[data.track] || `Track ${data.track + 1}`;
                window.showToast(`🔄 Filtro eliminado de ${trackName}`, window.TOAST_TYPES.INFO, 1500);
            }
            
            // Remove filter dots from steps
            trackFilterState[data.track] = 0;
            updateTrackStepDots(data.track);
            saveSeqFxToShared();
            break;
        case 'padFilterSet':
            if (data.success && window.showToast) {
                const padName = padNames[data.pad] || `Pad ${data.pad + 1}`;
                window.showToast(`✅ Filtro aplicado a ${padName}`, window.TOAST_TYPES.SUCCESS, 1500);
            }
            break;
        case 'padFilterCleared':
            if (window.showToast) {
                const padName = padNames[data.pad] || `Pad ${data.pad + 1}`;
                window.showToast(`🔄 Filtro eliminado de ${padName}`, window.TOAST_TYPES.INFO, 1500);
            }
            // Remove badge from pad element
            const padElement = document.querySelector(`.pad[data-pad="${data.pad}"]`);
            if (padElement) {
                const badge = padElement.querySelector('.pad-filter-badge');
                if (badge) badge.remove();
            }
            break;
        case 'stepVelocitySet':
            // Update velocity in step element
            const stepEl = document.querySelector(`[data-track="${data.track}"][data-step="${data.step}"]`);
            if (stepEl) {
                stepEl.dataset.velocity = data.velocity;
            }
            break;
        case 'stepVelocity':
            // Response to getStepVelocity query
            break;
        case 'filterPresets':
            // Store filter presets for future use
            if (data.presets) {
                window.filterPresets = data.presets;
            }
            break;
        case 'trackVolumeSet':
            // Update track volume
            if (data.track !== undefined && data.volume !== undefined) {
                updateTrackVolume(data.track, data.volume);
            }
            break;
        case 'trackVolumes':
            // Initial track volumes state
            if (Array.isArray(data.volumes)) {
                data.volumes.forEach((volume, track) => {
                    if (track < 16) {
                        updateTrackVolume(track, volume);
                    }
                });
            }
            break;
        case 'trackMuted':
            // Sync mute state from server (for multi-client sync)
            if (data.track !== undefined && data.muted !== undefined) {
                setTrackMuted(data.track, data.muted, false); // false = don't send back to server
            }
            break;
        case 'midiDevice':
            handleMIDIDeviceMessage(data);
            break;
        case 'midiMessage':
            handleMIDIMessage(data);
            break;
        case 'uploadProgress':
            handleUploadProgress(data);
            break;
        case 'uploadComplete':
            handleUploadComplete(data);
            break;
        case 'midiScan':
            handleMidiScanState(data);
            break;

        // ============= UDP→WS SYNC HANDLERS =============
        case 'playState':
            isPlaying = !!data.playing;
            updateSequencerStatusMeter();
            break;

        case 'tempoChange':
            if (data.tempo !== undefined) {
                const _ts = document.getElementById('tempoSlider');
                const _tv = document.getElementById('tempoValue');
                if (_ts) _ts.value = String(data.tempo);
                if (_tv) _tv.textContent = String(data.tempo);
                updateBpmMeter(parseFloat(data.tempo));
            }
            break;

        case 'stepSet':
            if (data.track !== undefined && data.step !== undefined) {
                const stepEl = document.querySelector(`.step-btn[data-track="${data.track}"][data-step="${data.step}"]`);
                if (stepEl) stepEl.classList.toggle('active', !!data.active);
                // Also update grid-based seq-step (the primary grid)
                const seqStepEl = document.querySelector(`.seq-step[data-track="${data.track}"][data-step="${data.step}"]`);
                if (seqStepEl) {
                    seqStepEl.classList.toggle('active', !!data.active);
                    if (data.noteLen) {
                        seqStepEl.dataset.notelen = String(data.noteLen);
                        _noteLenLabel(seqStepEl);
                    }
                }
            }
            break;

        case 'patternCleared':
            // Refresh pattern grid from server
            sendWebSocket({ cmd: 'getPattern' });
            break;

        case 'masterFx':
            handleMasterFxUpdate(data);
            break;

        case 'trackFxUpdate':
            handleTrackFxUpdate(data);
            break;

        case 'allStopped':
            isPlaying = false;
            updateSequencerStatusMeter();
            break;

        case 'ledMode':
            // LED mono mode changed from slave, UI seldom shows this
            break;

        case 'trackLiveFx':
            // Per-track live FX from backend (echo/flanger/compressor)
            if (data.track !== undefined && data.fx) {
                const s = trackLiveFxState[data.track];
                if (s) {
                    if (data.fx === 'echo')       { s.echo.active = !!data.active; if (data.time !== undefined) s.echo.time = data.time; if (data.feedback !== undefined) s.echo.feedback = data.feedback; if (data.mix !== undefined) s.echo.mix = data.mix; }
                    if (data.fx === 'flanger')    { s.flanger.active = !!data.active; if (data.rate !== undefined) s.flanger.rate = data.rate; if (data.depth !== undefined) s.flanger.depth = data.depth; if (data.feedback !== undefined) s.flanger.feedback = data.feedback; }
                    if (data.fx === 'compressor') { s.compressor.active = !!data.active; if (data.threshold !== undefined) s.compressor.threshold = data.threshold; if (data.ratio !== undefined) s.compressor.ratio = data.ratio; }
                }
                updateTrackStepDots(data.track);
                saveSeqFxToShared();
            }
            break;

        case 'xtraSampleList':
            // Handle XTRA sample library list
            if (typeof window._handleXtraSampleList === 'function') {
                window._handleXtraSampleList(data);
            }
            break;

        // ═══ Daisy SD Card messages ═══
        case 'sdKitList':
            sdRenderKitList(data.kits || [], data.error);
            break;
        case 'sdFolderList':
            sdRenderFolders(data.folders || []);
            break;
        case 'sdFileList':
            sdRenderFiles(data.folder, data.files || []);
            break;
        case 'sdStatus':
            sdRenderStatus(data);
            break;
        case 'sdLoadKitAck':
            sdLog(`Kit "${data.kit}" ${data.ok ? 'loading...' : 'FAILED'}`);
            break;
        case 'sdLoadSampleAck':
            sdLog(`Pad ${data.pad} ← ${data.file} ${data.ok ? '✓' : '✗'}`);
            break;
        case 'sdUnloadKitAck':
            sdLog('Kit unloaded');
            sdRefreshStatus();
            break;
        case 'sdAbortAck':
            sdLog('Load aborted');
            break;
        case 'sdEvent':
            sdHandleEvent(data);
            break;

        // ═══ LFO messages ═══
        case 'lfoState':
            lfoHandleFullState(data.pads || []);
            break;
        case 'lfoResetAck':
            lfoHandleReset();
            break;
    }
    
    // Call keyboard controls handler if function exists
    if (typeof window.handleKeyboardWebSocketMessage === 'function') {
        window.handleKeyboardWebSocketMessage(data);
    }
}

// ============= MASTER FX UPDATE FROM UDP/WS =============
function handleMasterFxUpdate(data) {
    const p = data.param;
    const v = data.value;
    const byId = (id) => document.getElementById(id);

    // --- Global Filter ---
    if (p === 'filterType') {
        const sel = byId('filterType'); if (sel) sel.value = v;
    } else if (p === 'filterCutoff') {
        const sl = byId('filterCutoff'); if (sl) { sl.value = v; const vd = byId('filterCutoffValue'); if (vd) vd.textContent = Math.round(v); }
    } else if (p === 'filterResonance') {
        const sl = byId('filterResonance'); if (sl) { sl.value = v; const vd = byId('filterResonanceValue'); if (vd) vd.textContent = parseFloat(v).toFixed(1); }
    } else if (p === 'bitCrush') {
        const sl = byId('bitCrush'); if (sl) { sl.value = v; const vd = byId('bitCrushValue'); if (vd) vd.textContent = v; }
    } else if (p === 'distortion') {
        const sl = byId('distortion'); if (sl) { sl.value = v; const vd = byId('distortionValue'); if (vd) vd.textContent = v; }
    } else if (p === 'distortionMode') {
        const sel = byId('distortionMode'); if (sel) sel.value = v;
    } else if (p === 'sampleRate') {
        const sl = byId('sampleRate'); if (sl) { sl.value = v; const vd = byId('sampleRateValue'); if (vd) vd.textContent = v; }
    }

    // --- Delay ---
    else if (p === 'delayActive') { const cb = byId('delayActive'); if (cb) cb.checked = !!v; }
    else if (p === 'delayTime') { const sl = byId('delayTime'); if (sl) { sl.value = v; const vd = byId('delayTimeValue'); if (vd) vd.textContent = Math.round(v); } }
    else if (p === 'delayFeedback') { const sl = byId('delayFeedback'); if (sl) { sl.value = v; const vd = byId('delayFeedbackValue'); if (vd) vd.textContent = Math.round(v); } }
    else if (p === 'delayMix') { const sl = byId('delayMix'); if (sl) { sl.value = v; const vd = byId('delayMixValue'); if (vd) vd.textContent = Math.round(v); } }

    // --- Phaser ---
    else if (p === 'phaserActive') { const cb = byId('phaserActive'); if (cb) cb.checked = !!v; }
    else if (p === 'phaserRate') { const sl = byId('phaserRate'); if (sl) { sl.value = v; const vd = byId('phaserRateValue'); if (vd) vd.textContent = Math.round(v); } }
    else if (p === 'phaserDepth') { const sl = byId('phaserDepth'); if (sl) { sl.value = v; const vd = byId('phaserDepthValue'); if (vd) vd.textContent = Math.round(v); } }
    else if (p === 'phaserFeedback') { const sl = byId('phaserFeedback'); if (sl) { sl.value = v; const vd = byId('phaserFeedbackValue'); if (vd) vd.textContent = Math.round(v); } }

    // --- Flanger ---
    else if (p === 'flangerActive') { const cb = byId('flangerActive'); if (cb) cb.checked = !!v; }
    else if (p === 'flangerRate') { const sl = byId('flangerRate'); if (sl) { sl.value = v; const vd = byId('flangerRateValue'); if (vd) vd.textContent = Math.round(v); } }
    else if (p === 'flangerDepth') { const sl = byId('flangerDepth'); if (sl) { sl.value = v; const vd = byId('flangerDepthValue'); if (vd) vd.textContent = Math.round(v); } }
    else if (p === 'flangerFeedback') { const sl = byId('flangerFeedback'); if (sl) { sl.value = v; const vd = byId('flangerFeedbackValue'); if (vd) vd.textContent = Math.round(v); } }
    else if (p === 'flangerMix') { const sl = byId('flangerMix'); if (sl) { sl.value = v; const vd = byId('flangerMixValue'); if (vd) vd.textContent = Math.round(v); } }

    // --- Compressor ---
    else if (p === 'compressorActive') { const cb = byId('compressorActive'); if (cb) cb.checked = !!v; }
    else if (p === 'compressorThreshold') { const sl = byId('compressorThreshold'); if (sl) { sl.value = v; const vd = byId('compressorThresholdValue'); if (vd) vd.textContent = Math.round(v); } }
    else if (p === 'compressorRatio') { const sl = byId('compressorRatio'); if (sl) { sl.value = v; const vd = byId('compressorRatioValue'); if (vd) vd.textContent = parseFloat(v).toFixed(1); } }
    else if (p === 'compressorAttack') { const sl = byId('compressorAttack'); if (sl) { sl.value = v; const vd = byId('compressorAttackValue'); if (vd) vd.textContent = Math.round(v); } }
    else if (p === 'compressorRelease') { const sl = byId('compressorRelease'); if (sl) { sl.value = v; const vd = byId('compressorReleaseValue'); if (vd) vd.textContent = Math.round(v); } }
    else if (p === 'compressorMakeupGain') { const sl = byId('compressorMakeupGain'); if (sl) { sl.value = v; const vd = byId('compressorMakeupGainValue'); if (vd) vd.textContent = parseFloat(v).toFixed(1); } }

    // --- Master Volume ---
    else if (p === 'volume') {
        const sl = byId('masterVolume'); if (sl) { sl.value = v; const vd = byId('masterVolumeValue'); if (vd) vd.textContent = v; }
    }
    // --- Live Pitch ---
    else if (p === 'livePitch') {
        const sl = byId('livePitchSlider'); if (sl) { sl.value = v; const vd = byId('livePitchValue'); if (vd) vd.textContent = parseFloat(v).toFixed(2); }
    }
}

// ============= TRACK FX UPDATE FROM UDP/WS =============
function handleTrackFxUpdate(data) {
    const track = data.track !== undefined ? data.track : data.pad;
    if (track === undefined || track < 0) return;

    if (data.fx === 'reverse') {
        if (track < 16 && typeof trackFxEffects !== 'undefined') {
            trackFxEffects[track].reverse = !!data.value;
        }
    } else if (data.fx === 'pitch') {
        if (track < 16 && typeof trackFxEffects !== 'undefined') {
            trackFxEffects[track].pitch = parseFloat(data.value);
        }
    } else if (data.fx === 'stutter') {
        if (track < 16 && typeof trackFxEffects !== 'undefined') {
            trackFxEffects[track].stutter = !!data.value;
            trackFxEffects[track].stutterMs = data.interval || 100;
        }
    }
    // Only update the detailed FX UI if this track is currently viewed
    if (track === selectedFxTrack && typeof updateTrackFxUI === 'function') {
        updateTrackFxUI();
    }
    updateTrackFxStatusGrid();
    updateTrackFxBtnIndicators();
}

function loadPatternData(data) {
    // Clear circular data
    circularSequencerData = Array.from({ length: 16 }, () => Array(16).fill(false));
    
    // Build a set of steps that should be active
    const shouldBeActive = new Set();
    
    for (let track = 0; track < 16; track++) {
        const trackData = data[track] || data[track.toString()];
        if (trackData) {
            trackData.forEach((active, step) => {
                if (active) {
                    shouldBeActive.add(`${track}-${step}`);
                    if (circularSequencerData[track]) {
                        circularSequencerData[track][step] = true;
                    }
                }
            });
        }
    }
    
    // Single pass: only toggle steps that changed
    document.querySelectorAll('.seq-step').forEach(el => {
        const key = `${el.dataset.track}-${el.dataset.step}`;
        const wantActive = shouldBeActive.has(key);
        const isActive = el.classList.contains('active');
        if (wantActive && !isActive) {
            el.classList.add('active');
        } else if (!wantActive && isActive) {
            el.classList.remove('active');
        }
    });
    
    // Cargar velocidades si están disponibles
    if (data.velocities) {
        for (let track = 0; track < 16; track++) {
            const velData = data.velocities[track] || data.velocities[track.toString()];
            if (velData && Array.isArray(velData)) {
                velData.forEach((velocity, step) => {
                    const stepEl = document.querySelector(`[data-track="${track}"][data-step="${step}"]`);
                    if (stepEl && stepEl.classList.contains('active')) {
                        stepEl.dataset.velocity = velocity;
                    }
                });
            }
        }
    }
    
    // Cargar duraciones de nota si están disponibles
    if (data.noteLens) {
        for (let track = 0; track < 16; track++) {
            const nlData = data.noteLens[track] || data.noteLens[track.toString()];
            if (nlData && Array.isArray(nlData)) {
                nlData.forEach((div, step) => {
                    const stepEl = document.querySelector(`[data-track="${track}"][data-step="${step}"]`);
                    if (stepEl) {
                        stepEl.dataset.notelen = String(div || 1);
                        _noteLenLabel(stepEl);
                    }
                });
            }
        }
    }
}

function updateStatus(connected) {
    const dot = document.getElementById('statusDot');
    const text = document.getElementById('statusText');
    
    if (connected) {
        dot.classList.add('connected');
        text.textContent = 'Conectado';
    } else {
        dot.classList.remove('connected');
        text.textContent = 'Desconectado';
    }
}

// Loop Types
const LOOP_TYPES = [
    { id: 0, name: 'EVERY STEP', icon: '🔁', desc: 'Trigger en cada step (16th)' },
    { id: 1, name: 'EVERY BEAT', icon: '🥁', desc: 'Trigger cada compás (quarter)' },
    { id: 2, name: '2x BEAT', icon: '⚡', desc: '2 triggers por compás (8th)' },
    { id: 3, name: 'ARRHYTHMIC', icon: '🎲', desc: 'Triggers aleatorios' }
];

// Show loop type popup for a pad
function showLoopTypePopup(padIndex) {
    if (!isConnected) return;
    
    // If already looping, just toggle off
    const currentState = padLoopState[padIndex];
    if (currentState && currentState.active) {
        sendWebSocket({ cmd: 'toggleLoop', track: padIndex });
        closeLoopTypePopup();
        return;
    }
    
    // Remove any existing popup
    closeLoopTypePopup();
    
    const backdrop = document.createElement('div');
    backdrop.id = 'loopPopupBackdrop';
    backdrop.className = 'loop-popup-backdrop';
    backdrop.addEventListener('click', closeLoopTypePopup);
    
    const popup = document.createElement('div');
    popup.id = 'loopPopupModal';
    popup.className = 'loop-popup-modal';
    
    const padName = padNames[padIndex] || `Pad ${padIndex + 1}`;
    popup.innerHTML = `
        <div class="loop-popup-header">
            <span class="loop-popup-title">🔁 LOOP: ${padName}</span>
            <button class="loop-popup-close" onclick="closeLoopTypePopup()">&times;</button>
        </div>
        <div class="loop-popup-options">
            ${LOOP_TYPES.map(lt => `
                <button class="loop-type-btn" data-loop-type="${lt.id}" onclick="activateLoop(${padIndex}, ${lt.id})">
                    <span class="loop-type-icon">${lt.icon}</span>
                    <span class="loop-type-name">${lt.name}</span>
                    <span class="loop-type-desc">${lt.desc}</span>
                </button>
            `).join('')}
        </div>
    `;
    
    document.body.appendChild(backdrop);
    document.body.appendChild(popup);
    
    requestAnimationFrame(() => {
        backdrop.classList.add('visible');
        popup.classList.add('visible');
    });
}

function activateLoop(padIndex, loopType) {
    sendWebSocket({ cmd: 'toggleLoop', track: padIndex, loopType: loopType });
    closeLoopTypePopup();
}

function closeLoopTypePopup() {
    const backdrop = document.getElementById('loopPopupBackdrop');
    const popup = document.getElementById('loopPopupModal');
    if (popup) { popup.classList.remove('visible'); popup.classList.add('closing'); }
    if (backdrop) { backdrop.classList.remove('visible'); }
    setTimeout(() => {
        if (backdrop) backdrop.remove();
        if (popup) popup.remove();
    }, 300);
}

function updateLoopButtonState(padIndex) {
    // Main pads loop button
    let loopBtn = document.querySelector(`.loop-btn[data-pad="${padIndex}"]`);
    // XTRA pads loop button
    if (!loopBtn) {
        const xtraPad = document.querySelector(`.pad-xtra[data-pad-index="${padIndex}"]`);
        if (xtraPad) loopBtn = xtraPad.querySelector('.xtra-loop');
    }
    if (!loopBtn) return;
    
    const state = padLoopState[padIndex];
    if (state && state.active) {
        loopBtn.classList.add('active');
        if (state.paused) {
            loopBtn.classList.add('paused');
        } else {
            loopBtn.classList.remove('paused');
        }
    } else {
        loopBtn.classList.remove('active', 'paused');
    }
}

// Create Pads
function createPads() {
    const grid = document.getElementById('padsGrid');
    
    const families = padNames;
    
    for (let i = 0; i < 16; i++) {
        const padContainer = document.createElement('div');
        padContainer.className = 'pad-container';
        
        const pad = document.createElement('div');
        pad.className = 'pad';
        pad.dataset.pad = i;
        
        pad.innerHTML = `
            <button class="pad-upload-btn" data-pad="${i}" title="Load Sample">+</button>
            <button class="pad-filter-btn" data-pad="${i}" title="Filter">F</button>
            <button class="pad-fx-btn" data-pad="${i}" title="FX (Distortion/BitCrush)">🎸</button>
            <button class="loop-btn" data-pad="${i}" title="Toggle Loop">🔁</button>
            <div class="pad-content">
                <div class="pad-name">${padNames[i]}</div>
                <div class="pad-sample-info" id="sampleInfo-${i}"><span class="sample-file">...</span><span class="sample-quality">44.1k•16b•M</span></div>
                <span class="pad-filter-indicator" data-pad="${i}" style="display:none;"></span>
            </div>
            <div class="pad-corona" aria-hidden="true"></div>
        `;
        
        const keyLabel = padKeyBindings[i];
        if (keyLabel) {
            const keyHint = document.createElement('div');
            keyHint.className = 'pad-key-hint';
            keyHint.textContent = keyLabel;
            pad.appendChild(keyHint);
        }
        
        // Touch y click con tremolo
        pad.addEventListener('touchstart', (e) => {
            e.preventDefault();
            startTremolo(i, pad);
        });
        
        pad.addEventListener('touchend', (e) => {
            e.preventDefault();
            stopTremolo(i, pad);
        });
        
        pad.addEventListener('mousedown', () => {
            startTremolo(i, pad);
        });
        
        pad.addEventListener('mouseup', () => {
            stopTremolo(i, pad);
        });
        
        pad.addEventListener('mouseleave', () => {
            stopTremolo(i, pad);
        });
        
        // Event listener para botón de upload
        const uploadBtn = pad.querySelector('.pad-upload-btn');
        if (uploadBtn) {
            uploadBtn.addEventListener('touchstart', (e) => {
                e.stopPropagation();
            });
            uploadBtn.addEventListener('touchend', (e) => {
                e.preventDefault();
                e.stopPropagation();
                showUploadDialog(i);
            });
            uploadBtn.addEventListener('click', (e) => {
                e.preventDefault();
                e.stopPropagation();
                showUploadDialog(i);
            });
        }
        
        // Event listener para botón F de filtro
        const filterBtn = pad.querySelector('.pad-filter-btn');
        if (filterBtn) {
            filterBtn.addEventListener('touchstart', (e) => {
                e.stopPropagation();
            });
            filterBtn.addEventListener('touchend', (e) => {
                e.preventDefault();
                e.stopPropagation();
                showPadFilterSelector(i, pad);
            });
            filterBtn.addEventListener('click', (e) => {
                e.preventDefault();
                e.stopPropagation();
                showPadFilterSelector(i, pad);
            });
        }
        
        // Event listener para botón FX (Distortion/BitCrush)
        const fxBtn = pad.querySelector('.pad-fx-btn');
        if (fxBtn) {
            fxBtn.addEventListener('touchstart', (e) => {
                e.stopPropagation();
            });
            fxBtn.addEventListener('touchend', (e) => {
                e.preventDefault();
                e.stopPropagation();
                showPadFxPopup(i, pad);
            });
            fxBtn.addEventListener('click', (e) => {
                e.preventDefault();
                e.stopPropagation();
                showPadFxPopup(i, pad);
            });
        }
        
        // Event listener para botón de loop
        const loopBtn = pad.querySelector('.loop-btn');
        if (loopBtn) {
            loopBtn.addEventListener('touchstart', (e) => {
                e.stopPropagation();
            });
            loopBtn.addEventListener('touchend', (e) => {
                e.preventDefault();
                e.stopPropagation();
                showLoopTypePopup(i);
            });
            loopBtn.addEventListener('click', (e) => {
                e.preventDefault();
                e.stopPropagation();
                showLoopTypePopup(i);
            });
        }
        
        // Botón para seleccionar sample (se añade después según count)
        const selectBtn = document.createElement('button');
        selectBtn.className = 'pad-select-btn';
        selectBtn.style.display = 'none';  // Oculto por defecto
        selectBtn.dataset.padIndex = i;
        selectBtn.dataset.family = families[i];
        selectBtn.addEventListener('click', (e) => {
            e.preventDefault();
            e.stopPropagation();
            showSampleSelector(i, families[i]);
        });

        
        padContainer.appendChild(pad);
        padContainer.appendChild(selectBtn);
        grid.appendChild(padContainer);

        refreshPadSampleInfo(i);
    }
}

function startTremolo(padIndex, padElement) {
    // Trigger IMMEDIATELY - zero delay
    triggerPad(padIndex);
    padElement.style.animation = 'padRipple 0.3s ease-out';
    
    setTimeout(() => { padElement.style.animation = ''; }, 300);
    
    // Ultra-fast tremolo: 55ms repeat (~18/sec), starting after only 100ms hold
    tremoloIntervals[padIndex] = setTimeout(() => {
        padElement.classList.add('tremolo-active');
        
        tremoloIntervals[padIndex] = setInterval(() => {
            triggerPad(padIndex);
            padElement.style.filter = 'brightness(1.35)';
            setTimeout(() => {
                padElement.style.filter = 'brightness(1.1)';
            }, 22);
        }, 55); // 55ms = ~18 triggers/segundo
    }, 100); // Solo 100ms de delay antes de tremolo
}

function stopTremolo(padIndex, padElement) {
    // Detener
    // Detener cualquier intervalo o timeout de tremolo
    if (tremoloIntervals[padIndex]) {
        clearTimeout(tremoloIntervals[padIndex]);
        clearInterval(tremoloIntervals[padIndex]);
        delete tremoloIntervals[padIndex];
    }
    
    // Limpiar estados visuales
    padElement.classList.remove('active');
    padElement.classList.remove('tremolo-active');
    padElement.style.filter = '';
    padElement.style.animation = '';
}

function startKeyboardTremolo(padIndex, padElement) {
    stopKeyboardTremolo(padIndex, padElement);
    if (!padElement) return;

    // Ultra-low latency engine using performance.now() + requestAnimationFrame
    const state = {
        startTime: performance.now(),
        lastTrigger: 0,
        currentRate: 55,     // Start at 55ms (~18 hits/sec) - ultra fast
        minRate: 18,         // Minimum 18ms (~55 hits/sec) - machine gun
        rafId: null,
        alive: true
    };
    keyboardTremoloState[padIndex] = state;
    padElement.classList.add('keyboard-tremolo');

    // First trigger IMMEDIATELY with zero delay
    triggerPad(padIndex);
    padElement.classList.add('active');
    padElement.style.filter = 'brightness(1.5)';
    state.lastTrigger = performance.now();

    const tick = (now) => {
        if (!state.alive) return;
        const elapsed = now - state.lastTrigger;
        if (elapsed >= state.currentRate) {
            triggerPad(padIndex);
            // Visual flash (minimal DOM work for speed)
            padElement.style.filter = 'brightness(1.5)';
            setTimeout(() => { if (state.alive) padElement.style.filter = 'brightness(1.15)'; }, 25);
            state.lastTrigger = now;
            // Accelerate: exponential ramp from 55ms to 18ms
            const holdTime = now - state.startTime;
            state.currentRate = Math.max(state.minRate, 55 * Math.pow(0.82, holdTime / 120));
        }
        state.rafId = requestAnimationFrame(tick);
    };
    state.rafId = requestAnimationFrame(tick);
}

function stopKeyboardTremolo(padIndex, padElement) {
    const state = keyboardTremoloState[padIndex];
    if (state) {
        state.alive = false;
        if (state.rafId) cancelAnimationFrame(state.rafId);
    }
    delete keyboardTremoloState[padIndex];

    if (padElement) {
        padElement.classList.remove('keyboard-tremolo');
        padElement.classList.remove('active');
        padElement.style.filter = '';
    }
}

// Show filter selector overlay for pad
function showPadFilterSelector(padIndex, padElement) {
    // Remove any existing modal
    closePadFilterModal();
    
    // Create backdrop
    const backdrop = document.createElement('div');
    backdrop.className = 'pfe-backdrop';
    backdrop.id = 'pfeBackdrop';
    backdrop.addEventListener('click', closePadFilterModal);
    
    // Create centered modal
    const modal = document.createElement('div');
    modal.className = 'pfe-modal';
    modal.id = 'pfeModal';
    modal.dataset.padIndex = padIndex;
    
    // Header
    const header = document.createElement('div');
    header.className = 'pfe-header';
    header.innerHTML = `
        <span class="pfe-pad-name">🎛️ ${padNames[padIndex] || 'XTRA ' + (padIndex - 15)}</span>
        <span class="pfe-title">SELECT FILTER</span>
        <button class="pfe-close" title="Cerrar">✕</button>
    `;
    header.querySelector('.pfe-close').addEventListener('click', (e) => {
        e.stopPropagation();
        closePadFilterModal();
    });
    
    // Filter grid
    const filterGrid = document.createElement('div');
    filterGrid.className = 'pfe-filter-grid';
    
    FILTER_TYPES.forEach((filter, index) => {
        // Skip pad-only filters (Scratch, Turntablism) when sync is enabled
        if (filter.padOnly && padSeqSyncEnabled) return;
        
        const filterBtn = document.createElement('button');
        filterBtn.className = 'pfe-filter-btn';
        if (filter.padOnly) filterBtn.classList.add('pfe-filter-special');
        filterBtn.dataset.filterType = index;
        if (index === padFilterState[padIndex]) {
            filterBtn.classList.add('active');
        }
        filterBtn.innerHTML = `
            <span class="pfe-icon">${filter.icon}</span>
            <span class="pfe-name">${filter.name}</span>
        `;
        filterBtn.addEventListener('click', (e) => {
            e.stopPropagation();
            setPadFilter(padIndex, index);
            closePadFilterModal();
        });
        filterGrid.appendChild(filterBtn);
    });
    
    modal.appendChild(header);
    modal.appendChild(filterGrid);
    
    document.body.appendChild(backdrop);
    document.body.appendChild(modal);
    
    // Animate in
    requestAnimationFrame(() => {
        backdrop.classList.add('visible');
        modal.classList.add('visible');
    });
}

function closePadFilterModal() {
    const backdrop = document.getElementById('pfeBackdrop');
    const modal = document.getElementById('pfeModal');
    if (modal) {
        modal.classList.remove('visible');
        modal.classList.add('closing');
    }
    if (backdrop) {
        backdrop.classList.remove('visible');
    }
    setTimeout(() => {
        if (backdrop) backdrop.remove();
        if (modal) modal.remove();
    }, 300);
}

// Set filter for a specific pad
function setPadFilter(padIndex, filterType) {
    padFilterState[padIndex] = filterType;
    
    // Update visual indicator
    updatePadFilterIndicator(padIndex);
    
    // Send to ESP32
    if (isConnected) {
        const msg = {
            cmd: 'setPadFilter',
            pad: padIndex,
            filterType: filterType
        };
        ws.send(JSON.stringify(msg));
    }
    
    // Sync: also apply to track if sync enabled (skip pad-only special filters)
    if (padSeqSyncEnabled && padIndex < 16 && filterType <= 9) {
        trackFilterState[padIndex] = filterType;
        syncFilterToTrack(padIndex, filterType);
    }
}

// Sync filter from pad to corresponding track
function syncFilterToTrack(trackIndex, filterType) {
    const filterShortcuts = {
        0: { type: 0, name: 'Clear Filter' },
        1: { type: 1, cutoff: 1000, resonance: 1, name: 'Low Pass' },
        2: { type: 2, cutoff: 1000, resonance: 1, name: 'High Pass' },
        3: { type: 3, cutoff: 1000, resonance: 2, name: 'Band Pass' },
        4: { type: 4, cutoff: 1000, resonance: 2, name: 'Notch' },
        5: { type: 5, cutoff: 1000, resonance: 1, name: 'All Pass' },
        6: { type: 6, cutoff: 1000, resonance: 2, gain: 6, name: 'Peaking' },
        7: { type: 7, cutoff: 500, resonance: 1, gain: 6, name: 'Low Shelf' },
        8: { type: 8, cutoff: 4000, resonance: 1, gain: 6, name: 'High Shelf' },
        9: { type: 9, cutoff: 1000, resonance: 10, name: 'Resonant' }
    };
    
    const filter = filterShortcuts[filterType];
    if (!filter) return;
    
    const cmd = {
        cmd: filter.type === 0 ? 'clearTrackFilter' : 'setTrackFilter',
        track: trackIndex
    };
    if (filter.type !== 0) {
        cmd.filterType = filter.type;
        cmd.cutoff = filter.cutoff;
        cmd.resonance = filter.resonance;
        if (filter.gain !== undefined) cmd.gain = filter.gain;
    }
    sendWebSocket(cmd);
}

// Sync toggles removed — always synced
function setupSyncToggles() {
    // No-op: pads and sequencer tracks are always synced
}

// Expose syncFilterToPad for keyboard-controls to use
// Use _internal name to avoid infinite recursion (global scope resolves bare name to window.)
window.syncFilterToPad = function(padIndex, filterType) {
    if (!padSeqSyncEnabled) return;
    padFilterState[padIndex] = filterType;
    updatePadFilterIndicator(padIndex);
    if (isConnected) {
        ws.send(JSON.stringify({
            cmd: 'setPadFilter',
            pad: padIndex,
            filterType: filterType
        }));
    }
};

// Clear filter for a specific pad
function clearPadFilter(padIndex) {
    setPadFilter(padIndex, 0);
}

// ============= PER-PAD FX POPUP =============
const DISTORTION_MODES = [
    { id: 0, name: 'SOFT CLIP', icon: '🎸', desc: 'Saturación suave analógica' },
    { id: 1, name: 'HARD CLIP', icon: '⚡', desc: 'Recorte duro digital' },
    { id: 2, name: 'TUBE', icon: '🔥', desc: 'Saturación tipo válvula' },
    { id: 3, name: 'FUZZ', icon: '💥', desc: 'Distorsión extrema' }
];

function showPadFxPopup(padIndex, padElement) {
    closePadFxPopup();
    const backdrop = document.createElement('div');
    backdrop.id = 'padFxBackdrop';
    backdrop.className = 'loop-popup-backdrop';
    backdrop.addEventListener('click', closePadFxPopup);

    const cfx  = padFxState[padIndex] || {};
    const clive = padLiveFxState[padIndex] || {};
    const dist  = cfx.distortion || 0;
    const dmode = cfx.distMode || 0;
    const bits  = cfx.bitcrush || 16;
    const rev   = !!clive.reverse;
    const pitch = clive.pitch !== undefined ? clive.pitch : 1.0;
    const stut  = !!clive.stutter;
    const stutMs = clive.stutterMs || 100;

    const padName = padNames[padIndex] || `PAD ${padIndex + 1}`;
    const popup = document.createElement('div');
    popup.id = 'padFxModal';
    popup.className = 'pad-fx-modal';
    popup.innerHTML = `
        <div class="loop-popup-header">
            <span class="loop-popup-title">🎸 FX: ${padName}</span>
            <button class="loop-popup-close" onclick="closePadFxPopup()">&times;</button>
        </div>
        <div class="pad-fx-content">
            <div class="pad-fx-section">
                <h4>🎸 DISTORTION</h4>
                <div class="pad-fx-modes">
                    ${DISTORTION_MODES.map(m => `
                        <button class="loop-type-btn pad-fx-mode-btn ${m.id === dmode ? 'active' : ''}"
                                data-mode="${m.id}" onclick="setPadFxDistMode(${padIndex}, ${m.id})">
                            <span class="loop-type-icon">${m.icon}</span>
                            <span class="loop-type-name">${m.name}</span>
                        </button>`).join('')}
                </div>
                <div class="pad-fx-slider-row">
                    <label>Drive <span id="padFxDriveVal">${dist}</span>%</label>
                    <input type="range" id="padFxDrive" min="0" max="100" value="${dist}"
                           oninput="setPadFxDrive(${padIndex}, this.value)" class="fx-slider">
                </div>
            </div>
            <div class="pad-fx-section">
                <h4>📼 BIT CRUSH</h4>
                <div class="pad-fx-slider-row">
                    <label>Bits <span id="padFxBitsVal">${bits}</span></label>
                    <input type="range" id="padFxBits" min="4" max="16" value="${bits}"
                           oninput="setPadFxBits(${padIndex}, this.value)" class="fx-slider">
                </div>
            </div>
            <div class="pad-fx-section">
                <h4>⏪ REVERSE</h4>
                <button class="fx-toggle-btn ${rev ? 'fx-on' : ''}" id="padRevBtn"
                        onclick="setPadFxReverse(${padIndex}, !${rev})">
                    ${rev ? '⏪ ON' : '▶️ OFF'}
                </button>
            </div>
            <div class="pad-fx-section">
                <h4>🎵 PITCH SHIFT</h4>
                <div class="pad-fx-slider-row">
                    <label>Pitch <span id="padFxPitchVal">${pitch.toFixed(2)}</span>×</label>
                    <input type="range" id="padFxPitch" min="25" max="200" value="${Math.round(pitch*100)}"
                           oninput="setPadFxPitch(${padIndex}, this.value/100)" class="fx-slider">
                </div>
                <div class="pad-fx-modes" style="grid-template-columns:repeat(4,1fr);margin-top:6px">
                    ${[0.25,0.5,0.75,1.0,1.25,1.5,2.0].map(v=>`
                        <button class="pitch-preset-btn ${Math.abs(pitch-v)<0.01?'active':''}"
                                onclick="setPadFxPitch(${padIndex},${v})">${v}×</button>`).join('')}
                </div>
            </div>
            <div class="pad-fx-section">
                <h4>🔁 STUTTER</h4>
                <button class="fx-toggle-btn ${stut ? 'fx-on' : ''}" id="padStutBtn"
                        onclick="setPadFxStutterToggle(${padIndex}, !${stut})">
                    ${stut ? '🔁 ON' : '🔁 OFF'}
                </button>
                <div class="pad-fx-slider-row" style="margin-top:8px">
                    <label>Interval <span id="padFxStutVal">${stutMs}</span>ms</label>
                    <input type="range" id="padFxStutMs" min="20" max="500" value="${stutMs}"
                           oninput="setPadFxStutterMs(${padIndex}, this.value)" class="fx-slider">
                </div>
            </div>
            <button class="pad-fx-clear-btn" onclick="clearPadFxAll(${padIndex})">🚫 CLEAR ALL FX</button>
        </div>
    `;
    document.body.appendChild(backdrop);
    document.body.appendChild(popup);
    requestAnimationFrame(() => { backdrop.classList.add('visible'); popup.classList.add('visible'); });
}

function closePadFxPopup() {
    const backdrop = document.getElementById('padFxBackdrop');
    const popup = document.getElementById('padFxModal');
    if (popup) { popup.classList.remove('visible'); popup.classList.add('closing'); }
    if (backdrop) { backdrop.classList.remove('visible'); }
    setTimeout(() => { if (backdrop) backdrop.remove(); if (popup) popup.remove(); }, 300);
}

function setPadFxDistMode(padIndex, mode) {
    if (!padFxState[padIndex]) padFxState[padIndex] = {};
    padFxState[padIndex].distMode = mode;
    const drive = padFxState[padIndex].distortion || 0;
    sendWebSocket({ cmd: 'setPadDistortion', pad: padIndex, amount: drive, mode: mode });
    // Update active state visually
    document.querySelectorAll('#padFxModal .pad-fx-mode-btn').forEach(btn => {
        btn.classList.toggle('active', parseInt(btn.dataset.mode) === mode);
    });
    // Sync to track if enabled
    if (padSeqSyncEnabled && padIndex < 16) {
        if (!trackFxState[padIndex]) trackFxState[padIndex] = {};
        trackFxState[padIndex].distMode = mode;
        trackFxState[padIndex].distortion = drive;
        sendWebSocket({ cmd: 'setTrackDistortion', track: padIndex, amount: drive, mode: mode });
    }
}

function setPadFxDrive(padIndex, value) {
    const val = parseInt(value);
    if (!padFxState[padIndex]) padFxState[padIndex] = {};
    padFxState[padIndex].distortion = val;
    const mode = padFxState[padIndex].distMode || 0;
    document.getElementById('padFxDriveVal').textContent = val;
    sendWebSocket({ cmd: 'setPadDistortion', pad: padIndex, amount: val, mode: mode });
    updatePadFxIndicator(padIndex);
    // Sync to track if enabled
    if (padSeqSyncEnabled && padIndex < 16) {
        if (!trackFxState[padIndex]) trackFxState[padIndex] = {};
        trackFxState[padIndex].distortion = val;
        trackFxState[padIndex].distMode = mode;
        sendWebSocket({ cmd: 'setTrackDistortion', track: padIndex, amount: val, mode: mode });
    }
}

function setPadFxBits(padIndex, value) {
    const val = parseInt(value);
    if (!padFxState[padIndex]) padFxState[padIndex] = {};
    padFxState[padIndex].bitcrush = val;
    document.getElementById('padFxBitsVal').textContent = val;
    sendWebSocket({ cmd: 'setPadBitCrush', pad: padIndex, value: val });
    updatePadFxIndicator(padIndex);
    // Sync to track if enabled
    if (padSeqSyncEnabled && padIndex < 16) {
        if (!trackFxState[padIndex]) trackFxState[padIndex] = {};
        trackFxState[padIndex].bitcrush = val;
        sendWebSocket({ cmd: 'setTrackBitCrush', track: padIndex, value: val });
    }
}

// === PAD FX — Reverse / Pitch / Stutter ===
function setPadFxReverse(padIndex, val) {
    if (!padLiveFxState[padIndex]) padLiveFxState[padIndex] = { reverse: false, pitch: 1.0, stutter: false, stutterMs: 100 };
    padLiveFxState[padIndex].reverse = val;
    sendWebSocket({ cmd: 'setReverse', pad: padIndex, value: val });
    updatePadFxIndicator(padIndex);
    const btn = document.getElementById('padRevBtn');
    if (btn) { btn.textContent = val ? '⏪ ON' : '▶️ OFF'; btn.classList.toggle('fx-on', val); }
}
function setPadFxPitch(padIndex, val) {
    val = parseFloat(val);
    if (!padLiveFxState[padIndex]) padLiveFxState[padIndex] = { reverse: false, pitch: 1.0, stutter: false, stutterMs: 100 };
    padLiveFxState[padIndex].pitch = val;
    sendWebSocket({ cmd: 'setPitchShift', pad: padIndex, value: val });
    updatePadFxIndicator(padIndex);
    const vEl = document.getElementById('padFxPitchVal');
    if (vEl) vEl.textContent = val.toFixed(2);
    document.querySelectorAll('#padFxModal .pitch-preset-btn').forEach(b => {
        b.classList.toggle('active', Math.abs(parseFloat(b.textContent) - val) < 0.01);
    });
}
function setPadFxStutterToggle(padIndex, val) {
    if (!padLiveFxState[padIndex]) padLiveFxState[padIndex] = { reverse: false, pitch: 1.0, stutter: false, stutterMs: 100 };
    padLiveFxState[padIndex].stutter = val;
    const ms = padLiveFxState[padIndex].stutterMs || 100;
    sendWebSocket({ cmd: 'setStutter', pad: padIndex, active: val, interval: ms });
    updatePadFxIndicator(padIndex);
    const btn = document.getElementById('padStutBtn');
    if (btn) { btn.textContent = val ? '🔁 ON' : '🔁 OFF'; btn.classList.toggle('fx-on', val); }
}
function setPadFxStutterMs(padIndex, ms) {
    ms = parseInt(ms);
    if (!padLiveFxState[padIndex]) padLiveFxState[padIndex] = { reverse: false, pitch: 1.0, stutter: false, stutterMs: 100 };
    padLiveFxState[padIndex].stutterMs = ms;
    const active = padLiveFxState[padIndex].stutter;
    sendWebSocket({ cmd: 'setStutter', pad: padIndex, active: active, interval: ms });
    const vEl = document.getElementById('padFxStutVal');
    if (vEl) vEl.textContent = ms;
}

function clearPadFxAll(padIndex) {
    padFxState[padIndex] = null;
    if (padLiveFxState[padIndex]) {
        padLiveFxState[padIndex] = { reverse: false, pitch: 1.0, stutter: false, stutterMs: 100 };
    }
    sendWebSocket({ cmd: 'clearPadFX', pad: padIndex });
    sendWebSocket({ cmd: 'setReverse', pad: padIndex, value: false });
    sendWebSocket({ cmd: 'setPitchShift', pad: padIndex, value: 1.0 });
    sendWebSocket({ cmd: 'setStutter', pad: padIndex, active: false, interval: 100 });
    updatePadFxIndicator(padIndex);
    closePadFxPopup();
    if (padSeqSyncEnabled && padIndex < 16) {
        trackFxState[padIndex] = null;
        sendWebSocket({ cmd: 'clearTrackFX', track: padIndex });
    }
}

function updatePadFxIndicator(padIndex) {
    const fx   = padFxState[padIndex];
    const live = padLiveFxState[padIndex];
    const hasFx = !!(
        (fx && ((fx.distortion > 0) || (fx.bitcrush !== undefined && fx.bitcrush < 16))) ||
        (live && (live.reverse || live.pitch !== 1.0 || live.stutter))
    );
    let pad = document.querySelector(`.pad[data-pad="${padIndex}"]`);
    if (!pad) {
        const xtraPad = document.querySelector(`.pad-xtra[data-pad-index="${padIndex}"]`);
        if (xtraPad) {
            const fxBtn = xtraPad.querySelector('.xtra-fx');
            if (fxBtn) fxBtn.classList.toggle('active', hasFx);
            return;
        }
        return;
    }
    let badge = pad.querySelector('.pad-fx-badge');
    if (hasFx) {
        if (!badge) {
            badge = document.createElement('span');
            badge.className = 'pad-fx-badge';
            pad.appendChild(badge);
        }
        badge.textContent = '🎸';
        badge.style.display = 'block';
    } else if (badge) {
        badge.style.display = 'none';
    }
}

// === TRACK FX — Reverse / Pitch / Stutter ===
function setTrackFxReverse(trackIndex, val) {
    if (!trackFxEffects[trackIndex]) trackFxEffects[trackIndex] = { reverse: false, pitch: 1.0, stutter: false, stutterMs: 100 };
    trackFxEffects[trackIndex].reverse = val;
    sendWebSocket({ cmd: 'setReverse', track: trackIndex, value: val });
    updateTrackStepDots(trackIndex);
    const btn = document.getElementById('trkRevBtn');
    if (btn) { btn.textContent = val ? '⏪ ON' : '▶️ OFF'; btn.classList.toggle('fx-on', val); }
}
function setTrackFxPitch(trackIndex, val) {
    val = parseFloat(val);
    if (!trackFxEffects[trackIndex]) trackFxEffects[trackIndex] = { reverse: false, pitch: 1.0, stutter: false, stutterMs: 100 };
    trackFxEffects[trackIndex].pitch = val;
    sendWebSocket({ cmd: 'setPitchShift', track: trackIndex, value: val });
    updateTrackStepDots(trackIndex);
    const vEl = document.getElementById('trkFxPitchVal');
    if (vEl) vEl.textContent = val.toFixed(2);
    document.querySelectorAll('#padFxModal .pitch-preset-btn').forEach(b => {
        b.classList.toggle('active', Math.abs(parseFloat(b.textContent) - val) < 0.01);
    });
}
function setTrackFxStutterToggle(trackIndex, val) {
    if (!trackFxEffects[trackIndex]) trackFxEffects[trackIndex] = { reverse: false, pitch: 1.0, stutter: false, stutterMs: 100 };
    trackFxEffects[trackIndex].stutter = val;
    const ms = trackFxEffects[trackIndex].stutterMs || 100;
    sendWebSocket({ cmd: 'setStutter', track: trackIndex, active: val, interval: ms });
    updateTrackStepDots(trackIndex);
    const btn = document.getElementById('trkStutBtn');
    if (btn) { btn.textContent = val ? '🔁 ON' : '🔁 OFF'; btn.classList.toggle('fx-on', val); }
}
function setTrackFxStutterMs(trackIndex, ms) {
    ms = parseInt(ms);
    if (!trackFxEffects[trackIndex]) trackFxEffects[trackIndex] = { reverse: false, pitch: 1.0, stutter: false, stutterMs: 100 };
    trackFxEffects[trackIndex].stutterMs = ms;
    const active = trackFxEffects[trackIndex].stutter;
    sendWebSocket({ cmd: 'setStutter', track: trackIndex, active: active, interval: ms });
    const vEl = document.getElementById('trkFxStutVal');
    if (vEl) vEl.textContent = ms;
}

// === TRACK FX — Echo / Flanger / Compressor ===
function setTrackFxEchoActive(trackIndex, val) {
    if (!trackLiveFxState[trackIndex]) trackLiveFxState[trackIndex] = { echo: { active: false, time: 100, feedback: 40, mix: 50 }, flanger: { active: false, rate: 50, depth: 50, feedback: 30 }, compressor: { active: false, threshold: -20, ratio: 4 } };
    trackLiveFxState[trackIndex].echo.active = val;
    const e = trackLiveFxState[trackIndex].echo;
    sendWebSocket({ cmd: 'setTrackEcho', track: trackIndex, active: val, time: e.time, feedback: e.feedback, mix: e.mix });
    updateTrackStepDots(trackIndex);
    const btn = document.getElementById('trkEchoBtn');
    if (btn) { btn.textContent = val ? '🔊 ON' : '🔊 OFF'; btn.classList.toggle('fx-on', val); }
}
function setTrackFxEchoParam(trackIndex, param, val) {
    if (!trackLiveFxState[trackIndex]) return;
    trackLiveFxState[trackIndex].echo[param] = val;
    const e = trackLiveFxState[trackIndex].echo;
    sendWebSocket({ cmd: 'setTrackEcho', track: trackIndex, active: e.active, time: e.time, feedback: e.feedback, mix: e.mix });
    const el = document.getElementById(`trkEcho${param.charAt(0).toUpperCase()+param.slice(1)}Val`);
    if (el) el.textContent = val;
}
function setTrackFxFlangerActive(trackIndex, val) {
    if (!trackLiveFxState[trackIndex]) trackLiveFxState[trackIndex] = { echo: { active: false, time: 100, feedback: 40, mix: 50 }, flanger: { active: false, rate: 50, depth: 50, feedback: 30 }, compressor: { active: false, threshold: -20, ratio: 4 } };
    trackLiveFxState[trackIndex].flanger.active = val;
    const f = trackLiveFxState[trackIndex].flanger;
    sendWebSocket({ cmd: 'setTrackFlanger', track: trackIndex, active: val, rate: f.rate, depth: f.depth, feedback: f.feedback });
    updateTrackStepDots(trackIndex);
    const btn = document.getElementById('trkFlngBtn');
    if (btn) { btn.textContent = val ? '🌀 ON' : '🌀 OFF'; btn.classList.toggle('fx-on', val); }
}
function setTrackFxFlangerParam(trackIndex, param, val) {
    if (!trackLiveFxState[trackIndex]) return;
    trackLiveFxState[trackIndex].flanger[param] = val;
    const f = trackLiveFxState[trackIndex].flanger;
    sendWebSocket({ cmd: 'setTrackFlanger', track: trackIndex, active: f.active, rate: f.rate, depth: f.depth, feedback: f.feedback });
    const el = document.getElementById(`trkFlng${param.charAt(0).toUpperCase()+param.slice(1)}Val`);
    if (el) el.textContent = val;
}
function setTrackFxCompActive(trackIndex, val) {
    if (!trackLiveFxState[trackIndex]) trackLiveFxState[trackIndex] = { echo: { active: false, time: 100, feedback: 40, mix: 50 }, flanger: { active: false, rate: 50, depth: 50, feedback: 30 }, compressor: { active: false, threshold: -20, ratio: 4 } };
    trackLiveFxState[trackIndex].compressor.active = val;
    const c = trackLiveFxState[trackIndex].compressor;
    sendWebSocket({ cmd: 'setTrackCompressor', track: trackIndex, active: val, threshold: c.threshold, ratio: c.ratio });
    updateTrackStepDots(trackIndex);
    const btn = document.getElementById('trkCompBtn');
    if (btn) { btn.textContent = val ? '🗜️ ON' : '🗜️ OFF'; btn.classList.toggle('fx-on', val); }
}
function setTrackFxCompParam(trackIndex, param, val) {
    if (!trackLiveFxState[trackIndex]) return;
    trackLiveFxState[trackIndex].compressor[param] = val;
    const c = trackLiveFxState[trackIndex].compressor;
    sendWebSocket({ cmd: 'setTrackCompressor', track: trackIndex, active: c.active, threshold: c.threshold, ratio: c.ratio });
    const el = document.getElementById(`trkComp${param.charAt(0).toUpperCase()+param.slice(1)}Val`);
    if (el) el.textContent = val;
}

// ── Step filter/fx dot indicators ────────────────────────────────────────────
function updateTrackStepDots(track) {
    const filterType = trackFilterState[track] || 0;
    const fx   = trackFxState[track];
    const eff  = trackFxEffects[track];
    const live = trackLiveFxState[track];
    const hasFx = !!(
        (fx   && ((fx.distortion > 0) || (fx.bitcrush !== undefined && fx.bitcrush < 16))) ||
        (eff  && (eff.reverse || (eff.pitch !== undefined && eff.pitch !== 1.0) || eff.stutter)) ||
        (live && (live.echo.active || live.flanger.active || live.compressor.active))
    );

    document.querySelectorAll(`.seq-step[data-track="${track}"]`).forEach(stepEl => {
        // Filter dot — top-right, visible only on active steps (CSS)
        let fd = stepEl.querySelector('.step-filter-dot');
        if (filterType > 0) {
            if (!fd) { fd = document.createElement('i'); fd.className = 'step-filter-dot'; stepEl.appendChild(fd); }
            fd.dataset.ft = String(filterType);
        } else if (fd) { fd.remove(); }
        // FX dot — bottom-right, visible only on active steps (CSS)
        let xd = stepEl.querySelector('.step-fx-dot');
        if (hasFx) {
            if (!xd) { xd = document.createElement('i'); xd.className = 'step-fx-dot'; stepEl.appendChild(xd); }
        } else if (xd) { xd.remove(); }
    });
    // Highlight fxCell buttons to reflect state
    const fxCell = document.querySelector(`.seq-fx-cell[data-track="${track}"]`);
    if (fxCell) {
        const fb = fxCell.querySelector('.seq-filter-btn');
        const db = fxCell.querySelector('.seq-dist-btn');
        if (fb) fb.classList.toggle('fx-active', filterType > 0);
        if (db) db.classList.toggle('fx-active', hasFx);
    }
}
window.updateTrackStepDots = updateTrackStepDots;

/* ── Sync FX from Patchbay localStorage ── */
function syncFxFromPatchbay() {
    try {
        const raw = localStorage.getItem('r808_shared_fx');
        if (!raw) return;
        const state = JSON.parse(raw);
        const filterTypeNameToInt = {
            lowpass:1, highpass:2, bandpass:3, notch:4, allpass:5,
            peaking:6, lowshelf:7, highshelf:8, resonant:9
        };
        const filterTypes = Object.keys(filterTypeNameToInt);

        Object.entries(state).forEach(([trackStr, fxList]) => {
            const track = parseInt(trackStr, 10);
            if (isNaN(track) || track < 0 || track >= 16) return;
            if (!Array.isArray(fxList)) return;

            fxList.forEach(fx => {
                /* Filter FX */
                if (filterTypes.includes(fx.fxType)) {
                    trackFilterState[track] = filterTypeNameToInt[fx.fxType] || 0;
                }
                /* Echo */
                if (fx.fxType === 'echo' || fx.fxType === 'delay') {
                    const s = trackLiveFxState[track];
                    if (s) {
                        s.echo.active = true;
                        if (fx.params.time != null) s.echo.time = fx.params.time;
                        if (fx.params.feedback != null) s.echo.feedback = fx.params.feedback;
                        if (fx.params.mix != null) s.echo.mix = fx.params.mix;
                    }
                }
                /* Flanger */
                if (fx.fxType === 'flanger') {
                    const s = trackLiveFxState[track];
                    if (s) {
                        s.flanger.active = true;
                        if (fx.params.rate != null) s.flanger.rate = fx.params.rate;
                        if (fx.params.depth != null) s.flanger.depth = fx.params.depth;
                        if (fx.params.feedback != null) s.flanger.feedback = fx.params.feedback;
                    }
                }
                /* Compressor */
                if (fx.fxType === 'compressor') {
                    const s = trackLiveFxState[track];
                    if (s) {
                        s.compressor.active = true;
                        if (fx.params.threshold != null) s.compressor.threshold = fx.params.threshold;
                        if (fx.params.ratio != null) s.compressor.ratio = fx.params.ratio;
                    }
                }
            });
            updateTrackStepDots(track);
        });
        console.log('[SYNC] FX state loaded from patchbay localStorage');
    } catch(ex) {
        console.warn('[SYNC] Could not read patchbay FX state:', ex);
    }
}
window.syncFxFromPatchbay = syncFxFromPatchbay;

/* Save sequencer FX state to localStorage for patchbay to pick up */
function saveSeqFxToShared() {
    try {
        const filterTypeIntToName = {
            1:'lowpass', 2:'highpass', 3:'bandpass', 4:'notch', 5:'allpass',
            6:'peaking', 7:'lowshelf', 8:'highshelf', 9:'resonant'
        };
        const state = {};
        for (let t = 0; t < 16; t++) {
            const fxList = [];
            if (trackFilterState[t] > 0) {
                fxList.push({ fxType: filterTypeIntToName[trackFilterState[t]] || 'lowpass', params: { cutoff: 1000, resonance: 1 } });
            }
            const live = trackLiveFxState[t];
            if (live) {
                if (live.echo.active) fxList.push({ fxType: 'echo', params: { time: live.echo.time, feedback: live.echo.feedback, mix: live.echo.mix } });
                if (live.flanger.active) fxList.push({ fxType: 'flanger', params: { rate: live.flanger.rate, depth: live.flanger.depth, feedback: live.flanger.feedback } });
                if (live.compressor.active) fxList.push({ fxType: 'compressor', params: { threshold: live.compressor.threshold, ratio: live.compressor.ratio } });
            }
            if (fxList.length > 0) state[t] = fxList;
        }
        localStorage.setItem('r808_seq_fx', JSON.stringify(state));
    } catch(ex) {}
}
window.saveSeqFxToShared = saveSeqFxToShared;

// Track FX functions (same concept but for sequencer tracks)
function showTrackFxPopup(trackIndex) {
    closePadFxPopup();
    const backdrop = document.createElement('div');
    backdrop.id = 'padFxBackdrop';
    backdrop.className = 'loop-popup-backdrop';
    backdrop.addEventListener('click', closePadFxPopup);

    const cfx   = trackFxState[trackIndex] || {};
    const ceff  = trackFxEffects[trackIndex] || {};
    const clive = trackLiveFxState[trackIndex] || {};
    const dist   = cfx.distortion || 0;
    const dmode  = cfx.distMode || 0;
    const bits   = cfx.bitcrush || 16;
    const rev    = !!ceff.reverse;
    const pitch  = ceff.pitch !== undefined ? ceff.pitch : 1.0;
    const stut   = !!ceff.stutter;
    const stutMs = ceff.stutterMs || 100;
    const echo   = clive.echo   || { active: false, time: 100, feedback: 40, mix: 50 };
    const flng   = clive.flanger|| { active: false, rate: 50, depth: 50, feedback: 30 };
    const comp   = clive.compressor || { active: false, threshold: -20, ratio: 4 };

    const trackName = padNames[trackIndex] || `Track ${trackIndex + 1}`;
    const popup = document.createElement('div');
    popup.id = 'padFxModal';
    popup.className = 'pad-fx-modal';
    popup.innerHTML = `
        <div class="loop-popup-header">
            <span class="loop-popup-title">🎚️ TRACK FX: ${trackName}</span>
            <button class="loop-popup-close" onclick="closePadFxPopup()">&times;</button>
        </div>
        <div class="pad-fx-content">
            <div class="pad-fx-section">
                <h4>🎸 DISTORTION</h4>
                <div class="pad-fx-modes">
                    ${DISTORTION_MODES.map(m => `
                        <button class="loop-type-btn pad-fx-mode-btn ${m.id === dmode ? 'active' : ''}"
                                data-mode="${m.id}" onclick="setTrackFxDistMode(${trackIndex}, ${m.id})">
                            <span class="loop-type-icon">${m.icon}</span>
                            <span class="loop-type-name">${m.name}</span>
                        </button>`).join('')}
                </div>
                <div class="pad-fx-slider-row">
                    <label>Drive <span id="padFxDriveVal">${dist}</span>%</label>
                    <input type="range" id="padFxDrive" min="0" max="100" value="${dist}"
                           oninput="setTrackFxDrive(${trackIndex}, this.value)" class="fx-slider">
                </div>
            </div>
            <div class="pad-fx-section">
                <h4>📼 BIT CRUSH</h4>
                <div class="pad-fx-slider-row">
                    <label>Bits <span id="padFxBitsVal">${bits}</span></label>
                    <input type="range" id="padFxBits" min="4" max="16" value="${bits}"
                           oninput="setTrackFxBits(${trackIndex}, this.value)" class="fx-slider">
                </div>
            </div>
            <div class="pad-fx-section">
                <h4>⏪ REVERSE</h4>
                <button class="fx-toggle-btn ${rev ? 'fx-on' : ''}" id="trkRevBtn"
                        onclick="setTrackFxReverse(${trackIndex}, !${rev})">
                    ${rev ? '⏪ ON' : '▶️ OFF'}
                </button>
            </div>
            <div class="pad-fx-section">
                <h4>🎵 PITCH SHIFT</h4>
                <div class="pad-fx-slider-row">
                    <label>Pitch <span id="trkFxPitchVal">${pitch.toFixed(2)}</span>×</label>
                    <input type="range" id="trkFxPitch" min="25" max="200" value="${Math.round(pitch*100)}"
                           oninput="setTrackFxPitch(${trackIndex}, this.value/100)" class="fx-slider">
                </div>
                <div class="pad-fx-modes" style="grid-template-columns:repeat(4,1fr);margin-top:6px">
                    ${[0.25,0.5,0.75,1.0,1.25,1.5,2.0].map(v=>`
                        <button class="pitch-preset-btn ${Math.abs(pitch-v)<0.01?'active':''}"
                                onclick="setTrackFxPitch(${trackIndex},${v})">${v}×</button>`).join('')}
                </div>
            </div>
            <div class="pad-fx-section">
                <h4>🔁 STUTTER</h4>
                <button class="fx-toggle-btn ${stut ? 'fx-on' : ''}" id="trkStutBtn"
                        onclick="setTrackFxStutterToggle(${trackIndex}, !${stut})">
                    ${stut ? '🔁 ON' : '🔁 OFF'}
                </button>
                <div class="pad-fx-slider-row" style="margin-top:8px">
                    <label>Interval <span id="trkFxStutVal">${stutMs}</span>ms</label>
                    <input type="range" id="trkFxStutMs" min="20" max="500" value="${stutMs}"
                           oninput="setTrackFxStutterMs(${trackIndex}, this.value)" class="fx-slider">
                </div>
            </div>
            <div class="pad-fx-section">
                <h4>🔊 ECHO / DELAY</h4>
                <button class="fx-toggle-btn ${echo.active ? 'fx-on' : ''}" id="trkEchoBtn"
                        onclick="setTrackFxEchoActive(${trackIndex}, !${echo.active})">
                    ${echo.active ? '🔊 ON' : '🔊 OFF'}
                </button>
                <div class="pad-fx-slider-row" style="margin-top:8px">
                    <label>Time <span id="trkEchoTimeVal">${echo.time}</span>ms</label>
                    <input type="range" id="trkEchoTime" min="10" max="200" value="${echo.time}"
                           oninput="setTrackFxEchoParam(${trackIndex},'time',+this.value)" class="fx-slider">
                </div>
                <div class="pad-fx-slider-row">
                    <label>Feedback <span id="trkEchoFbVal">${echo.feedback}</span>%</label>
                    <input type="range" id="trkEchoFb" min="0" max="95" value="${echo.feedback}"
                           oninput="setTrackFxEchoParam(${trackIndex},'feedback',+this.value)" class="fx-slider">
                </div>
                <div class="pad-fx-slider-row">
                    <label>Mix <span id="trkEchoMixVal">${echo.mix}</span>%</label>
                    <input type="range" id="trkEchoMix" min="0" max="100" value="${echo.mix}"
                           oninput="setTrackFxEchoParam(${trackIndex},'mix',+this.value)" class="fx-slider">
                </div>
            </div>
            <div class="pad-fx-section">
                <h4>🌀 FLANGER</h4>
                <button class="fx-toggle-btn ${flng.active ? 'fx-on' : ''}" id="trkFlngBtn"
                        onclick="setTrackFxFlangerActive(${trackIndex}, !${flng.active})">
                    ${flng.active ? '🌀 ON' : '🌀 OFF'}
                </button>
                <div class="pad-fx-slider-row" style="margin-top:8px">
                    <label>Rate <span id="trkFlngRateVal">${flng.rate}</span>%</label>
                    <input type="range" id="trkFlngRate" min="1" max="100" value="${flng.rate}"
                           oninput="setTrackFxFlangerParam(${trackIndex},'rate',+this.value)" class="fx-slider">
                </div>
                <div class="pad-fx-slider-row">
                    <label>Depth <span id="trkFlngDepthVal">${flng.depth}</span>%</label>
                    <input type="range" id="trkFlngDepth" min="0" max="100" value="${flng.depth}"
                           oninput="setTrackFxFlangerParam(${trackIndex},'depth',+this.value)" class="fx-slider">
                </div>
                <div class="pad-fx-slider-row">
                    <label>Feedback <span id="trkFlngFbVal">${flng.feedback}</span>%</label>
                    <input type="range" id="trkFlngFb" min="-90" max="90" value="${flng.feedback}"
                           oninput="setTrackFxFlangerParam(${trackIndex},'feedback',+this.value)" class="fx-slider">
                </div>
            </div>
            <div class="pad-fx-section">
                <h4>🗜️ COMPRESSOR</h4>
                <button class="fx-toggle-btn ${comp.active ? 'fx-on' : ''}" id="trkCompBtn"
                        onclick="setTrackFxCompActive(${trackIndex}, !${comp.active})">
                    ${comp.active ? '🗜️ ON' : '🗜️ OFF'}
                </button>
                <div class="pad-fx-slider-row" style="margin-top:8px">
                    <label>Threshold <span id="trkCompThVal">${comp.threshold}</span>dB</label>
                    <input type="range" id="trkCompTh" min="-60" max="0" value="${comp.threshold}"
                           oninput="setTrackFxCompParam(${trackIndex},'threshold',+this.value)" class="fx-slider">
                </div>
                <div class="pad-fx-slider-row">
                    <label>Ratio <span id="trkCompRatioVal">${comp.ratio}</span>:1</label>
                    <input type="range" id="trkCompRatio" min="1" max="20" value="${comp.ratio}"
                           oninput="setTrackFxCompParam(${trackIndex},'ratio',+this.value)" class="fx-slider">
                </div>
            </div>
            <button class="pad-fx-clear-btn" onclick="clearTrackFxAll(${trackIndex})">🚫 CLEAR ALL FX</button>
        </div>
    `;
    document.body.appendChild(backdrop);
    document.body.appendChild(popup);
    requestAnimationFrame(() => { backdrop.classList.add('visible'); popup.classList.add('visible'); });
}

function setTrackFxDistMode(trackIndex, mode) {
    if (!trackFxState[trackIndex]) trackFxState[trackIndex] = {};
    trackFxState[trackIndex].distMode = mode;
    const drive = trackFxState[trackIndex].distortion || 0;
    sendWebSocket({ cmd: 'setTrackDistortion', track: trackIndex, amount: drive, mode: mode });
    document.querySelectorAll('#padFxModal .pad-fx-mode-btn').forEach(btn => {
        btn.classList.toggle('active', parseInt(btn.dataset.mode) === mode);
    });
    // Sync to pad if enabled
    if (padSeqSyncEnabled && trackIndex < 16) {
        if (!padFxState[trackIndex]) padFxState[trackIndex] = {};
        padFxState[trackIndex].distMode = mode;
        padFxState[trackIndex].distortion = drive;
        sendWebSocket({ cmd: 'setPadDistortion', pad: trackIndex, amount: drive, mode: mode });
        updatePadFxIndicator(trackIndex);
    }
}

function setTrackFxDrive(trackIndex, value) {
    const val = parseInt(value);
    if (!trackFxState[trackIndex]) trackFxState[trackIndex] = {};
    trackFxState[trackIndex].distortion = val;
    const mode = trackFxState[trackIndex].distMode || 0;
    document.getElementById('padFxDriveVal').textContent = val;
    sendWebSocket({ cmd: 'setTrackDistortion', track: trackIndex, amount: val, mode: mode });
    updateTrackStepDots(trackIndex);
    // Sync to pad if enabled
    if (padSeqSyncEnabled && trackIndex < 16) {
        if (!padFxState[trackIndex]) padFxState[trackIndex] = {};
        padFxState[trackIndex].distortion = val;
        padFxState[trackIndex].distMode = mode;
        sendWebSocket({ cmd: 'setPadDistortion', pad: trackIndex, amount: val, mode: mode });
        updatePadFxIndicator(trackIndex);
    }
}

function setTrackFxBits(trackIndex, value) {
    const val = parseInt(value);
    if (!trackFxState[trackIndex]) trackFxState[trackIndex] = {};
    trackFxState[trackIndex].bitcrush = val;
    document.getElementById('padFxBitsVal').textContent = val;
    sendWebSocket({ cmd: 'setTrackBitCrush', track: trackIndex, value: val });
    updateTrackStepDots(trackIndex);
    // Sync to pad if enabled
    if (padSeqSyncEnabled && trackIndex < 16) {
        if (!padFxState[trackIndex]) padFxState[trackIndex] = {};
        padFxState[trackIndex].bitcrush = val;
        sendWebSocket({ cmd: 'setPadBitCrush', pad: trackIndex, value: val });
        updatePadFxIndicator(trackIndex);
    }
}

function clearTrackFxAll(trackIndex) {
    trackFxState[trackIndex] = null;
    if (trackFxEffects[trackIndex]) trackFxEffects[trackIndex] = { reverse: false, pitch: 1.0, stutter: false, stutterMs: 100 };
    if (trackLiveFxState[trackIndex]) trackLiveFxState[trackIndex] = {
        echo:       { active: false, time: 100, feedback: 40, mix: 50 },
        flanger:    { active: false, rate: 50, depth: 50, feedback: 30 },
        compressor: { active: false, threshold: -20, ratio: 4 }
    };
    sendWebSocket({ cmd: 'clearTrackFX', track: trackIndex });
    sendWebSocket({ cmd: 'setReverse', track: trackIndex, value: false });
    sendWebSocket({ cmd: 'setPitchShift', track: trackIndex, value: 1.0 });
    sendWebSocket({ cmd: 'setStutter', track: trackIndex, active: false, interval: 100 });
    sendWebSocket({ cmd: 'setTrackEcho', track: trackIndex, active: false, time: 100, feedback: 40, mix: 50 });
    sendWebSocket({ cmd: 'setTrackFlanger', track: trackIndex, active: false, rate: 50, depth: 0, feedback: 0 });
    sendWebSocket({ cmd: 'setTrackCompressor', track: trackIndex, active: false, threshold: -20, ratio: 4 });
    updateTrackStepDots(trackIndex);
    closePadFxPopup();
    if (padSeqSyncEnabled && trackIndex < 16) {
        padFxState[trackIndex] = null;
        sendWebSocket({ cmd: 'clearPadFX', pad: trackIndex });
        updatePadFxIndicator(trackIndex);
    }
}

// Expose FX functions
window.showPadFxPopup = showPadFxPopup;
window.showTrackFxPopup = showTrackFxPopup;
window.setPadFxDistMode = setPadFxDistMode;
window.setPadFxDrive = setPadFxDrive;
window.setPadFxBits = setPadFxBits;
window.setPadFxReverse = setPadFxReverse;
window.setPadFxPitch = setPadFxPitch;
window.setPadFxStutterToggle = setPadFxStutterToggle;
window.setPadFxStutterMs = setPadFxStutterMs;
window.clearPadFxAll = clearPadFxAll;
window.setTrackFxDistMode = setTrackFxDistMode;
window.setTrackFxDrive = setTrackFxDrive;
window.setTrackFxBits = setTrackFxBits;
window.setTrackFxReverse = setTrackFxReverse;
window.setTrackFxPitch = setTrackFxPitch;
window.setTrackFxStutterToggle = setTrackFxStutterToggle;
window.setTrackFxStutterMs = setTrackFxStutterMs;
window.setTrackFxEchoActive = setTrackFxEchoActive;
window.setTrackFxEchoParam = setTrackFxEchoParam;
window.setTrackFxFlangerActive = setTrackFxFlangerActive;
window.setTrackFxFlangerParam = setTrackFxFlangerParam;
window.setTrackFxCompActive = setTrackFxCompActive;
window.setTrackFxCompParam = setTrackFxCompParam;
window.clearTrackFxAll = clearTrackFxAll;

// Update pad filter indicator visual
function updatePadFilterIndicator(padIndex) {
    const indicator = document.querySelector(`.pad-filter-indicator[data-pad="${padIndex}"]`);
    if (indicator) {
        const filterType = padFilterState[padIndex];
        if (filterType > 0) {
            const filter = FILTER_TYPES[filterType];
            const newHtml = `<span class="filter-icon">${filter.icon}</span><span class="filter-name">${filter.name}</span>`;
            if (indicator.style.display !== 'flex') indicator.style.display = 'flex';
            if (indicator.innerHTML !== newHtml) indicator.innerHTML = newHtml;
        } else {
            if (indicator.style.display !== 'none') indicator.style.display = 'none';
        }
    }
    // Also update XTRA pad filter button indicator
    const xtraPad = document.querySelector(`.pad-xtra[data-pad-index="${padIndex}"]`);
    if (xtraPad) {
        const filterBtn = xtraPad.querySelector('.xtra-filter');
        if (filterBtn) {
            filterBtn.classList.toggle('active', padFilterState[padIndex] > 0);
        }
    }
}

// Actualizar botones de selección de samples según conteo
function updateSampleButtons() {
    let buttonsShown = 0;
    document.querySelectorAll('.pad-select-btn').forEach((btn, index) => {
        const family = padNames[index];
        const count = sampleCounts[family] || 0;
        
        if (count > 1) {
            btn.style.display = 'flex';
            btn.innerHTML = `📂<span class="sample-count-badge">${count}</span>`;
            btn.title = `${count} ${family} samples available - Click to change`;
            buttonsShown++;
        } else {
            btn.style.display = 'none';
        }
    });
}

function handleSampleCountsMessage(payload) {
    const sanitizedCounts = {};
    let totalFiles = 0;
    padNames.forEach((family) => {
        const count = typeof payload[family] === 'number' ? payload[family] : 0;
        sanitizedCounts[family] = count;
        totalFiles += count;
    });
    sampleCounts = sanitizedCounts;
    updateSampleButtons();
    updateInstrumentCounts(totalFiles);
    scheduleSampleBrowserRender();

    // Limpiar timer de reintento
    if (sampleRetryTimer) {
        clearTimeout(sampleRetryTimer);
        sampleRetryTimer = null;
    }
}

function updateInstrumentCounts(totalFiles) {
    // Todas las 16 familias que envía el backend
    const allFamilies = ['BD', 'SD', 'CH', 'OH', 'CP', 'CB', 'RS', 'CL', 'MA', 'CY', 'HT', 'LT', 'MC', 'MT', 'HC', 'LC'];
    
    allFamilies.forEach((family) => {
        const label = document.getElementById(`instCount-${family}`);
        if (label) {
            const count = sampleCounts[family] || 0;
            label.textContent = count > 0 ? `${count} library files` : 'No files found';
        }
    });
    
    const totalsEl = document.getElementById('libraryTotals');
    if (totalsEl) {
        const files = typeof totalFiles === 'number' ? totalFiles : Object.values(sampleCounts).reduce((sum, val) => sum + (val || 0), 0);
        // Contar familias activas (las 8 de padNames)
        const activeFamilies = padNames.length;
        // Contar total de familias con samples
        const totalFamilies = allFamilies.filter(f => (sampleCounts[f] || 0) > 0).length;
        totalsEl.textContent = `${files} files total (${activeFamilies} active / ${totalFamilies} families)`;
    }
}

function refreshPadSampleInfo(padIndex) {
    const infoEl = document.getElementById(`sampleInfo-${padIndex}`);
    const meta = padSampleMetadata[padIndex];
    if (!infoEl) return;
    
    const fileSpan = infoEl.querySelector('.sample-file');
    const qualitySpan = infoEl.querySelector('.sample-quality');
    
    if (!meta) {
        if (fileSpan) fileSpan.textContent = '—';
        if (qualitySpan) qualitySpan.textContent = '';
        infoEl.title = 'No sample loaded';
    } else {
        // Extract filename without extension for cleaner display
        const cleanName = meta.filename.replace(/\.(wav|raw)$/i, '');
        if (fileSpan) fileSpan.textContent = cleanName;
        
        // Format: "44.1k•16b•M" or "22k•8b•S"
        const quality = meta.quality || '44.1kHz•16-bit mono';
        const shortQuality = quality
            .replace(/kHz/g, 'k')
            .replace(/-bit/g, 'b')
            .replace(/mono/g, 'M')
            .replace(/stereo/g, 'S')
            .replace(/ /g, '•');
        
        if (qualitySpan) qualitySpan.textContent = shortQuality;
        infoEl.title = `${meta.filename} - ${meta.sizeKB} KB - ${meta.format}`;
    }
    updateInstrumentMetadata(padIndex);
    scheduleSampleBrowserRender();
}

function applySampleMetadataFromState(sampleList) {
    if (!Array.isArray(sampleList)) return;
    let anyChanged = false;
    sampleList.forEach(sample => {
        const padIndex = sample.pad;
        if (typeof padIndex !== 'number' || padIndex < 0 || padIndex >= padNames.length) {
            return;
        }
        const oldMeta = padSampleMetadata[padIndex];
        if (sample.loaded && sample.name) {
            // Skip if name hasn't changed
            if (oldMeta && oldMeta.filename === sample.name) return;
            const sizeBytes = typeof sample.size === 'number' ? sample.size : 0;
            padSampleMetadata[padIndex] = {
                filename: sample.name,
                sizeKB: (sizeBytes / 1024).toFixed(1),
                format: sample.format ? sample.format.toUpperCase() : inferFormatFromName(sample.name),
                quality: sample.quality || DEFAULT_SAMPLE_QUALITY
            };
        } else {
            // Skip if already null
            if (!oldMeta) return;
            padSampleMetadata[padIndex] = null;
        }
        anyChanged = true;
        refreshPadSampleInfo(padIndex);
    });
    if (anyChanged) scheduleSampleBrowserRender();
}

function inferFormatFromName(name) {
    if (!name || typeof name !== 'string') return 'RAW/WAV';
    const lower = name.toLowerCase();
    if (lower.endsWith('.wav')) return 'WAV';
    if (lower.endsWith('.raw')) return 'RAW';
    return 'RAW/WAV';
}

function updateInstrumentMetadata(padIndex) {
    const family = padNames[padIndex];
    if (!family) return;
    const meta = padSampleMetadata[padIndex];
    const currentEl = document.getElementById(`instCurrent-${family}`);
    const qualityEl = document.getElementById(`instQuality-${family}`);
    if (!currentEl || !qualityEl) return;
    if (!meta) {
        currentEl.textContent = 'Current: —';
        qualityEl.textContent = 'Format: —';
        return;
    }
    currentEl.textContent = `Current: ${meta.filename} (${meta.sizeKB} KB)`;
    qualityEl.textContent = `Format: ${meta.format} • ${meta.quality}`;
}

function getLoadedSampleLookup() {
    const lookup = {};
    padNames.forEach((family, index) => {
        const meta = padSampleMetadata[index];
        if (meta && meta.filename) {
            lookup[family] = meta.filename;
        }
    });
    return lookup;
}

function updateDeviceStats(data) {
    if (data.samplesLoaded !== undefined) {
        const el = document.getElementById('samplesCount');
        if (el) el.textContent = `${data.samplesLoaded}/${padNames.length} pads`;
    }
    if (data.memoryUsed !== undefined) {
        const el = document.getElementById('memoryUsed');
        if (el) el.textContent = formatBytes(data.memoryUsed);
    }
    if (data.psramFree !== undefined) {
        const el = document.getElementById('psramFree');
        if (el) el.textContent = `PSRAM free ${formatBytes(data.psramFree)}`;
    }
    const formatEl = document.getElementById('sampleFormat');
    if (formatEl) formatEl.textContent = '44.1kHz Mono 16-bit';
}

function formatBytes(bytes) {
    if (bytes === undefined || bytes === null) return '—';
    if (bytes === 0) return '0 B';
    const units = ['B', 'KB', 'MB', 'GB'];
    let value = bytes;
    let unitIndex = 0;
    while (value >= 1024 && unitIndex < units.length - 1) {
        value /= 1024;
        unitIndex++;
    }
    const decimals = unitIndex === 0 ? 0 : 1;
    return `${value.toFixed(decimals)} ${units[unitIndex]}`;
}

function triggerPad(padIndex) {
    const now = performance.now();
    if (padIndex >= 0 && padIndex < lastPadTriggerMs.length) {
        if (now - lastPadTriggerMs[padIndex] < PAD_TEST_MIN_TRIGGER_MS) {
            return;
        }
        lastPadTriggerMs[padIndex] = now;
    }

    console.log('[PAD] triggerPad()', padIndex, 'ws=', ws ? ws.readyState : 'null');

    // Enviar al ESP32 (Protocolo Binario para baja latencia)
    if (ws && ws.readyState === WebSocket.OPEN) {
        const data = new Uint8Array(3);
        data[0] = 0x90; // Comando Trigger (0x90)
        data[1] = padIndex;
        data[2] = 127;  // Velocity
        ws.send(data);
    } else {
        // Fallback por HTTP si WS no está conectado
        fetch('/api/trigger', {
            method: 'POST',
            headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
            body: `pad=${encodeURIComponent(padIndex)}`
        }).catch((err) => console.error('[PAD] /api/trigger failed', err));
    }
}



function flashPad(padIndex) {
    const pad = document.querySelector(`[data-pad="${padIndex}"]`);
    if (pad) {
        pad.classList.add('triggered');
        setTimeout(() => pad.classList.remove('triggered'), 600);
    }
}

function updatePadLoopVisual(padIndex) {
    let pad = document.querySelector(`[data-pad="${padIndex}"]`);
    // Also check XTRA pads
    if (!pad) {
        pad = document.querySelector(`.pad-xtra[data-pad-index="${padIndex}"]`);
    }
    if (!pad) return;
    
    const state = padLoopState[padIndex];
    if (state && state.active) {
        pad.classList.add('looping');
        if (state.paused) {
            pad.classList.add('loop-paused');
        } else {
            pad.classList.remove('loop-paused');
        }
    } else {
        pad.classList.remove('looping', 'loop-paused');
    }
    
    // Actualizar el botón de loop
    updateLoopButtonState(padIndex);

    updateTrackLoopVisual(padIndex);
}

function setTrackMuted(track, isMuted, sendCommand) {
    trackMutedState[track] = !!isMuted;

    const labelEl = document.querySelector(`.track-label[data-track="${track}"]`);
    if (labelEl) {
        labelEl.classList.toggle('muted', isMuted);
    }
    const muteBtn = document.querySelector(`.mute-btn[data-track="${track}"]`);
    if (muteBtn) {
        muteBtn.classList.toggle('muted', isMuted);
    }
    document.querySelectorAll(`.seq-step[data-track="${track}"]`).forEach(step => {
        step.classList.toggle('track-muted', isMuted);
    });
    
    // Update volume muted state in volumes section
    if (window.updateVolumeMutedState) {
        window.updateVolumeMutedState(track, isMuted);
    }

    if (sendCommand) {
        sendWebSocket({
            cmd: 'mute',
            track: track,
            value: isMuted
        });
        
        // Show toast notification
        const trackName = padNames[track] || `Track ${track + 1}`;
        if (window.showToast && window.TOAST_TYPES) {
            window.showToast(`${isMuted ? '🔇' : '🔊'} ${trackName} ${isMuted ? 'Muted' : 'Unmuted'}`, 
                           window.TOAST_TYPES.WARNING, 1500);
        }
    }

    // Sync Mute All button visual
    updateMuteAllButton();
}

function updateMuteAllButton() {
    const btn = document.getElementById('muteAllBtn');
    if (!btn) return;
    const allMuted = trackMutedState.every(m => m);
    const iconEl = btn.querySelector('.seq-btn-icon');
    const labelEl = btn.querySelector('.seq-btn-label');
    btn.classList.toggle('all-muted', allMuted);
    if (iconEl) iconEl.textContent = allMuted ? '🔊' : '🔇';
    if (labelEl) labelEl.textContent = allMuted ? 'UNMUTE' : 'MUTE ALL';
}

function toggleAllMuted() {
    // If any track is unmuted → mute all; if all muted → unmute all
    const anyUnmuted = trackMutedState.some(m => !m);
    const newState = anyUnmuted;
    for (let t = 0; t < 16; t++) {
        setTrackMuted(t, newState, true);
    }
    // Update button visual
    const btn = document.getElementById('muteAllBtn');
    if (btn) {
        const iconEl = btn.querySelector('.seq-btn-icon');
        const labelEl = btn.querySelector('.seq-btn-label');
        btn.classList.toggle('all-muted', newState);
        if (iconEl) iconEl.textContent = newState ? '🔊' : '🔇';
        if (labelEl) labelEl.textContent = newState ? 'UNMUTE' : 'MUTE ALL';
    }
    if (window.showToast && window.TOAST_TYPES) {
        window.showToast(newState ? '🔇 All Tracks Muted' : '🔊 All Tracks Unmuted',
                       window.TOAST_TYPES.WARNING, 1500);
    }
}

function setSoloTrack(track) {
    if (trackSoloState === track) {
        // Desactivar solo: restaurar estado de mutes previo
        trackSoloState = -1;
        if (preSoloMuteState) {
            for (let t = 0; t < 16; t++) {
                setTrackMuted(t, preSoloMuteState[t], true);
            }
            preSoloMuteState = null;
        }
        if (window.showToast && window.TOAST_TYPES) {
            window.showToast('🔊 Solo OFF', window.TOAST_TYPES.INFO, 1500);
        }
    } else {
        // Guardar estado actual y activar solo
        preSoloMuteState = [...trackMutedState];
        trackSoloState = track;
        for (let t = 0; t < 16; t++) {
            setTrackMuted(t, t !== track, true);
        }
        const trackName = padNames[track] || `Track ${track + 1}`;
        if (window.showToast && window.TOAST_TYPES) {
            window.showToast(`🎯 Solo: ${trackName}`, window.TOAST_TYPES.SUCCESS, 1500);
        }
    }
    // Actualizar visual de botones solo
    document.querySelectorAll('.solo-btn').forEach(btn => {
        const t = parseInt(btn.dataset.track);
        btn.classList.toggle('active', t === trackSoloState);
    });
}

function updateTrackLoopVisual(trackIndex) {
    const label = document.querySelector(`.track-label[data-track="${trackIndex}"]`);
    const steps = document.querySelectorAll(`.seq-step[data-track="${trackIndex}"]`);
    const state = padLoopState[trackIndex];
    if (!label) return;

    if (state && state.active) {
        label.classList.add('looping');
        steps.forEach(step => step.classList.add('looping'));
        if (state.paused) {
            label.classList.add('loop-paused');
            steps.forEach(step => step.classList.add('loop-paused'));
        } else {
            label.classList.remove('loop-paused');
            steps.forEach(step => step.classList.remove('loop-paused'));
        }
    } else {
        label.classList.remove('looping', 'loop-paused');
        steps.forEach(step => step.classList.remove('looping', 'loop-paused'));
    }
}

// Create Sequencer
function createSequencer() {
    const grid = document.getElementById('sequencerGrid');
    const indicator = document.getElementById('stepIndicator');
    const gridWrapper = document.getElementById('sequencerContainer');
    const trackNames = ['BD', 'SD', 'CH', 'OH', 'CY', 'CP', 'RS', 'CB', 'LT', 'MT', 'HT', 'MA', 'CL', 'HC', 'MC', 'LC'];
    const trackColors = [
        '#ff0000', '#ffa500', '#ffff00', '#00ffff',
        '#e6194b', '#ff00ff', '#00ff00', '#f58231',
        '#911eb4', '#46f0f0', '#f032e6', '#bcf60c',
        '#38ceff', '#fabebe', '#008080', '#484dff'
    ];

    stepDots = [];
    stepColumns = Array.from({ length: 16 }, () => []);
    lastCurrentStep = null;
    
    // 16 tracks x (16 steps + FX column)
    for (let track = 0; track < 16; track++) {
        // Track label con botón volumen
        const label = document.createElement('div');
        label.className = 'track-label';
        label.dataset.track = track;
        
        const volumeBtn = document.createElement('button');
        volumeBtn.className = 'volume-btn';
        volumeBtn.setAttribute('aria-label', 'Volume');
        volumeBtn.title = 'Volume';
        volumeBtn.textContent = 'V';
        volumeBtn.dataset.track = track;
        volumeBtn.style.borderColor = trackColors[track];
        volumeBtn.addEventListener('click', (e) => {
            e.stopPropagation();
            showVolumeMenu(track, e.target);
        });

        // Mute button
        const muteBtn = document.createElement('button');
        muteBtn.className = 'mute-btn';
        muteBtn.textContent = 'M';
        muteBtn.title = 'Mute track';
        muteBtn.dataset.track = track;
        muteBtn.addEventListener('click', (e) => {
            e.stopPropagation();
            setTrackMuted(track, !trackMutedState[track], true);
        });

        // Solo button
        const soloBtn = document.createElement('button');
        soloBtn.className = 'solo-btn';
        soloBtn.textContent = 'S';
        soloBtn.title = 'Solo track';
        soloBtn.dataset.track = track;
        soloBtn.addEventListener('click', (e) => {
            e.stopPropagation();
            setSoloTrack(track);
        });
        
        const name = document.createElement('span');
        name.textContent = trackNames[track];
        name.style.color = trackColors[track];

        const loopIndicator = document.createElement('span');
        loopIndicator.className = 'loop-indicator';
        loopIndicator.textContent = 'LOOP';
        
        label.appendChild(name);          // grid row1 col1
        label.appendChild(volumeBtn);       // grid row1 col2
        label.appendChild(muteBtn);         // grid row2 col1
        label.appendChild(soloBtn);         // grid row2 col2
        label.appendChild(loopIndicator);   // absolute overlay
        label.style.borderColor = trackColors[track];
        
        // Set initial background with color and alpha based on volume
        updateTrackLabelBackground(label, track, trackVolumes[track]);
        
        // Hacer click en label selecciona el track para filtros
        label.addEventListener('click', (e) => {
            if (window.selectTrack) {
                window.selectTrack(track);
            }
        });
        
        grid.appendChild(label);
        
        // 16 steps
        for (let step = 0; step < 16; step++) {
            const stepEl = document.createElement('div');
            stepEl.className = 'seq-step';
            stepEl.dataset.track = track;
            stepEl.dataset.step = step;
            stepEl.dataset.notelen = '1';  // default: full note
            if (step % 4 === 0) stepEl.classList.add('beat-step');
            else if (step % 2 === 0) stepEl.classList.add('half-step');
            
            // Inner elements for note-length visualization
            const nlBar = document.createElement('div');
            nlBar.className = 'step-notelen-bar';
            stepEl.appendChild(nlBar);
            
            const nlLabel = document.createElement('div');
            nlLabel.className = 'step-notelen-label';
            stepEl.appendChild(nlLabel);
            
            // Right-click / long-press: show note-length menu
            stepEl.addEventListener('contextmenu', (e) => {
                e.preventDefault();
                if (stepEl.classList.contains('active')) {
                    showNoteLenMenu(e, track, step, stepEl);
                }
            });
            
            // Long-press support (touch)
            let _nlTimer = null;
            stepEl.addEventListener('touchstart', (e) => {
                _nlTimer = setTimeout(() => {
                    _nlTimer = null;
                    if (stepEl.classList.contains('active')) {
                        showNoteLenMenu(e.touches[0], track, step, stepEl);
                    }
                }, 500);
            }, { passive: true });
            stepEl.addEventListener('touchend', () => { if (_nlTimer) clearTimeout(_nlTimer); }, { passive: true });
            stepEl.addEventListener('touchmove', () => { if (_nlTimer) clearTimeout(_nlTimer); }, { passive: true });
            
            stepEl.addEventListener('click', () => {
                toggleStep(track, step, stepEl);
                // Seleccionar celda para velocity editor
                if (window.selectCell) {
                    window.selectCell(track, step);
                }
            });

            stepColumns[step].push(stepEl);
            
            grid.appendChild(stepEl);
        }
        
        // FX column (after 16 steps)
        const fxCell = document.createElement('div');
        fxCell.className = 'seq-fx-cell';
        fxCell.dataset.track = track;
        fxCell.style.borderColor = trackColors[track];
        
        const filterBtn = document.createElement('button');
        filterBtn.className = 'seq-fx-btn seq-filter-btn';
        filterBtn.title = 'Filtro (F1-F10)';
        filterBtn.textContent = 'F';
        filterBtn.addEventListener('click', (e) => {
            e.stopPropagation();
            if (window.keyboard_controls_module) {
                window.keyboard_controls_module.selectedTrack = track;
            }
            if (window.showTrackFilterPanel) {
                window.showTrackFilterPanel(track);
            }
        });
        
        const trackFxBtn = document.createElement('button');
        trackFxBtn.className = 'seq-fx-btn seq-dist-btn';
        trackFxBtn.title = 'Distortion & BitCrush';
        trackFxBtn.textContent = 'FX';
        trackFxBtn.addEventListener('click', (e) => {
            e.stopPropagation();
            showTrackFxPopup(track);
        });
        
        fxCell.appendChild(filterBtn);
        fxCell.appendChild(trackFxBtn);

        // Render button – renders track to WAV inline
        const renderBtn = document.createElement('button');
        renderBtn.className = 'seq-fx-btn seq-render-btn seq-render-wide';
        renderBtn.title = 'Render track to WAV';
        renderBtn.textContent = 'R';
        renderBtn.dataset.track = track;
        renderBtn.addEventListener('click', (e) => {
            e.stopPropagation();
            if (window.renderSingleTrackInline) {
                window.renderSingleTrackInline(track);
            }
        });
        fxCell.appendChild(renderBtn);

        grid.appendChild(fxCell);
    }
    
    // Step indicator dots
    for (let i = 0; i < 16; i++) {
        const dot = document.createElement('div');
        dot.className = 'step-dot';
        dot.dataset.step = i;
        indicator.appendChild(dot);
        stepDots.push(dot);
    }

    if (gridWrapper) {
        let playheadLine = gridWrapper.querySelector('.step-playhead-line');
        if (!playheadLine) {
            playheadLine = document.createElement('div');
            playheadLine.className = 'step-playhead-line';
            gridWrapper.appendChild(playheadLine);
        }

        if (!gridWrapper.dataset.playheadBound) {
            const syncPlayhead = () => updateSequencerPlayhead(currentStep);
            gridWrapper.addEventListener('scroll', syncPlayhead, { passive: true });
            window.addEventListener('resize', syncPlayhead);
            gridWrapper.dataset.playheadBound = '1';
        }

        requestAnimationFrame(() => updateSequencerPlayhead(currentStep));
    }
}

function toggleStep(track, step, element) {
    const isActive = element.classList.toggle('active');
    
    // Update circular data
    if (circularSequencerData[track]) {
        circularSequencerData[track][step] = isActive;
    }
    if (renderCircularSequencer) renderCircularSequencer._dirty = true;
    
    const noteLen = parseInt(element.dataset.notelen || '1', 10);
    
    sendWebSocket({
        cmd: 'setStep',
        track: track,
        step: step,
        active: isActive,
        noteLen: noteLen
    });
}

// ====== NOTE LENGTH MENU ======
let _activeLenMenu = null;

function _noteLenLabel(div) {
    const labels = { 1: '', 2: '½', 4: '¼', 8: '⅛', 16: '¹⁄₁₆', 32: '¹⁄₃₂', 64: '¹⁄₆₄' };
    const el = div.querySelector('.step-notelen-label');
    if (el) el.textContent = labels[parseInt(div.dataset.notelen || '1', 10)] || '';
}

function showNoteLenMenu(e, track, step, stepEl) {
    closeNoteLenMenu();
    
    const menu = document.createElement('div');
    menu.className = 'notelen-menu';
    
    const opts = [
        { div: 1, icon: '♩', label: '1/1' },
        { div: 2, icon: '♪', label: '1/2' },
        { div: 4, icon: '♬', label: '1/4' },
        { div: 8, icon: '𝅘𝅥𝅮', label: '1/8' },
        { div: 16, icon: '𝅘𝅥𝅯', label: '1/16' },
        { div: 32, icon: '𝅘𝅥𝅰', label: '1/32' },
        { div: 64, icon: '𝅘𝅥𝅱', label: '1/64' }
    ];
    
    const curDiv = parseInt(stepEl.dataset.notelen || '1', 10);
    
    opts.forEach(opt => {
        const btn = document.createElement('button');
        btn.className = 'notelen-btn' + (opt.div === curDiv ? ' active' : '');
        btn.innerHTML = `<span class="nl-icon">${opt.icon}</span><span class="nl-label">${opt.label}</span>`;
        btn.addEventListener('click', (ev) => {
            ev.stopPropagation();
            setStepNoteLen(track, step, stepEl, opt.div);
            closeNoteLenMenu();
        });
        menu.appendChild(btn);
    });
    
    // Position near click
    const x = e.clientX || (e.pageX - window.scrollX) || 0;
    const y = e.clientY || (e.pageY - window.scrollY) || 0;
    menu.style.left = Math.min(x, window.innerWidth - 220) + 'px';
    menu.style.top = Math.max(y - 10, 4) + 'px';
    
    document.body.appendChild(menu);
    _activeLenMenu = menu;
    
    // Close on outside click
    setTimeout(() => {
        document.addEventListener('click', closeNoteLenMenu, { once: true });
        document.addEventListener('touchstart', closeNoteLenMenu, { once: true, passive: true });
    }, 10);
}

function closeNoteLenMenu() {
    if (_activeLenMenu) {
        _activeLenMenu.remove();
        _activeLenMenu = null;
    }
}

function setStepNoteLen(track, step, stepEl, div) {
    stepEl.dataset.notelen = String(div);
    // Update label
    _noteLenLabel(stepEl);
    
    sendWebSocket({
        cmd: 'setStep',
        track: track,
        step: step,
        active: stepEl.classList.contains('active'),
        noteLen: div
    });
    
    // Show visual feedback notification
    const names = { 1: 'Nota entera (1/1)', 2: 'Media nota (1/2)', 4: 'Cuarto (1/4)', 8: 'Octavo (1/8)', 16: 'Semicorchea (1/16)', 32: 'Fusa (1/32)', 64: 'Semifusa (1/64)' };
    showNotification(`Track ${track + 1} Step ${step + 1}: ${names[div] || div}`);
}

// Throttle step updates via rAF to avoid layout thrashing
let _pendingStep = null;
let _stepRafScheduled = false;

function _flushStepUpdate() {
    _stepRafScheduled = false;
    if (_pendingStep === null) return;
    _applyStepUpdate(_pendingStep);
    _pendingStep = null;
}

function updateCurrentStep(step) {
    _pendingStep = step;
    if (!_stepRafScheduled) {
        _stepRafScheduled = true;
        requestAnimationFrame(_flushStepUpdate);
    }
}

function _applyStepUpdate(step) {
    if (!stepDots.length) {
        stepDots = Array.from(document.querySelectorAll('.step-dot'));
    }
    if (!stepColumns.length || !stepColumns[0] || stepColumns[0].length === 0) {
        stepColumns = Array.from({ length: 16 }, () => []);
        document.querySelectorAll('.seq-step').forEach(el => {
            const elStep = parseInt(el.dataset.step, 10);
            if (!Number.isNaN(elStep) && elStep >= 0 && elStep < stepColumns.length) {
                stepColumns[elStep].push(el);
            }
        });
    }

    currentStep = step;

    if (step === lastCurrentStep) return;

    if (lastCurrentStep !== null) {
        const prevDot = stepDots[lastCurrentStep];
        if (prevDot) prevDot.classList.remove('current');
        const prevColumn = stepColumns[lastCurrentStep] || [];
        prevColumn.forEach(el => el.classList.remove('current'));
    }

    const nextDot = stepDots[step];
    if (nextDot) nextDot.classList.add('current');
    const nextColumn = stepColumns[step] || [];
    nextColumn.forEach(el => el.classList.add('current'));
    updateSequencerPlayhead(step);

    lastCurrentStep = step;

    // === SYNC LEDS: flash live pads in rhythm with sequencer ===
    if (syncLedsEnabled && isPlaying) {
        for (let track = 0; track < 16; track++) {
            if (circularSequencerData[track] && circularSequencerData[track][step]) {
                const pad = document.querySelector(`.pad[data-pad="${track}"]`);
                if (pad) {
                    pad.classList.add('sync-flash');
                    setTimeout(() => pad.classList.remove('sync-flash'), 120);
                }
            }
        }
    }
}

function updateSequencerPlayhead(step) {
    const gridWrapper = document.getElementById('sequencerContainer');
    if (!gridWrapper) return;

    const playheadLine = gridWrapper.querySelector('.step-playhead-line');
    if (!playheadLine) return;

    if (typeof step !== 'number' || step < 0 || step > 15) {
        playheadLine.classList.remove('visible');
        return;
    }

    const stepEl = document.querySelector(`.seq-step[data-track="0"][data-step="${step}"]`);
    if (!stepEl) {
        playheadLine.classList.remove('visible');
        return;
    }

    const wrapperRect = gridWrapper.getBoundingClientRect();
    const stepRect = stepEl.getBoundingClientRect();
    const x = (stepRect.left - wrapperRect.left) + gridWrapper.scrollLeft + (stepRect.width / 2);

    playheadLine.style.transform = `translateX(${Math.round(x)}px)`;
    playheadLine.classList.add('visible');
}

// Toggle between grid and circular view
function toggleSequencerView() {
    const gridContainer = document.getElementById('sequencerContainer');
    const circularContainer = document.getElementById('sequencerCircularContainer');
    const viewModeBtn = document.getElementById('viewModeBtn');
    const btnLabel = viewModeBtn.querySelector('.seq-btn-label');
    const btnIcon = viewModeBtn.querySelector('.seq-btn-icon');
    
    if (sequencerViewMode === 'grid') {
        // Switch to circular
        sequencerViewMode = 'circular';
        gridContainer.classList.add('hidden');
        circularContainer.classList.remove('hidden');
        btnLabel.textContent = 'GRID';
        btnIcon.textContent = '▦';
        
        // Initialize circular view if not already done
        initCircularSequencer();
        syncCircularFromGrid();
        renderCircularSequencer();
    } else {
        // Switch to grid
        sequencerViewMode = 'grid';
        gridContainer.classList.remove('hidden');
        circularContainer.classList.add('hidden');
        btnLabel.textContent = 'CIRCULAR';
        btnIcon.textContent = '⭘';
        
        // Stop circular animation
        if (circularAnimationFrame) {
            cancelAnimationFrame(circularAnimationFrame);
            circularAnimationFrame = null;
        }
    }
}

// Initialize circular sequencer
function initCircularSequencer() {
    if (circularCanvas) return; // Already initialized
    
    circularCanvas = document.getElementById('circularCanvas');
    if (!circularCanvas) {
        console.error('circularCanvas not found');
        return;
    }
    
    circularCtx = circularCanvas.getContext('2d');
    
    // Set canvas size with proper handling for iOS
    const container = document.getElementById('circularSequencer');
    const containerWidth = container.clientWidth || container.offsetWidth;
    const containerHeight = container.clientHeight || container.offsetHeight;
    const size = Math.min(containerWidth, containerHeight, 600);
    
    // Ensure minimum size for visibility
    const finalSize = Math.max(size, 300);
    
    circularCanvas.width = finalSize;
    circularCanvas.height = finalSize;
    
    // Set explicit CSS size for iOS
    circularCanvas.style.width = finalSize + 'px';
    circularCanvas.style.height = finalSize + 'px';
    
    // Handle canvas clicks and touch events
    circularCanvas.addEventListener('click', handleCircularClick);
    circularCanvas.addEventListener('touchend', (e) => {
        e.preventDefault();
        const touch = e.changedTouches[0];
        const rect = circularCanvas.getBoundingClientRect();
        const event = {
            clientX: touch.clientX,
            clientY: touch.clientY
        };
        handleCircularClick(event);
    }, { passive: false });
    
    // Create track labels
    const trackLabelsContainer = document.getElementById('circularTrackLabels');
    trackLabelsContainer.innerHTML = '';
    const trackNames = ['BD', 'SD', 'CH', 'OH', 'CP', 'RS', 'CL', 'CY'];
    const trackColors = ['#ff0000', '#ffa500', '#ffff00', '#00ffff', '#ff00ff', '#00ff00', '#38ceff', '#484dff'];
    
    trackNames.forEach((name, index) => {
        const label = document.createElement('div');
        label.className = 'circular-track-label';
        label.textContent = name;
        label.style.color = trackColors[index];
        label.dataset.track = index;
        label.addEventListener('click', () => {
            if (window.selectTrack) {
                window.selectTrack(index);
            }
        });
        trackLabelsContainer.appendChild(label);
    });
}

// Sync circular data from grid
function syncCircularFromGrid() {
    document.querySelectorAll('.seq-step').forEach(el => {
        const track = parseInt(el.dataset.track);
        const step = parseInt(el.dataset.step);
        if (!isNaN(track) && !isNaN(step)) {
            circularSequencerData[track][step] = el.classList.contains('active');
        }
    });
}

// Handle clicks on circular sequencer
function handleCircularClick(event) {
    const rect = circularCanvas.getBoundingClientRect();
    const x = event.clientX - rect.left;
    const y = event.clientY - rect.top;
    
    const centerX = circularCanvas.width / 2;
    const centerY = circularCanvas.height / 2;
    
    const dx = x - centerX;
    const dy = y - centerY;
    const distance = Math.sqrt(dx * dx + dy * dy);
    
    // Calculate which ring (track) was clicked
    const maxRadius = Math.min(centerX, centerY) * 0.85;
    const minRadius = maxRadius * 0.25;
    const ringWidth = (maxRadius - minRadius) / 16;
    
    if (distance < minRadius || distance > maxRadius) return;
    
    const track = Math.floor((distance - minRadius) / ringWidth);
    if (track < 0 || track >= 16) return;
    
    // Calculate which step was clicked
    let angle = Math.atan2(dy, dx);
    angle = (angle + Math.PI * 2.5) % (Math.PI * 2); // Start from top
    const step = Math.floor((angle / (Math.PI * 2)) * 16);
    
    if (step < 0 || step >= 16) return;
    
    // Toggle step
    circularSequencerData[track][step] = !circularSequencerData[track][step];
    
    // Update grid and send to ESP32
    const gridStep = document.querySelector(`.seq-step[data-track="${track}"][data-step="${step}"]`);
    if (gridStep) {
        if (circularSequencerData[track][step]) {
            gridStep.classList.add('active');
        } else {
            gridStep.classList.remove('active');
        }
    }
    
    sendWebSocket({
        cmd: 'setStep',
        track: track,
        step: step,
        active: circularSequencerData[track][step]
    });
    
    renderCircularSequencer();
}

// Render circular sequencer
function renderCircularSequencer() {
    if (!circularCtx || sequencerViewMode !== 'circular') return;
    
    // Only re-render when step changed or data toggled
    if (typeof renderCircularSequencer._lastStep === 'undefined') {
        renderCircularSequencer._lastStep = -1;
    }
    if (renderCircularSequencer._lastStep === currentStep && !renderCircularSequencer._dirty) {
        circularAnimationFrame = requestAnimationFrame(renderCircularSequencer);
        return;
    }
    renderCircularSequencer._lastStep = currentStep;
    renderCircularSequencer._dirty = false;
    
    const ctx = circularCtx;
    const width = circularCanvas.width;
    const height = circularCanvas.height;
    const centerX = width / 2;
    const centerY = height / 2;
    
    // Clear canvas
    ctx.fillStyle = '#0a0a0a';
    ctx.fillRect(0, 0, width, height);
    
    const trackColors = [
        '#ff0000', '#ffa500', '#ffff00', '#00ffff',
        '#e6194b', '#ff00ff', '#00ff00', '#f58231',
        '#911eb4', '#46f0f0', '#f032e6', '#bcf60c',
        '#38ceff', '#fabebe', '#008080', '#484dff'
    ];
    const maxRadius = Math.min(centerX, centerY) * 0.85;
    const minRadius = maxRadius * 0.25;
    const ringWidth = (maxRadius - minRadius) / 16;
    
    // Draw circular grid
    for (let track = 0; track < 16; track++) {
        const innerRadius = minRadius + track * ringWidth;
        const outerRadius = innerRadius + ringWidth;
        const midRadius = (innerRadius + outerRadius) / 2;
        
        const isMuted = trackMutedState[track];
        
        // Draw ring outline
        ctx.strokeStyle = isMuted ? 'rgba(100, 100, 100, 0.2)' : 'rgba(255, 255, 255, 0.1)';
        ctx.lineWidth = 1;
        ctx.beginPath();
        ctx.arc(centerX, centerY, innerRadius, 0, Math.PI * 2);
        ctx.stroke();
        
        // Draw steps
        for (let step = 0; step < 16; step++) {
            const angle = (step / 16) * Math.PI * 2 - Math.PI / 2;
            const nextAngle = ((step + 1) / 16) * Math.PI * 2 - Math.PI / 2;
            
            const isActive = circularSequencerData[track][step];
            const isCurrent = step === currentStep;
            
            // Draw step arc
            ctx.beginPath();
            ctx.arc(centerX, centerY, outerRadius - 2, angle + 0.02, nextAngle - 0.02);
            ctx.arc(centerX, centerY, innerRadius + 2, nextAngle - 0.02, angle + 0.02, true);
            ctx.closePath();
            
            if (isActive) {
                if (isMuted) {
                    // Muted: gray color with low opacity
                    ctx.fillStyle = 'rgba(100, 100, 100, 0.4)';
                    ctx.globalAlpha = isCurrent ? 0.5 : 0.3;
                    ctx.fill();
                    ctx.globalAlpha = 1.0;
                } else {
                    ctx.fillStyle = trackColors[track];
                    ctx.globalAlpha = isCurrent ? 1.0 : 0.7;
                    ctx.fill();
                    ctx.globalAlpha = 1.0;
                    
                    // Add glow effect for active steps
                    ctx.shadowBlur = isCurrent ? 20 : 10;
                    ctx.shadowColor = trackColors[track];
                    ctx.fill();
                    ctx.shadowBlur = 0;
                }
            } else if (isCurrent) {
                ctx.fillStyle = isMuted ? 'rgba(80, 80, 80, 0.1)' : 'rgba(255, 255, 255, 0.15)';
                ctx.fill();
            } else {
                ctx.fillStyle = isMuted ? 'rgba(60, 60, 60, 0.02)' : 'rgba(255, 255, 255, 0.03)';
                ctx.fill();
            }
            
            // Draw step separator
            if (step % 4 === 0) {
                ctx.strokeStyle = isMuted ? 'rgba(100, 100, 100, 0.15)' : 'rgba(255, 255, 255, 0.2)';
                ctx.lineWidth = 2;
            } else {
                ctx.strokeStyle = isMuted ? 'rgba(100, 100, 100, 0.05)' : 'rgba(255, 255, 255, 0.05)';
                ctx.lineWidth = 1;
            }
            ctx.beginPath();
            ctx.moveTo(
                centerX + Math.cos(angle) * innerRadius,
                centerY + Math.sin(angle) * innerRadius
            );
            ctx.lineTo(
                centerX + Math.cos(angle) * outerRadius,
                centerY + Math.sin(angle) * outerRadius
            );
            ctx.stroke();
        }
    }
    
    // Draw center circle
    ctx.beginPath();
    ctx.arc(centerX, centerY, minRadius, 0, Math.PI * 2);
    ctx.fillStyle = 'rgba(26, 26, 26, 0.95)';
    ctx.fill();
    ctx.strokeStyle = '#ff0000';
    ctx.lineWidth = 2;
    ctx.stroke();
    
    // Draw step indicator
    const indicatorAngle = (currentStep / 16) * Math.PI * 2 - Math.PI / 2;
    ctx.strokeStyle = '#ff0000';
    ctx.lineWidth = 3;
    ctx.beginPath();
    ctx.moveTo(centerX, centerY);
    ctx.lineTo(
        centerX + Math.cos(indicatorAngle) * maxRadius,
        centerY + Math.sin(indicatorAngle) * maxRadius
    );
    ctx.stroke();
    
    // Add pulsing dot at the end of indicator
    ctx.beginPath();
    ctx.arc(
        centerX + Math.cos(indicatorAngle) * maxRadius,
        centerY + Math.sin(indicatorAngle) * maxRadius,
        5,
        0,
        Math.PI * 2
    );
    ctx.fillStyle = '#ff0000';
    ctx.shadowBlur = 15;
    ctx.shadowColor = '#ff0000';
    ctx.fill();
    ctx.shadowBlur = 0;
    
    // Update center display
    document.getElementById('circularStepNumber').textContent = currentStep + 1;
    
    // Request next frame if in circular mode
    if (sequencerViewMode === 'circular') {
        circularAnimationFrame = requestAnimationFrame(renderCircularSequencer);
    }
}

// Controls
function setupControls() {
    // Play/Stop - Use togglePlayPause for proper state management
    document.getElementById('playBtn').addEventListener('click', () => {
        togglePlayPause();
    });
    
    document.getElementById('stopBtn').addEventListener('click', () => {
        sendWebSocket({ cmd: 'stop' });
        isPlaying = false;
        updateSequencerStatusMeter();
    });
    
    document.getElementById('clearBtn').addEventListener('click', () => {
        if (confirm('¿Borrar todos los steps del pattern actual?')) {
            document.querySelectorAll('.seq-step').forEach(el => {
                const track = parseInt(el.dataset.track);
                const step = parseInt(el.dataset.step);
                if (el.classList.contains('active')) {
                    el.classList.remove('active');
                    sendWebSocket({
                        cmd: 'setStep',
                        track: track,
                        step: step,
                        active: false
                    });
                }
            });
        }
    });
    
    // View Mode Toggle Button
    const viewModeBtn = document.getElementById('viewModeBtn');
    if (viewModeBtn) {
        viewModeBtn.addEventListener('click', () => {
            toggleSequencerView();
        });
    }
    
    // Clear MIDI Monitor button
    const clearMidiBtn = document.getElementById('clearMidiMonitor');
    if (clearMidiBtn) {
        clearMidiBtn.addEventListener('click', () => {
            midiMessagesQueue.length = 0;
            const monitor = document.getElementById('midiMonitor');
            if (monitor) {
                monitor.innerHTML = `
                    <div class="monitor-placeholder">
                        <div class="placeholder-icon">🎹</div>
                        <div class="placeholder-text">Monitor limpiado</div>
                        <div class="placeholder-hint">Esperando nuevos mensajes MIDI...</div>
                    </div>
                `;
            }
        });
    }
    
    // Tempo slider
    const tempoSlider = document.getElementById('tempoSlider');
    const tempoValue = document.getElementById('tempoValue');
    
    tempoSlider.addEventListener('input', (e) => {
        const tempo = e.target.value;
        tempoValue.textContent = tempo;
        
        // Actualizar velocidad de animación del BPM
        const bpm = parseFloat(tempo);
        const beatDuration = 60 / bpm; // segundos por beat
        tempoValue.style.animationDuration = `${beatDuration}s`;
        updateBpmMeter(bpm);
    });
    
    tempoSlider.addEventListener('change', (e) => {
        sendWebSocket({
            cmd: 'tempo',
            value: parseFloat(e.target.value)
        });
    });
    
    // Sequencer volume slider
    const sequencerVolumeSlider = document.getElementById('sequencerVolumeSlider');
    const sequencerVolumeValue = document.getElementById('sequencerVolumeValue');
    
    sequencerVolumeSlider.addEventListener('input', (e) => {
        const volume = e.target.value;
        sequencerVolumeValue.textContent = volume;
        updateSequencerVolumeMeter(parseInt(volume, 10));
    });
    
    sequencerVolumeSlider.addEventListener('change', (e) => {
        const volume = parseInt(e.target.value);
        sendWebSocket({
            cmd: 'setSequencerVolume',
            value: volume
        });
    });
    
    // Live pads volume slider
    const liveVolumeSlider = document.getElementById('liveVolumeSlider');
    const liveVolumeValue = document.getElementById('liveVolumeValue');
    
    liveVolumeSlider.addEventListener('input', (e) => {
        const volume = e.target.value;
        liveVolumeValue.textContent = volume;
        updateLiveVolumeMeter(parseInt(volume, 10));
    });
    
    liveVolumeSlider.addEventListener('change', (e) => {
        const volume = parseInt(e.target.value);
        sendWebSocket({
            cmd: 'setLiveVolume',
            value: volume
        });
    });
    
    // Pattern buttons
    document.querySelectorAll('.btn-pattern').forEach(btn => {
        btn.addEventListener('click', (e) => {
            document.querySelectorAll('.btn-pattern').forEach(b => b.classList.remove('active'));
            btn.classList.add('active');
            
            const pattern = parseInt(btn.dataset.pattern);
            const patternName = btn.textContent.trim();
            
            // Actualizar display del patrón
            document.getElementById('currentPatternName').textContent = patternName;
            
            // Update circular pattern name
            const circularPatternName = document.getElementById('circularPatternName');
            if (circularPatternName) {
                circularPatternName.textContent = patternName;
            }
            
            // Cambiar pattern directamente por WebSocket
            // El backend envía automáticamente los datos del patrón
            sendWebSocket({
                cmd: 'selectPattern',
                index: pattern
            });
        });
    });
    
    // Color mode toggle
    const colorToggle = document.getElementById('colorToggle');
    colorToggle.addEventListener('click', () => {
        document.body.classList.toggle('mono-mode');
        if (document.body.classList.contains('mono-mode')) {
            colorToggle.textContent = '🎶 MONO MODE';
        } else {
            colorToggle.textContent = '🎨 COLOR MODE';
        }
        syncLedMonoMode();
    });
    
    // Botón para cargar listas de samplers
    const loadSampleListsBtn = document.getElementById('loadSampleListsBtn');
    if (loadSampleListsBtn) {
        loadSampleListsBtn.addEventListener('click', () => {
            const statusEl = document.getElementById('sampleLoadStatus');
            if (statusEl) statusEl.textContent = 'Cargando...';
            
            requestAllSamples();
            
            setTimeout(() => {
                const totalLoaded = Object.keys(sampleCatalog).length;
                if (statusEl) statusEl.textContent = `${totalLoaded}/16 familias cargadas`;
            }, 5000);
        });
    }

    // Botón Solo Pads — oculta controles extra
    const soloPadsBtn = document.getElementById('soloPadsBtn');
    if (soloPadsBtn) {
        // Auto-activate in embed mode
        const urlP = new URLSearchParams(location.search);
        if (urlP.get('solopads') === '1') {
            document.getElementById('section-pads')?.classList.add('solo-pads-mode');
            soloPadsBtn.classList.add('active');
        }
        soloPadsBtn.addEventListener('click', () => {
            const section = document.getElementById('section-pads');
            if (!section) return;
            section.classList.toggle('solo-pads-mode');
            soloPadsBtn.classList.toggle('active');
        });
    }
    
    // Botón de debug info
    const debugInfoBtn = document.getElementById('debugInfoBtn');
    if (debugInfoBtn) {
        debugInfoBtn.addEventListener('click', () => {
            console.log('=== DEBUG INFO ===');
            console.log('WebSocket state:', ws ? ws.readyState : 'null');
            console.log('Connected:', isConnected);
            console.log('Sample Counts:', sampleCounts);
            console.log('Sample Catalog families:', Object.keys(sampleCatalog));
            console.log('Catalog details:');
            Object.keys(sampleCatalog).forEach(family => {
                console.log(`  ${family}: ${sampleCatalog[family].length} samples`);
            });
        });
    }
    
    // Botón para recargar conteos
    const reloadCountsBtn = document.getElementById('reloadCountsBtn');
    if (reloadCountsBtn) {
        reloadCountsBtn.addEventListener('click', () => {
            requestSampleCounts();
        });
    }
    
    // Master FX Controls
    setupFXControls();
}

function setupFXControls() {
    // Filter Type
    const filterType = document.getElementById('filterType');
    if (filterType) {
        filterType.addEventListener('change', (e) => {
            sendWebSocket({
                cmd: 'setFilter',
                type: parseInt(e.target.value)
            });
            updateFilterMeter();
        });
    }
    
    // Filter Cutoff
    const filterCutoff = document.getElementById('filterCutoff');
    const filterCutoffValue = document.getElementById('filterCutoffValue');
    if (filterCutoff) {
        filterCutoff.addEventListener('input', (e) => {
            if (filterCutoffValue) filterCutoffValue.textContent = e.target.value;
            sendWebSocket({
                cmd: 'setFilterCutoff',
                value: parseFloat(e.target.value)
            });
            updateFilterMeter();
        });
    }
    
    // Filter Resonance
    const filterResonance = document.getElementById('filterResonance');
    const filterResonanceValue = document.getElementById('filterResonanceValue');
    if (filterResonance) {
        filterResonance.addEventListener('input', (e) => {
            if (filterResonanceValue) filterResonanceValue.textContent = parseFloat(e.target.value).toFixed(1);
            sendWebSocket({
                cmd: 'setFilterResonance',
                value: parseFloat(e.target.value)
            });
            updateFilterMeter();
        });
    }
    
    // ============= Distortion (improved with modes) =============
    const distortionMode = document.getElementById('distortionMode');
    if (distortionMode) {
        distortionMode.addEventListener('change', (e) => {
            sendWebSocket({ cmd: 'setDistortionMode', value: parseInt(e.target.value) });
        });
    }
    
    const distortion = document.getElementById('distortion');
    const distortionValue = document.getElementById('distortionValue');
    if (distortion) {
        distortion.addEventListener('input', (e) => {
            if (distortionValue) distortionValue.textContent = e.target.value;
            sendWebSocket({ cmd: 'setDistortion', value: parseFloat(e.target.value) });
        });
    }
    
    // ============= Lo-Fi Controls =============
    const lofiActive = document.getElementById('lofiActive');
    if (lofiActive) {
        lofiActive.addEventListener('change', (e) => {
            const active = e.target.checked;
            toggleFxCard(e.target, active);
            if (!active) {
                // Reset Lo-Fi to defaults (off)
                const bcEl = document.getElementById('bitCrush');
                const srEl = document.getElementById('sampleRate');
                const bcVal = document.getElementById('bitCrushValue');
                const srVal = document.getElementById('sampleRateValue');
                if (bcEl) { bcEl.value = 16; if (bcVal) bcVal.textContent = '16'; }
                if (srEl) { srEl.value = 44100; if (srVal) srVal.textContent = '44100'; }
                sendWebSocket({ cmd: 'setBitCrush', value: 16 });
                sendWebSocket({ cmd: 'setSampleRate', value: 44100 });
            }
        });
    }
    
    const bitCrush = document.getElementById('bitCrush');
    const bitCrushValue = document.getElementById('bitCrushValue');
    if (bitCrush) {
        bitCrush.addEventListener('input', (e) => {
            if (bitCrushValue) bitCrushValue.textContent = e.target.value;
            sendWebSocket({ cmd: 'setBitCrush', value: parseInt(e.target.value) });
        });
    }
    
    const sampleRate = document.getElementById('sampleRate');
    const sampleRateValue = document.getElementById('sampleRateValue');
    if (sampleRate) {
        sampleRate.addEventListener('input', (e) => {
            if (sampleRateValue) sampleRateValue.textContent = e.target.value;
            sendWebSocket({ cmd: 'setSampleRate', value: parseInt(e.target.value) });
        });
    }
    
    // ============= NEW: Delay/Echo =============
    const delayActive = document.getElementById('delayActive');
    if (delayActive) {
        delayActive.addEventListener('change', (e) => {
            sendWebSocket({ cmd: 'setDelayActive', value: e.target.checked });
            toggleFxCard(e.target, e.target.checked);
        });
    }
    
    setupFxSlider('delayTime', 'delayTimeValue', 'setDelayTime', '', true);
    setupFxSlider('delayFeedback', 'delayFeedbackValue', 'setDelayFeedback', '%', true);
    setupFxSlider('delayMix', 'delayMixValue', 'setDelayMix', '', true);
    
    // ============= NEW: Phaser =============
    const phaserActive = document.getElementById('phaserActive');
    if (phaserActive) {
        phaserActive.addEventListener('change', (e) => {
            sendWebSocket({ cmd: 'setPhaserActive', value: e.target.checked });
            toggleFxCard(e.target, e.target.checked);
        });
    }
    
    setupFxSlider('phaserRate', 'phaserRateValue', 'setPhaserRate', '', true);
    setupFxSlider('phaserDepth', 'phaserDepthValue', 'setPhaserDepth', '%', true);
    setupFxSlider('phaserFeedback', 'phaserFeedbackValue', 'setPhaserFeedback', '%', true);
    
    // ============= NEW: Flanger =============
    const flangerActive = document.getElementById('flangerActive');
    if (flangerActive) {
        flangerActive.addEventListener('change', (e) => {
            sendWebSocket({ cmd: 'setFlangerActive', value: e.target.checked });
            toggleFxCard(e.target, e.target.checked);
        });
    }
    
    setupFxSlider('flangerRate', 'flangerRateValue', 'setFlangerRate', '', true);
    setupFxSlider('flangerDepth', 'flangerDepthValue', 'setFlangerDepth', '%', true);
    setupFxSlider('flangerFeedback', 'flangerFeedbackValue', 'setFlangerFeedback', '%', true);
    setupFxSlider('flangerMix', 'flangerMixValue', 'setFlangerMix', '%', true);
    
    // ============= NEW: Compressor =============
    const compressorActive = document.getElementById('compressorActive');
    if (compressorActive) {
        compressorActive.addEventListener('change', (e) => {
            sendWebSocket({ cmd: 'setCompressorActive', value: e.target.checked });
            toggleFxCard(e.target, e.target.checked);
        });
    }
    
    setupFxSlider('compressorThreshold', 'compressorThresholdValue', 'setCompressorThreshold', 'dB', false);
    setupFxSlider('compressorRatio', 'compressorRatioValue', 'setCompressorRatio', '', false);
    setupFxSlider('compressorAttack', 'compressorAttackValue', 'setCompressorAttack', 'ms', false);
    setupFxSlider('compressorRelease', 'compressorReleaseValue', 'setCompressorRelease', 'ms', false);
    setupFxSlider('compressorMakeup', 'compressorMakeupValue', 'setCompressorMakeupGain', 'dB', false);
}

// Helper: setup a slider -> WebSocket binding
function setupFxSlider(sliderId, valueId, wsCmd, suffix, isInt) {
    const slider = document.getElementById(sliderId);
    const valueEl = document.getElementById(valueId);
    if (!slider) return;
    slider.addEventListener('input', (e) => {
        const val = isInt ? parseInt(e.target.value) : parseFloat(e.target.value);
        if (valueEl) valueEl.textContent = val;
        sendWebSocket({ cmd: wsCmd, value: val });
    });
}

// Helper: visual toggle for FX card active state
function toggleFxCard(checkbox, active) {
    const card = checkbox.closest('.fx-card');
    if (card) {
        card.classList.toggle('fx-card-active', active);
    }
}

function initHeaderMeters() {
    const tempoSlider = document.getElementById('tempoSlider');
    if (tempoSlider) {
        updateBpmMeter(parseFloat(tempoSlider.value));
    }
    const sequencerVolumeSlider = document.getElementById('sequencerVolumeSlider');
    if (sequencerVolumeSlider) {
        updateSequencerVolumeMeter(parseInt(sequencerVolumeSlider.value, 10));
    }
    const liveVolumeSlider = document.getElementById('liveVolumeSlider');
    if (liveVolumeSlider) {
        updateLiveVolumeMeter(parseInt(liveVolumeSlider.value, 10));
    }
    updateSequencerStatusMeter();
}

function getNormalizedPercentage(value, min, max) {
    if (typeof value !== 'number' || isNaN(value)) return 0;
    if (typeof min !== 'number' || isNaN(min)) min = 0;
    if (typeof max !== 'number' || isNaN(max) || max === min) return 0;
    const clamped = Math.min(Math.max(value, min), max);
    return ((clamped - min) / (max - min)) * 100;
}

function updateBpmMeter(value) {
    if (typeof value !== 'number' || isNaN(value)) return;
    const display = document.getElementById('meterBpmValue');
    const bar = document.getElementById('meterBpmBar');
    const slider = document.getElementById('tempoSlider');
    const meter = document.getElementById('meter-bpm');
    if (!display || !bar || !slider) return;
    display.textContent = Math.round(value);
    const min = parseFloat(slider.min) || 40;
    const max = parseFloat(slider.max) || 300;
    bar.style.width = `${getNormalizedPercentage(value, min, max).toFixed(1)}%`;
    if (bar.parentElement) {
        bar.parentElement.classList.add('active');
    }
    if (meter) {
        const duration = Math.max(0.2, 60 / Math.max(1, value));
        meter.style.setProperty('--bpm-heart-duration', `${duration}s`);
        meter.classList.add('bpm-heart');
    }
}

function updateSequencerVolumeMeter(value) {
    if (typeof value !== 'number' || isNaN(value)) return;
    const display = document.getElementById('meterSequencerVolumeValue');
    const bar = document.getElementById('meterSequencerVolumeBar');
    const slider = document.getElementById('sequencerVolumeSlider');
    if (!display || !bar || !slider) return;
    display.textContent = `${Math.round(value)}%`;
    // Calculate percentage based on 150 as max (100% bar width)
    // 0 = 0%, 100 = 66.6%, 150 = 100%
    const percentage = (value / 150) * 100;
    bar.style.width = `${Math.min(percentage, 100).toFixed(1)}%`;
    if (bar.parentElement) {
        bar.parentElement.classList.add('active');
    }
}

function updateLiveVolumeMeter(value) {
    if (typeof value !== 'number' || isNaN(value)) return;
    const display = document.getElementById('meterLiveVolumeValue');
    const bar = document.getElementById('meterLiveVolumeBar');
    const slider = document.getElementById('liveVolumeSlider');
    if (!display || !bar || !slider) return;
    display.textContent = `${Math.round(value)}%`;
    // Calculate percentage based on 150 as max (100% bar width)
    // 0 = 0%, 100 = 66.6%, 150 = 100%
    const percentage = (value / 150) * 100;
    bar.style.width = `${Math.min(percentage, 100).toFixed(1)}%`;
    if (bar.parentElement) {
        bar.parentElement.classList.add('active');
    }
}

let _lastStatusPlaying = null;
function updateSequencerStatusMeter() {
    if (_lastStatusPlaying === isPlaying) return;
    _lastStatusPlaying = isPlaying;
    const meterValue = document.getElementById('meterSequencerStatus');
    const meterBar = document.getElementById('meterSequencerStatusBar');
    if (!meterValue || !meterBar) return;
    
    const barWrapper = meterBar.parentElement;
    
    if (isPlaying) {
        meterValue.textContent = '▶ PLAY';
        meterBar.style.width = '100%';
        if (barWrapper) {
            barWrapper.classList.add('active');
        }
    } else {
        meterValue.textContent = '⬛ STOP';
        meterBar.style.width = '0%';
        if (barWrapper) {
            barWrapper.classList.remove('active');
        }
    }
}

function syncLedMonoMode() {
    const isMono = document.body.classList.contains('mono-mode');
    sendWebSocket({
        cmd: 'setLedMonoMode',
        value: isMono
    });
}

// === SYNC LEDS TOGGLE ===
// ============= AI Chat Toggle =============
function initAiToggle() {
    const btn = document.getElementById('aiToggleBtn');
    const panel = document.getElementById('seqChatPanel');
    if (!btn || !panel) return;

    let aiEnabled = false;

    btn.addEventListener('click', () => {
        aiEnabled = !aiEnabled;
        const label = btn.querySelector('.seq-btn-label');
        if (aiEnabled) {
            panel.classList.remove('ai-disabled');
            panel.classList.add('expanded');
            btn.classList.add('active');
            if (label) label.textContent = 'AI ON';
            // Auto-connect chat on first enable
            if (window.chatConnect) window.chatConnect();
        } else {
            panel.classList.add('ai-disabled');
            panel.classList.remove('expanded');
            btn.classList.remove('active');
            if (label) label.textContent = 'AI OFF';
        }
    });
}

function initSyncLeds() {
    const toggle = document.getElementById('syncLedsToggle');
    if (toggle) {
        toggle.addEventListener('change', (e) => {
            syncLedsEnabled = e.target.checked;
            if (window.showToast && window.TOAST_TYPES) {
                window.showToast(
                    syncLedsEnabled ? '💡 Sync LEDs ON — pads flash con sequencer' : '💡 Sync LEDs OFF',
                    window.TOAST_TYPES.INFO, 1500
                );
            }
        });
    }
}


function updateSequencerState(data) {
    const tempoSlider = document.getElementById('tempoSlider');
    const tempoValue = document.getElementById('tempoValue');
    if (data.tempo !== undefined && tempoSlider && tempoValue) {
        const tempoString = String(data.tempo);
        if (tempoSlider.value !== tempoString || tempoValue.textContent !== tempoString) {
            tempoSlider.value = tempoString;
            tempoValue.textContent = tempoString;
            updateBpmMeter(parseFloat(data.tempo));
        }
    }
    if (data.sequencerVolume !== undefined) {
        const sequencerVolumeSlider = document.getElementById('sequencerVolumeSlider');
        const sequencerVolumeValue = document.getElementById('sequencerVolumeValue');
        if (sequencerVolumeSlider && sequencerVolumeValue) {
            const seqVolumeString = String(data.sequencerVolume);
            if (sequencerVolumeSlider.value !== seqVolumeString || sequencerVolumeValue.textContent !== seqVolumeString) {
                sequencerVolumeSlider.value = seqVolumeString;
                sequencerVolumeValue.textContent = seqVolumeString;
                updateSequencerVolumeMeter(parseInt(data.sequencerVolume, 10));
            }
        }
    }
    if (data.liveVolume !== undefined) {
        const liveVolumeSlider = document.getElementById('liveVolumeSlider');
        const liveVolumeValue = document.getElementById('liveVolumeValue');
        if (liveVolumeSlider && liveVolumeValue) {
            const liveVolumeString = String(data.liveVolume);
            if (liveVolumeSlider.value !== liveVolumeString || liveVolumeValue.textContent !== liveVolumeString) {
                liveVolumeSlider.value = liveVolumeString;
                liveVolumeValue.textContent = liveVolumeString;
                updateLiveVolumeMeter(parseInt(data.liveVolume, 10));
            }
        }
    }
    
    // Update master volume displays in volumes section
    if (data.sequencerVolume !== undefined || data.liveVolume !== undefined) {
        const seqVol = data.sequencerVolume !== undefined ? data.sequencerVolume : 100;
        const liveVol = data.liveVolume !== undefined ? data.liveVolume : 100;
        if (window.updateMasterVolumeDisplays) {
            window.updateMasterVolumeDisplays(seqVol, liveVol);
        }
    }
    const loopTracksToUpdate = new Set();
    if (Array.isArray(data.loopActive)) {
        data.loopActive.forEach((active, track) => {
            if (!padLoopState[track]) {
                padLoopState[track] = { active: false, paused: false };
            }
            const nextValue = !!active;
            if (padLoopState[track].active !== nextValue) {
                padLoopState[track].active = nextValue;
                loopTracksToUpdate.add(track);
            }
        });
    }
    if (Array.isArray(data.loopPaused)) {
        data.loopPaused.forEach((paused, track) => {
            if (!padLoopState[track]) {
                padLoopState[track] = { active: false, paused: false };
            }
            const nextValue = !!paused;
            if (padLoopState[track].paused !== nextValue) {
                padLoopState[track].paused = nextValue;
                loopTracksToUpdate.add(track);
            }
        });
    }
    loopTracksToUpdate.forEach((track) => updatePadLoopVisual(track));
    if (Array.isArray(data.trackMuted)) {
        data.trackMuted.forEach((muted, track) => {
            const nextMuted = !!muted;
            if (trackMutedState[track] !== nextMuted) {
                setTrackMuted(track, nextMuted, false);
            }
        });
    }
    
    // Load track volumes from state
    if (Array.isArray(data.trackVolumes)) {
        data.trackVolumes.forEach((volume, track) => {
            if (track < 16) {
                updateTrackVolume(track, volume);
            }
        });
    }

    if (data.step !== undefined) {
        updateCurrentStep(data.step);
    }
    
    // Update playing state
    isPlaying = data.playing || false;
    
    // Update sequencer status meter
    updateSequencerStatusMeter();
    
    // Update pattern button
    if (data.pattern !== undefined) {
        document.querySelectorAll('.btn-pattern').forEach(btn => {
            btn.classList.toggle('active', parseInt(btn.dataset.pattern) === data.pattern);
        });
        
        // Solo solicitar patrón si cambió (no en cada state update)
        const currentActiveButton = document.querySelector('.btn-pattern.active');
        const currentPatternIndex = currentActiveButton ? parseInt(currentActiveButton.dataset.pattern) : -1;
        if (currentPatternIndex !== data.pattern) {
            // Pattern changed, request new pattern data
            setTimeout(() => {
                sendWebSocket({ cmd: 'getPattern' });
            }, 100);
        }
    }
    
    // Update song mode state
    if (data.songMode !== undefined) {
        updateSongModeUI(data.songMode, data.songLength || 1, data.pattern || 0);
    }
}

// ============= SONG MODE UI =============

let songModeActive = false;
let songLength = 1;
let currentSongBar = 0;

function updateSongModeUI(enabled, length, currentPattern) {
    songModeActive = enabled;
    songLength = length;
    currentSongBar = currentPattern;
    
    const navigator = document.getElementById('songBarNavigator');
    if (!navigator) return;
    
    if (!enabled) {
        navigator.style.display = 'none';
        return;
    }
    
    navigator.style.display = 'block';
    
    // Update bar indicator
    const barLabel = document.getElementById('songCurrentBar');
    if (barLabel) {
        barLabel.textContent = `Bar ${currentPattern + 1}/${length}`;
    }
    
    // Generate bar buttons
    const buttonsContainer = document.getElementById('songBarButtons');
    if (buttonsContainer) {
        buttonsContainer.innerHTML = '';
        for (let i = 0; i < length; i++) {
            const btn = document.createElement('button');
            btn.className = 'song-bar-btn' + (i === currentPattern ? ' active' : '');
            btn.textContent = i + 1;
            btn.title = `Bar ${i + 1}`;
            btn.addEventListener('click', () => {
                sendWebSocket({ cmd: 'selectPattern', index: i });
                // Update local state immediately
                updateSongBarHighlight(i);
                // Request pattern data
                setTimeout(() => sendWebSocket({ cmd: 'getPattern' }), 50);
            });
            buttonsContainer.appendChild(btn);
        }
    }
}

function updateSongBarHighlight(barIndex) {
    currentSongBar = barIndex;
    const buttons = document.querySelectorAll('.song-bar-btn');
    buttons.forEach((btn, i) => {
        btn.classList.toggle('active', i === barIndex);
    });
    const barLabel = document.getElementById('songCurrentBar');
    if (barLabel) {
        barLabel.textContent = `Bar ${barIndex + 1}/${songLength}`;
    }
}

function handleSongPatternChange(pattern, songLen) {
    // Called when ESP32 auto-advances pattern in song mode
    songModeActive = true;
    songLength = songLen;
    updateSongBarHighlight(pattern);
    
    // Request new pattern data for display
    sendWebSocket({ cmd: 'getPattern' });
    
    // Update pattern button in controls tab
    document.querySelectorAll('.btn-pattern').forEach(btn => {
        btn.classList.toggle('active', parseInt(btn.dataset.pattern) === pattern);
    });
    
    // Update pattern name display
    const patternName = `BAR ${pattern + 1}`;
    const patternNameEl = document.getElementById('currentPatternName');
    if (patternNameEl) patternNameEl.textContent = patternName;
    const circularPatternName = document.getElementById('circularPatternName');
    if (circularPatternName) circularPatternName.textContent = patternName;
}

function exitSongMode() {
    sendWebSocket({ cmd: 'setSongMode', enabled: false, length: 1 });
    songModeActive = false;
    const navigator = document.getElementById('songBarNavigator');
    if (navigator) navigator.style.display = 'none';
}

// Callback for midi-import.js to trigger song mode UI
window.onSongModeActivated = function(length, midiFileName) {
    updateSongModeUI(true, length, 0);
    // Show MIDI filename in song navigator
    const filenameEl = document.getElementById('songMidiFilename');
    if (filenameEl && midiFileName) {
        filenameEl.textContent = '📄 ' + midiFileName;
        filenameEl.style.display = 'inline';
    }
};

// Send WebSocket message (returns true if sent)
function sendWebSocket(data) {
    if (ws && ws.readyState === WebSocket.OPEN) {
        ws.send(JSON.stringify(data));
        return true;
    }
    return false;
}

// Check WebSocket connection status
function isWebSocketReady() {
    return ws && ws.readyState === WebSocket.OPEN;
}

// Export to window for keyboard-controls.js and midi-import.js
window.sendWebSocket = sendWebSocket;
window.isWebSocketReady = isWebSocketReady;

// ============= KEYBOARD CONTROLS =============

function setupKeyboardControls() {
    // Mapeo de teclas a pads (pads 0-7 con teclas 1-8)
    const keyToPad = padKeyBindings.reduce((mapping, key, idx) => {
        if (key) mapping[key.toUpperCase()] = idx;
        return mapping;
    }, {});

    const codeToPad = {
        Digit1: 0, Digit2: 1, Digit3: 2, Digit4: 3,
        Digit5: 4, Digit6: 5, Digit7: 6, Digit8: 7,
        Digit9: 8, Digit0: 9,
        KeyU: 10, KeyI: 11, KeyO: 12, KeyP: 13, KeyD: 14, KeyF: 15
    };

    const getPadIndexFromEvent = (e) => {
        const key = e.key.toUpperCase();
        if (keyToPad.hasOwnProperty(key)) {
            return keyToPad[key];
        }
        if (codeToPad.hasOwnProperty(e.code)) {
            return codeToPad[e.code];
        }
        return null;
    };
    
    // Export immediately for keyboard-controls.js
    window.getPadIndexFromEvent = getPadIndexFromEvent;
    window.keyboardPadsActive = keyboardPadsActive;
    window.setTrackMuted = setTrackMuted;
    window.trackMutedState = trackMutedState;
    window.startKeyboardTremolo = startKeyboardTremolo;
    window.stopKeyboardTremolo = stopKeyboardTremolo;
    
    // Keyboard handler for pad RELEASE (keyup) - keydown handled in keyboard-controls.js// Keyboard handler for pad RELEASE (keyup) - keydown handled in keyboard-controls.js
    document.addEventListener('keyup', (e) => {
        const key = e.key.toUpperCase();
        
        // Soltar pads
        const padIndex = getPadIndexFromEvent(e);
        if (padIndex !== null) {
            e.preventDefault();
            
            if (keyboardPadsActive[padIndex]) {
                keyboardPadsActive[padIndex] = false;
                const padElement = document.querySelector(`.pad[data-pad="${padIndex}"]`);
                if (padElement) {
                    stopKeyboardTremolo(padIndex, padElement);
                }
            }
        }
    });
    
    // Export functions for keyboard-controls.js
    window.togglePlayPause = togglePlayPause;
    window.changePattern = changePattern;
    window.selectPattern = selectPattern;
    window.adjustBPM = adjustBPM;
    window.adjustVolume = adjustVolume;
    window.adjustSequencerVolume = adjustSequencerVolume;
    window.getPadIndexFromEvent = getPadIndexFromEvent;
    window.keyboardPadsActive = keyboardPadsActive;
    window.startKeyboardTremolo = startKeyboardTremolo;
    window.stopKeyboardTremolo = stopKeyboardTremolo;
    window.exitSongMode = exitSongMode;
    
    // Song mode exit button
    const songExitBtn = document.getElementById('songModeExitBtn');
    if (songExitBtn) {
        songExitBtn.addEventListener('click', exitSongMode);
    }
    
    // Export pad filter functions and state
    window.padFilterState = padFilterState;
    window.trackFilterState = trackFilterState;
    window.padSeqSyncEnabled = padSeqSyncEnabled;
    window.updatePadFilterIndicator = updatePadFilterIndicator;
    window.setPadFilter = setPadFilter;
    window.clearPadFilter = clearPadFilter;
    
    // Sync toggle event handlers
    setupSyncToggles();
}

function changePattern(delta) {
    const patternButtons = Array.from(document.querySelectorAll('.btn-pattern'));
    if (patternButtons.length === 0) return;
    const currentIndex = patternButtons.findIndex(btn => btn.classList.contains('active'));
    const safeIndex = currentIndex >= 0 ? currentIndex : 0;
    const nextIndex = (safeIndex + delta + patternButtons.length) % patternButtons.length;
    patternButtons[nextIndex].click();
}

function selectPattern(index) {
    const patternButtons = Array.from(document.querySelectorAll('.btn-pattern'));
    if (patternButtons.length === 0) return;
    if (index >= 0 && index < patternButtons.length) {
        patternButtons[index].click();
    }
}


function togglePlayPause() {
    if (isPlaying) {
        // Pause
        sendWebSocket({ cmd: 'stop' });
        isPlaying = false;
    } else {
        // Play
        sendWebSocket({ cmd: 'start' });
        isPlaying = true;
    }
    updateSequencerStatusMeter();
    return isPlaying;
}

function adjustBPM(change) {
    const tempoSlider = document.getElementById('tempoSlider');
    const tempoValue = document.getElementById('tempoValue');
    
    if (tempoSlider && tempoValue) {
        let currentTempo = parseFloat(tempoSlider.value);
        let newTempo = currentTempo + change;
        
        // Limitar entre min y max
        const min = parseFloat(tempoSlider.min) || 40;
        const max = parseFloat(tempoSlider.max) || 300;
        newTempo = Math.max(min, Math.min(max, newTempo));
        
        tempoSlider.value = newTempo;
        tempoValue.textContent = newTempo;
        updateBpmMeter(newTempo);
        
        // Enviar al ESP32
        sendWebSocket({
            cmd: 'tempo',
            value: newTempo
        });
        
        // Actualizar animación del BPM
        const beatDuration = 60 / newTempo;
        tempoValue.style.animationDuration = `${beatDuration}s`;
    }
}

function adjustVolume(change) {
    const liveVolumeSlider = document.getElementById('liveVolumeSlider');
    const liveVolumeValue = document.getElementById('liveVolumeValue');
    
    if (liveVolumeSlider && liveVolumeValue) {
        let currentVolume = parseInt(liveVolumeSlider.value);
        let newVolume = currentVolume + change;
        
        // Limitar entre 0 y 100
        newVolume = Math.max(0, Math.min(100, newVolume));
        
        liveVolumeSlider.value = newVolume;
        liveVolumeValue.textContent = newVolume;
        updateLiveVolumeMeter(newVolume);
        
        // Enviar al ESP32
        sendWebSocket({
            cmd: 'setLiveVolume',
            value: newVolume
        });
    }
}

function adjustSequencerVolume(change) {
    const sequencerVolumeSlider = document.getElementById('sequencerVolumeSlider');
    const sequencerVolumeValue = document.getElementById('sequencerVolumeValue');
    
    if (sequencerVolumeSlider && sequencerVolumeValue) {
        let currentVolume = parseInt(sequencerVolumeSlider.value);
        let newVolume = currentVolume + change;
        
        // Limitar entre 0 y 100
        newVolume = Math.max(0, Math.min(100, newVolume));
        
        sequencerVolumeSlider.value = newVolume;
        sequencerVolumeValue.textContent = newVolume;
        updateSequencerVolumeMeter(newVolume);
        
        // Enviar al ESP32
        sendWebSocket({
            cmd: 'setSequencerVolume',
            value: newVolume
        });
    }
}

// ========================================
// TAB SYSTEM (Nuevo sistema de pestañas)
// ========================================

let currentTab = 'performance';

function initTabSystem() {
    const tabBtns = document.querySelectorAll('.tab-btn');

    // Multiview embed mode: activate via URL params (?embed=1&tab=sequencer)
    const urlParams = new URLSearchParams(location.search);
    if (urlParams.get('embed') === '1') {
        document.body.classList.add('embed-mode');
    }
    const urlTab = urlParams.get('tab');
    if (urlTab) {
        // URL param takes priority (multiview selected this tab)
        switchTab(urlTab);
    } else {
        // Fall back to saved preference
        const savedTab = localStorage.getItem('currentTab');
        if (savedTab) switchTab(savedTab);
    }

    // Event listeners para los botones de tabs
    tabBtns.forEach(btn => {
        btn.addEventListener('click', () => {
            const tabId = btn.getAttribute('data-tab');
            if (tabId) switchTab(tabId);
        });
    });
}

function switchTab(tabId) {
    currentTab = tabId;
    
    // Actualizar botones
    document.querySelectorAll('.tab-btn').forEach(btn => {
        if (btn.getAttribute('data-tab') === tabId) {
            btn.classList.add('active');
        } else {
            btn.classList.remove('active');
        }
    });
    
    // Actualizar contenido
    document.querySelectorAll('.tab-content').forEach(content => {
        if (content.id === `tab-${tabId}`) {
            content.classList.add('active');
        } else {
            content.classList.remove('active');
        }
    });
    
    // Guardar preferencia
    localStorage.setItem('currentTab', tabId);
}

// Sample Selector Functions
function showSampleSelector(padIndex, family) {
    sampleSelectorContext = { padIndex, family };
    // Solicitar lista de samples bajo demanda
    sendWebSocket({
        cmd: 'getSamples',
        family: family,
        pad: padIndex
    });
}

function displaySampleList(data) {
    const padIndex = data.pad;
    const family = data.family;
    const samples = data.samples;
    
    if (!samples || samples.length === 0) {
        if (sampleSelectorContext && sampleSelectorContext.family === family) {
            alert(`No samples found for ${family}`);
        }
        return;
    }

    // Update catalog for browser
    sampleCatalog[family] = samples.map(sample => ({
        family,
        name: sample.name,
        size: sample.size,
        format: sample.format ? sample.format.toUpperCase() : inferFormatFromName(sample.name),
        rate: sample.rate || 0,
        channels: sample.channels || 1,
        bits: sample.bits || 16
    }));
    scheduleSampleBrowserRender();

    if (!sampleSelectorContext || sampleSelectorContext.family !== family || sampleSelectorContext.padIndex !== padIndex) {
        return;
    }
    
    // Crear modal
    const modal = document.createElement('div');
    modal.className = 'sample-modal';
    modal.innerHTML = `
        <div class="sample-modal-content">
            <h3>Select ${family} Sample for Pad ${padIndex + 1}</h3>
            <div class="sample-modal-waveform">
                <div class="waveform-preview-label">📊 FORMA DE ONDA</div>
                <div class="waveform-canvas-wrapper" id="waveformCanvasWrapper">
                    <canvas id="samplePreviewWaveform" class="sample-preview-canvas" width="400" height="100"></canvas>
                    <div class="waveform-marker waveform-marker-start" id="waveformMarkerStart" title="Arrastra para Start">S</div>
                    <div class="waveform-marker waveform-marker-end" id="waveformMarkerEnd" title="Arrastra para End">E</div>
                </div>
                <div class="waveform-trim-controls">
                    <span class="waveform-trim-value" id="trimStartValue">Start: 0%</span>
                    <span class="waveform-preview-info" id="samplePreviewInfo">Selecciona un sample</span>
                    <span class="waveform-trim-value" id="trimEndValue">End: 100%</span>
                </div>
                <!-- FADE IN / FADE OUT controls -->
                <div class="sample-fade-controls">
                    <div class="fade-control">
                        <label><span class="fade-icon">🌅</span> FADE IN <span class="fade-value-display" id="fadeInDisplay">0ms</span></label>
                        <div class="fade-preview"><div class="fade-preview-bar fade-in-preview" id="fadeInPreviewBar" style="width:0%"></div></div>
                        <input type="range" id="fadeInSlider" min="0" max="500" step="5" value="0">
                    </div>
                    <div class="fade-control">
                        <label><span class="fade-icon">🌇</span> FADE OUT <span class="fade-value-display" id="fadeOutDisplay">0ms</span></label>
                        <div class="fade-preview"><div class="fade-preview-bar fade-out-preview" id="fadeOutPreviewBar" style="width:0%"></div></div>
                        <input type="range" id="fadeOutSlider" min="0" max="500" step="5" value="0">
                    </div>
                </div>
            </div>
            <div class="sample-list"></div>
            <div class="sample-modal-actions">
                <button class="btn-preview-play" id="btnPreviewPlay" disabled>▶ PLAY</button>
                <button class="btn-trim-load" id="btnTrimLoad" disabled>✂️ TRIM & LOAD</button>
                <button class="btn-close-modal">Cerrar</button>
            </div>
        </div>
    `;
    
    // Waveform state for this modal — includes fade params
    const wfState = {
        startNorm: 0, endNorm: 1,
        fadeInMs: 0, fadeOutMs: 0,
        selectedFile: null, selectedFamily: family,
        peaks: null, padIndex: padIndex
    };
    
    // Bind fade sliders
    const fadeInSlider = modal.querySelector('#fadeInSlider');
    const fadeOutSlider = modal.querySelector('#fadeOutSlider');
    const fadeInDisplay = modal.querySelector('#fadeInDisplay');
    const fadeOutDisplay = modal.querySelector('#fadeOutDisplay');
    const fadeInPreviewBar = modal.querySelector('#fadeInPreviewBar');
    const fadeOutPreviewBar = modal.querySelector('#fadeOutPreviewBar');
    
    fadeInSlider.addEventListener('input', () => {
        wfState.fadeInMs = parseInt(fadeInSlider.value, 10);
        fadeInDisplay.textContent = wfState.fadeInMs + 'ms';
        fadeInPreviewBar.style.width = (wfState.fadeInMs / 500 * 100) + '%';
    });
    fadeOutSlider.addEventListener('input', () => {
        wfState.fadeOutMs = parseInt(fadeOutSlider.value, 10);
        fadeOutDisplay.textContent = wfState.fadeOutMs + 'ms';
        fadeOutPreviewBar.style.width = (wfState.fadeOutMs / 500 * 100) + '%';
    });
    
    // Show current pad waveform if already loaded
    const previewCanvas = modal.querySelector('#samplePreviewWaveform');
    if (typeof SampleWaveform !== 'undefined') {
        SampleWaveform.fetchWaveform(padIndex).then(data => {
            if (data && data.peaks) {
                wfState.peaks = data.peaks;
                _drawWaveformWithMarkers(previewCanvas, wfState);
                const info = modal.querySelector('#samplePreviewInfo');
                if (info && data.duration) {
                    const dur = (data.duration / 1000).toFixed(2);
                    info.textContent = `${data.name || ''} · ${dur}s`;
                }
            }
        });
    }
    
    // Setup draggable S/E markers
    _setupWaveformMarkers(modal, previewCanvas, wfState);
    
    const sampleList = modal.querySelector('.sample-list');
    
    samples.forEach(sample => {
        const sampleItem = document.createElement('div');
        sampleItem.className = 'sample-item';
        const sizeKB = (sample.size / 1024).toFixed(1);
        sampleItem.innerHTML = `
            <span class="sample-name">${sample.name}</span>
            <span class="sample-size">${sizeKB} KB</span>
        `;
        sampleItem.addEventListener('click', () => {
            // Mark selected
            sampleList.querySelectorAll('.sample-item').forEach(s => s.classList.remove('selected'));
            sampleItem.classList.add('selected');
            
            // Fetch waveform from file
            wfState.selectedFile = sample.name;
            wfState.startNorm = 0;
            wfState.endNorm = 1;
            _updateTrimLabels(modal, wfState);
            
            const filePath = `/${family}/${sample.name}`;
            const info = modal.querySelector('#samplePreviewInfo');
            if (info) info.textContent = 'Cargando forma de onda...';
            
            fetch(`/api/waveform?file=${encodeURIComponent(filePath)}&points=200`)
                .then(r => r.json())
                .then(data => {
                    if (data && data.peaks) {
                        wfState.peaks = data.peaks;
                        _drawWaveformWithMarkers(previewCanvas, wfState);
                        if (info) {
                            const dur = (data.duration / 1000).toFixed(2);
                            info.textContent = `${sample.name} · ${dur}s · ${data.samples} samples`;
                        }
                        modal.querySelector('#btnTrimLoad').disabled = false;
                        modal.querySelector('#btnPreviewPlay').disabled = false;
                    }
                })
                .catch(() => {
                    if (info) info.textContent = 'Error cargando waveform';
                });
        });
        sampleList.appendChild(sampleItem);
    });
    
    // Preview Play button — loads with trim+fade and auto-triggers
    modal.querySelector('#btnPreviewPlay').addEventListener('click', () => {
        if (!wfState.selectedFile) return;
        const btn = modal.querySelector('#btnPreviewPlay');
        btn.textContent = '⏳ ...';
        btn.disabled = true;
        loadSampleToPad(padIndex, family, wfState.selectedFile, true, wfState.startNorm, wfState.endNorm, wfState.fadeInMs, wfState.fadeOutMs);
        // Re-enable after sample loads (~400ms)
        setTimeout(() => {
            btn.textContent = '▶ PLAY';
            btn.disabled = false;
            // Refresh waveform from loaded pad to show trimmed result
            if (typeof SampleWaveform !== 'undefined') {
                SampleWaveform.clearCache(padIndex);
            }
        }, 500);
    });
    
    // Trim & Load button
    modal.querySelector('#btnTrimLoad').addEventListener('click', () => {
        if (!wfState.selectedFile) return;
        loadSampleToPad(padIndex, family, wfState.selectedFile, false, wfState.startNorm, wfState.endNorm, wfState.fadeInMs, wfState.fadeOutMs);
        modal.parentNode.removeChild(modal);
        sampleSelectorContext = null;
    });
    
    modal.querySelector('.btn-close-modal').addEventListener('click', () => {
        modal.parentNode.removeChild(modal);
        sampleSelectorContext = null;
    });
    
    document.body.appendChild(modal);
}

function initSampleBrowser() {
    const filters = document.getElementById('sampleFilters');
    const list = document.getElementById('sampleBrowserList');
    if (!filters || !list) return;

    const allButton = document.createElement('button');
    allButton.className = 'sample-filter active';
    allButton.textContent = 'TODOS';
    allButton.dataset.family = 'ALL';
    filters.appendChild(allButton);

    const refreshButton = document.createElement('button');
    refreshButton.className = 'sample-refresh';
    refreshButton.textContent = '↻';
    refreshButton.title = 'Actualizar lista';
    refreshButton.addEventListener('click', (e) => {
        e.preventDefault();
        requestAllSamples();
    });
    filters.appendChild(refreshButton);

    padNames.forEach((family) => {
        const btn = document.createElement('button');
        btn.className = 'sample-filter';
        btn.textContent = family;
        btn.dataset.family = family;
        filters.appendChild(btn);
    });

    filters.addEventListener('click', (e) => {
        const button = e.target.closest('.sample-filter');
        if (!button) return;
        setSampleFilter(button.dataset.family);
    });

    setupSampleFilterControls();
}

function initInstrumentTabs() {
    const tabs = document.querySelectorAll('.instrument-tab');
    const panels = document.querySelectorAll('.instrument-panel');
    if (!tabs.length || !panels.length) return;

    tabs.forEach(tab => {
        tab.addEventListener('click', () => {
            const target = tab.dataset.tab;
            tabs.forEach(t => t.classList.toggle('active', t === tab));
            panels.forEach(panel => {
                panel.classList.toggle('active', panel.dataset.panel === target);
            });
            if (target === 'all') {
                const hasCatalog = padNames.some((family) => (sampleCatalog[family] || []).length > 0);
                if (!hasCatalog) {
                    requestAllSamples();
                }
                scheduleSampleBrowserRender();
            }
        });
    });
}

function setupSampleFilterControls() {
    const familySelect = document.getElementById('sampleFilterFamily');
    const formatSelect = document.getElementById('sampleFilterFormat');
    const rateSelect = document.getElementById('sampleFilterRate');
    const channelSelect = document.getElementById('sampleFilterChannels');
    const activeToggle = document.getElementById('sampleFilterActive');

    if (!familySelect || !formatSelect || !rateSelect || !channelSelect || !activeToggle) return;

    familySelect.innerHTML = '';
    const allOption = document.createElement('option');
    allOption.value = 'ALL';
    allOption.textContent = 'FAMILIA';
    familySelect.appendChild(allOption);
    padNames.forEach((family) => {
        const opt = document.createElement('option');
        opt.value = family;
        opt.textContent = family;
        familySelect.appendChild(opt);
    });

    formatSelect.innerHTML = `
        <option value="ALL">FORMATO</option>
        <option value="WAV">WAV</option>
        <option value="RAW">RAW</option>
    `;

    rateSelect.innerHTML = `
        <option value="ALL">KHZ</option>
        <option value="8000">8k</option>
        <option value="11025">11k</option>
        <option value="22050">22k</option>
        <option value="44100">44k</option>
    `;

    channelSelect.innerHTML = `
        <option value="ALL">CANAL</option>
        <option value="1">MONO</option>
        <option value="2">STEREO</option>
    `;

    const onFilterChange = () => scheduleSampleBrowserRender();
    familySelect.addEventListener('change', onFilterChange);
    formatSelect.addEventListener('change', onFilterChange);
    rateSelect.addEventListener('change', onFilterChange);
    channelSelect.addEventListener('change', onFilterChange);
    activeToggle.addEventListener('change', onFilterChange);
}

function setSampleFilter(family) {
    activeSampleFilter = family || 'ALL';
    document.querySelectorAll('.sample-filter').forEach(btn => {
        btn.classList.toggle('active', btn.dataset.family === activeSampleFilter);
    });
    scheduleSampleBrowserRender();
}

function requestSampleCounts() {
    sendWebSocket({
        cmd: 'getSampleCounts'
    });
}

function requestAllSamples() {
    if (!ws || ws.readyState !== WebSocket.OPEN) {
        return;
    }
    if (sampleRequestTimers.length) {
        sampleRequestTimers.forEach(timerId => clearTimeout(timerId));
        sampleRequestTimers = [];
    }
    const delayStep = 80;
    padNames.forEach((family, padIndex) => {
        const timerId = setTimeout(() => {
            sendWebSocket({
                cmd: 'getSamples',
                family,
                pad: padIndex
            });
        }, padIndex * delayStep);
        sampleRequestTimers.push(timerId);
    });
}

function scheduleSampleBrowserRender() {
    if (sampleBrowserRenderTimer) {
        clearTimeout(sampleBrowserRenderTimer);
    }
    sampleBrowserRenderTimer = setTimeout(() => {
        sampleBrowserRenderTimer = null;
        renderSampleBrowserList(activeSampleFilter);
    }, 120);
}

function renderSampleBrowserList(family) {
    const list = document.getElementById('sampleBrowserList');
    if (!list) return;
    const families = family === 'ALL' ? padNames : [family];
    const familyFilter = document.getElementById('sampleFilterFamily')?.value || 'ALL';
    const formatFilter = document.getElementById('sampleFilterFormat')?.value || 'ALL';
    const rateFilter = document.getElementById('sampleFilterRate')?.value || 'ALL';
    const channelFilter = document.getElementById('sampleFilterChannels')?.value || 'ALL';
    const activeOnly = document.getElementById('sampleFilterActive')?.checked || false;
    const activeLookup = getLoadedSampleLookup();
    const rows = [];

    families.forEach((fam) => {
        const samples = sampleCatalog[fam] || [];
        samples.forEach(sample => rows.push(sample));
    });

    list.innerHTML = '';

    if (rows.length === 0) {
        list.innerHTML = '<div class="sample-empty">Sin samples para este filtro.</div>';
        return;
    }

    rows.sort((a, b) => {
        const familyA = a.family || '';
        const familyB = b.family || '';
        const nameA = a.name || '';
        const nameB = b.name || '';
        return familyA.localeCompare(familyB) || nameA.localeCompare(nameB);
    });

    const filteredRows = rows.filter(sample => {
        if (familyFilter !== 'ALL' && sample.family !== familyFilter) return false;
        if (formatFilter !== 'ALL' && sample.format !== formatFilter) return false;
        if (rateFilter !== 'ALL' && String(sample.rate || '') !== rateFilter) return false;
        if (channelFilter !== 'ALL' && String(sample.channels || '') !== channelFilter) return false;
        if (activeOnly) {
            const activeName = activeLookup[sample.family];
            if (!activeName || activeName !== sample.name) return false;
        }
        return true;
    });

    if (filteredRows.length === 0) {
        list.innerHTML = '<div class="sample-empty">Sin samples para este filtro.</div>';
        return;
    }

    filteredRows.forEach(sample => {
        const row = document.createElement('div');
        row.className = 'sample-row instrument-card';
        const isActive = activeLookup[sample.family] === sample.name;
        if (isActive) {
            row.classList.add('active');
        }
        const sizeKB = (sample.size / 1024).toFixed(1);
        const format = sample.format || inferFormatFromName(sample.name);
        const rate = sample.rate ? `${Math.round(sample.rate / 1000)}kHz` : '—';
        const channels = sample.channels === 2 ? 'Stereo' : 'Mono';
        row.innerHTML = `
            <div class="inst-main">
                <span class="inst-code">${sample.family}</span>
                <div>
                    <div class="inst-name">${sample.name}</div>
                    <div class="inst-count">${sample.family} • ${sizeKB} KB</div>
                </div>
            </div>
            <div class="inst-meta">
                <span class="inst-current">Format: ${format} • ${rate} • ${channels}</span>
                <span class="inst-quality">${isActive ? 'ACTIVO' : 'DISPONIBLE'}</span>
            </div>
            ${isActive ? '<span class="sample-row-badge">ACTIVE</span>' : ''}
            <button class="sample-row-play" title="Reproducir">▶</button>
        `;

        row.querySelector('.sample-row-play').addEventListener('click', (e) => {
            e.stopPropagation();
            auditionSample(sample.family, sample.name);
        });

        row.addEventListener('click', () => {
            auditionSample(sample.family, sample.name);
        });

        list.appendChild(row);
    });
}

function auditionSample(family, filename) {
    const padIndex = padNames.indexOf(family);
    if (padIndex === -1) return;
    loadSampleToPad(padIndex, family, filename, true);
}

function loadSampleToPad(padIndex, family, filename, autoPlay = false, trimStart = 0, trimEnd = 1, fadeInMs = 0, fadeOutMs = 0) {
    if (autoPlay) {
        pendingAutoPlayPad = padIndex;
        setTimeout(() => {
            if (pendingAutoPlayPad === padIndex) {
                triggerPad(padIndex);
            }
        }, 350);
    }
    const msg = {
        cmd: 'loadSample',
        family: family,
        filename: filename,
        pad: padIndex
    };
    if (trimStart > 0.001 || trimEnd < 0.999) {
        msg.trimStart = trimStart;
        msg.trimEnd = trimEnd;
    }
    if (fadeInMs > 0) msg.fadeIn = fadeInMs;
    if (fadeOutMs > 0) msg.fadeOut = fadeOutMs;
    sendWebSocket(msg);
    // Invalidate waveform cache for this pad
    if (typeof SampleWaveform !== 'undefined') {
        SampleWaveform.clearCache(padIndex);
    }
}

function updatePadInfo(data) {
    const padIndex = data.pad;
    const filename = data.filename;
    const size = data.size;
    const sizeBytes = typeof size === 'number' ? size : 0;
    const sizeKB = (sizeBytes / 1024).toFixed(1);
    const format = data.format ? data.format.toUpperCase() : inferFormatFromName(filename);
    padSampleMetadata[padIndex] = {
        filename,
        sizeKB,
        format,
        quality: DEFAULT_SAMPLE_QUALITY
    };
    refreshPadSampleInfo(padIndex);
    showNotification(`Pad ${padIndex + 1}: ${filename} loaded`);

    if (pendingAutoPlayPad === padIndex) {
        pendingAutoPlayPad = null;
        setTimeout(() => triggerPad(padIndex), 80);
    }
}

// ============= FILTER PRESET SYSTEM =============

// Recommended demo pads for each filter type - 2 instruments that best showcase each filter
// [filterType]: [{pad, label, cutoff, resonance, gain}, ...]
const FILTER_DEMO_PADS = {
    1: [{pad:0, label:'BD', cutoff:800, q:3.0},     {pad:4, label:'CY', cutoff:600, q:3.0}],     // LOW PASS: kick gets muffled, cymbal loses sizzle
    2: [{pad:0, label:'BD', cutoff:800, q:3.0},     {pad:1, label:'SD', cutoff:500, q:3.0}],     // HIGH PASS: kick loses body, snare gets thin
    3: [{pad:1, label:'SD', cutoff:1500, q:5.0},    {pad:4, label:'CY', cutoff:2000, q:4.0}],    // BAND PASS: telephone effect
    4: [{pad:3, label:'OH', cutoff:1200, q:5.0},    {pad:4, label:'CY', cutoff:1200, q:5.0}],    // NOTCH: phaser-like on metals
    5: [{pad:0, label:'BD', cutoff:80, q:1.5, g:10},{pad:8, label:'LT', cutoff:200, q:1.0, g:8}],// BASS BOOST: sub on kick/tom
    6: [{pad:2, label:'CH', cutoff:3000, q:1.0, g:9},{pad:1, label:'SD', cutoff:6000, q:1.0, g:7}],// TREBLE BOOST: sizzle
    7: [{pad:0, label:'BD', cutoff:100, q:3.0, g:10},{pad:1, label:'SD', cutoff:2500, q:4.0, g:9}],// PEAK BOOST: punch
    8: [{pad:3, label:'OH', cutoff:300, q:5.0},     {pad:4, label:'CY', cutoff:1500, q:3.0}],    // PHASE: swirl on metals
    9: [{pad:0, label:'BD', cutoff:300, q:15.0},    {pad:1, label:'SD', cutoff:1000, q:12.0}]    // RESONANT: acid
};
window.FILTER_DEMO_PADS = FILTER_DEMO_PADS;

// Preview a filter on a specific pad: apply filter, trigger sound, auto-clear after delay
function previewFilterOnPad(filterType, padIndex, cutoff, resonance, gain) {
    const filterNames = ['OFF', 'LOW PASS', 'HIGH PASS', 'BAND PASS', 'NOTCH CUT', 
                        'BASS BOOST', 'TREBLE BOOST', 'PEAK BOOST', 'PHASE', 'RESONANT'];
    const filterIcons = ['🚫', '🔥', '✨', '📞', '🕳️', '🔊', '🌟', '⛰️', '🌀', '⚡'];

    // Apply filter to track
    sendWebSocket({
        cmd: 'setTrackFilter',
        track: padIndex,
        type: filterType,
        cutoff: cutoff,
        resonance: resonance || 2.0,
        gain: gain || 0
    });

    // Trigger pad after short delay for filter to be applied
    setTimeout(() => triggerPad(padIndex), 60);

    // Show toast
    if (window.showToast) {
        const freqStr = cutoff >= 1000 ? (cutoff/1000).toFixed(1)+'kHz' : cutoff+'Hz';
        window.showToast(
            `${filterIcons[filterType]} Preview: ${padNames[padIndex]} + ${filterNames[filterType]} @ ${freqStr}`,
            window.TOAST_TYPES?.SUCCESS || 'success',
            2000
        );
    }

    // Auto-clear filter after 2.5 seconds
    setTimeout(() => {
        sendWebSocket({
            cmd: 'clearTrackFilter',
            track: padIndex
        });
    }, 2500);
}
window.previewFilterOnPad = previewFilterOnPad;

// Apply filter preset from FX library
// Now accepts optional resonance and gain for more impactful presets
function applyFilterPreset(filterType, cutoffFreq, customResonance, customGain) {
    // Use custom values if provided, otherwise sensible defaults per filter type
    const defaultQ = {0:1, 1:2.0, 2:2.0, 3:3.0, 4:4.0, 5:1.0, 6:1.0, 7:3.0, 8:2.0, 9:10.0};
    const resonance = customResonance || defaultQ[filterType] || 1.5;
    const gain = customGain || (filterType >= 5 && filterType <= 7 ? 6.0 : 0.0);
    
    const filterNames = ['OFF', 'LOW PASS', 'HIGH PASS', 'BAND PASS', 'NOTCH CUT', 
                        'BASS BOOST', 'TREBLE BOOST', 'PEAK BOOST', 'PHASE', 'RESONANT'];
    const filterIcons = ['🚫', '🔥', '✨', '📞', '🕳️', '🔊', '🌟', '⛰️', '🌀', '⚡'];
    const filterColors = ['', '#ff6600', '#00ccff', '#ff00ff', '#888888', '#ff4444', '#44ff44', '#ffaa00', '#aa44ff', '#ff0044'];
    
    // Check if track is selected
    if (window.selectedTrack !== null && window.selectedTrack !== undefined) {
        const track = window.selectedTrack;
        const trackNames = padNames;
        
        sendWebSocket({
            cmd: 'setTrackFilter',
            track: track,
            type: filterType,
            cutoff: cutoffFreq,
            resonance: resonance,
            gain: gain
        });
        
        if (window.showToast) {
            const freqStr = cutoffFreq >= 1000 ? (cutoffFreq/1000).toFixed(1)+'kHz' : cutoffFreq+'Hz';
            window.showToast(
                `${filterIcons[filterType]} Track ${track + 1} (${trackNames[track]}): ${filterNames[filterType]} @ ${freqStr} Q:${resonance.toFixed(1)}`,
                window.TOAST_TYPES?.SUCCESS || 'success',
                2500
            );
        }
        
        // Update step filter dots
        trackFilterState[track] = filterType;
        updateTrackStepDots(track);

        return;
    }
    
    // Check if pad is selected
    if (window.selectedPad !== null && window.selectedPad !== undefined) {
        const pad = window.selectedPad;
        const names = ['BD', 'SD', 'CH', 'OH', 'CP', 'RS', 'CL', 'CY'];
        
        sendWebSocket({
            cmd: 'setPadFilter',
            pad: pad,
            type: filterType,
            cutoff: cutoffFreq,
            resonance: resonance,
            gain: gain
        });
        
        if (window.showToast) {
            const freqStr = cutoffFreq >= 1000 ? (cutoffFreq/1000).toFixed(1)+'kHz' : cutoffFreq+'Hz';
            window.showToast(
                `${filterIcons[filterType]} Pad ${pad + 1} (${names[pad]}): ${filterNames[filterType]} @ ${freqStr} Q:${resonance.toFixed(1)}`,
                window.TOAST_TYPES?.SUCCESS || 'success',
                2500
            );
        }
        
        // Create or update badge on pad
        const padElement = document.querySelector(`.pad[data-pad="${pad}"]`);
        if (padElement) {
            let badge = padElement.querySelector('.pad-filter-badge');
            if (filterType === 0) {
                if (badge) badge.remove();
                padElement.style.boxShadow = '';
            } else {
                if (!badge) {
                    badge = document.createElement('div');
                    badge.className = 'pad-filter-badge';
                    padElement.appendChild(badge);
                }
                badge.innerHTML = `${filterIcons[filterType]} <span class="pad-num">${filterNames[filterType]}</span>`;
                badge.style.borderColor = filterColors[filterType];
                // Add glow to pad when filter is active
                padElement.style.boxShadow = `0 0 12px ${filterColors[filterType]}60, inset 0 0 8px ${filterColors[filterType]}20`;
                // Pulse animation on apply
                padElement.classList.add('filter-applied-pulse');
                setTimeout(() => padElement.classList.remove('filter-applied-pulse'), 600);
            }
        }
        
        return;
    }
    
    // No selection - show info toast
    if (window.showToast) {
        window.showToast(
            '⚠️ Selecciona un track (click en nombre) o pad (click en pad LIVE) primero',
            window.TOAST_TYPES?.WARNING || 'warning',
            3000
        );
    }
}

// ============================================
// MIDI FUNCTIONS
// ============================================

// ============================================
// MIDI DASHBOARD - Professional Version
// ============================================

let midiTotalNotes = 0;
let midiCCMessages = 0;
let midiVelocitySum = 0;
let midiVelocityCount = 0;
let midiConnectTimestamp = null;
let midiUptimeInterval = null;
const midiMessagesQueue = [];
const MAX_MIDI_MESSAGES_DISPLAY = 50;

function handleMIDIDeviceMessage(data) {
    const badge = document.getElementById('midiConnectionBadge');
    const deviceCard = document.getElementById('midiDeviceCard');
    
    if (data.connected) {
        // Device connected
        badge.classList.add('connected');
        badge.querySelector('.badge-text').textContent = 'Conectado';
        
        deviceCard.classList.add('connected');
        document.getElementById('midiDeviceName').textContent = data.deviceName || 'USB MIDI Device';
        document.getElementById('midiVendorId').textContent = data.vendorId ? `0x${data.vendorId.toString(16).toUpperCase()}` : '—';
        document.getElementById('midiProductId').textContent = data.productId ? `0x${data.productId.toString(16).toUpperCase()}` : '—';
        
        // Start uptime counter
        midiConnectTimestamp = Date.now();
        if (midiUptimeInterval) clearInterval(midiUptimeInterval);
        midiUptimeInterval = setInterval(updateMidiUptime, 1000);
    } else {
        // Device disconnected
        badge.classList.remove('connected');
        badge.querySelector('.badge-text').textContent = 'Desconectado';
        
        deviceCard.classList.remove('connected');
        document.getElementById('midiDeviceName').textContent = 'Esperando conexión...';
        document.getElementById('midiVendorId').textContent = '—';
        document.getElementById('midiProductId').textContent = '—';
        
        // Stop uptime counter
        if (midiUptimeInterval) {
            clearInterval(midiUptimeInterval);
            midiUptimeInterval = null;
        }
        document.getElementById('midiUptime').textContent = '00:00';
    }
}

function handleMidiScanState(data) {
    const toggle = document.getElementById('midiScanToggle');
    if (toggle) toggle.checked = !!data.enabled;
}

function toggleMidiScan(enabled) {
    if (ws && ws.readyState === WebSocket.OPEN) {
        ws.send(JSON.stringify({ cmd: 'setMidiScan', enabled: enabled }));
    }
}

function updateMidiUptime() {
    if (!midiConnectTimestamp) return;
    
    const elapsed = Math.floor((Date.now() - midiConnectTimestamp) / 1000);
    const minutes = Math.floor(elapsed / 60);
    const seconds = elapsed % 60;
    document.getElementById('midiUptime').textContent = 
        `${String(minutes).padStart(2, '0')}:${String(seconds).padStart(2, '0')}`;
}

function handleMIDIMessage(data) {
    // Update stats
    if (data.messageType === 'noteOn') {
        midiTotalNotes++;
        midiVelocitySum += data.data2 || 0;
        midiVelocityCount++;
        
        document.getElementById('midiTotalNotes').textContent = midiTotalNotes;
        const avgVel = Math.round(midiVelocitySum / midiVelocityCount);
        document.getElementById('midiAvgVelocity').textContent = avgVel;
        
        // Animate velocity bar for the note
        animateNoteVelocity(data.data1, data.data2);
        
        // Highlight mapping item
        highlightMappingItem(data.data1);
    } else if (data.messageType === 'cc') {
        midiCCMessages++;
        document.getElementById('midiCCMessages').textContent = midiCCMessages;
    }
    
    // Add to message queue
    const messageEntry = {
        ...data,
        timestamp: Date.now()
    };
    
    midiMessagesQueue.unshift(messageEntry);
    if (midiMessagesQueue.length > MAX_MIDI_MESSAGES_DISPLAY) {
        midiMessagesQueue.pop();
    }
    
    // Update monitor display
    updateMIDIMonitorDisplay();
}

function animateNoteVelocity(note, velocity) {
    const item = document.querySelector(`.mapping-item[data-note="${note}"]`);
    if (!item) return;
    const velocityFill = item.querySelector('.velocity-fill');
    if (!velocityFill) return;
    const percent = Math.round((velocity / 127) * 100);
    velocityFill.style.width = `${percent}%`;
    setTimeout(() => { velocityFill.style.width = '0%'; }, 500);
}

function highlightMappingItem(note) {
    const item = document.querySelector(`.mapping-item[data-note="${note}"]`);
    if (!item) return;
    item.classList.add('active');
    setTimeout(() => { item.classList.remove('active'); }, 300);
}

function updateMIDIMonitorDisplay() {
    const monitor = document.getElementById('midiMonitor');
    if (!monitor) return;
    
    // Remove placeholder if exists (only once)
    const placeholder = monitor.querySelector('.monitor-placeholder');
    if (placeholder) {
        placeholder.remove();
    }
    
    // OPTIMIZACIÓN: Solo agregar el mensaje más reciente en lugar de re-renderizar todo
    // Esto evita el parpadeo y duplicación
    if (midiMessagesQueue.length > 0) {
        const latestMsg = midiMessagesQueue[0];
        
        // Aplicar filtro
        if (midiMonitorFilter !== 'all' && latestMsg.messageType !== midiMonitorFilter) return;
        
        const entry = createMIDIMessageEntry(latestMsg);
        
        // Insertar al inicio (más nuevo arriba)
        monitor.insertBefore(entry, monitor.firstChild);
        
        // Limitar el número de mensajes visibles (eliminar los más antiguos)
        while (monitor.children.length > MAX_MIDI_MESSAGES_DISPLAY) {
            monitor.removeChild(monitor.lastChild);
        }
    }
}

function createMIDIMessageEntry(msg) {
    const entry = document.createElement('div');
    entry.className = `midi-message-entry ${getMIDIMessageClass(msg.messageType)}`;
    
    // Header
    const header = document.createElement('div');
    header.className = 'message-header';
    
    const type = document.createElement('div');
    type.className = `message-type ${getMIDIMessageClass(msg.messageType)}`;
    type.innerHTML = `
        <span class="message-type-icon">${getMIDIIcon(msg.messageType)}</span>
        <span>${getMIDITypeName(msg.messageType)}</span>
    `;
    
    const time = document.createElement('div');
    time.className = 'message-time';
    const elapsed = Date.now() - msg.timestamp;
    time.textContent = elapsed < 1000 ? 'ahora' : `${Math.floor(elapsed / 1000)}s ago`;
    
    header.appendChild(type);
    header.appendChild(time);
    
    // Details
    const details = document.createElement('div');
    details.className = 'message-details';
    details.innerHTML = getMIDIDetailsHTML(msg);
    
    entry.appendChild(header);
    entry.appendChild(details);
    
    return entry;
}

function getMIDIMessageClass(type) {
    const classes = {
        'noteOn': 'note-on',
        'noteOff': 'note-off',
        'cc': 'cc',
        'pitchBend': 'pitchbend',
        'program': 'program'
    };
    return classes[type] || 'other';
}

function getMIDIIcon(type) {
    const icons = {
        'noteOn': '🎹',
        'noteOff': '⬜',
        'cc': '🎛️',
        'pitchBend': '🎚️',
        'program': '📋',
        'aftertouch': '👆'
    };
    return icons[type] || '📨';
}

function getMIDITypeName(type) {
    const names = {
        'noteOn': 'Note On',
        'noteOff': 'Note Off',
        'cc': 'Control Change',
        'pitchBend': 'Pitch Bend',
        'program': 'Program Change',
        'aftertouch': 'Aftertouch'
    };
    return names[type] || type;
}

function getMIDIDetailsHTML(msg) {
    let html = `<span><span class="label">Canal:</span> <span class="value">${msg.channel}</span></span>`;
    
    if (msg.messageType === 'noteOn' || msg.messageType === 'noteOff') {
        const noteName = getNoteNameFromNumber(msg.data1);
        html += `<span><span class="label">Nota:</span> <span class="value">${msg.data1} (${noteName})</span></span>`;
        html += `<span><span class="label">Velocity:</span> <span class="value">${msg.data2}</span></span>`;
    } else if (msg.messageType === 'cc') {
        html += `<span><span class="label">CC:</span> <span class="value">${msg.data1}</span></span>`;
        html += `<span><span class="label">Valor:</span> <span class="value">${msg.data2}</span></span>`;
    } else if (msg.messageType === 'pitchBend') {
        const bendValue = (msg.data1 | (msg.data2 << 7)) - 8192;
        html += `<span><span class="label">Bend:</span> <span class="value">${bendValue}</span></span>`;
    } else {
        html += `<span><span class="label">Data1:</span> <span class="value">${msg.data1}</span></span>`;
        if (msg.data2 !== undefined) {
            html += `<span><span class="label">Data2:</span> <span class="value">${msg.data2}</span></span>`;
        }
    }
    
    return html;
}

function getNoteNameFromNumber(noteNumber) {
    const notes = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'];
    const octave = Math.floor(noteNumber / 12) - 1;
    const noteName = notes[noteNumber % 12];
    return `${noteName}${octave}`;
}

function formatMIDIData(msg) {
    switch(msg.messageType) {
        case 'noteOn':
        case 'noteOff':
            return `Ch${msg.channel} Note:${msg.data1} Vel:${msg.data2}`;
        case 'cc':
            return `Ch${msg.channel} CC:${msg.data1} Val:${msg.data2}`;
        case 'program':
            return `Ch${msg.channel} Program:${msg.data1}`;
        case 'pitchBend':
            const bend = (msg.data2 << 7) | msg.data1;
            return `Ch${msg.channel} Bend:${bend}`;
        default:
            return `Ch${msg.channel} D1:${msg.data1} D2:${msg.data2}`;
    }
}

// Update messages per second periodically
setInterval(() => {
    // This would need backend support to send real-time stats
    // For now we can estimate based on message timestamps
}, 1000);

// ============================================
// SAMPLE UPLOAD FUNCTIONS
// ============================================

function showUploadDialog(padIndex) {
    // Crear input file oculto
    const input = document.createElement('input');
    input.type = 'file';
    input.accept = '.wav';
    input.style.display = 'none';
    
    input.addEventListener('change', (e) => {
        const file = e.target.files[0];
        if (!file) return;
        
        // Validar extensión
        if (!file.name.toLowerCase().endsWith('.wav')) {
            if (window.showToast) {
                window.showToast('❌ Solo se permiten archivos WAV', window.TOAST_TYPES.ERROR, 3000);
            }
            return;
        }
        
        // Validar tamaño (max 2MB)
        if (file.size > 2 * 1024 * 1024) {
            if (window.showToast) {
                window.showToast('❌ Archivo demasiado grande (max 2MB)', window.TOAST_TYPES.ERROR, 3000);
            }
            return;
        }
        
        uploadSample(padIndex, file);
    });
    
    document.body.appendChild(input);
    input.click();
    setTimeout(() => input.remove(), 1000);
}

let currentUploadPad = -1;

function uploadSample(padIndex, file) {
    currentUploadPad = padIndex;
    const padName = padNames[padIndex];
    
    // Mostrar toast de inicio
    if (window.showToast) {
        window.showToast(`📤 Subiendo ${file.name} a ${padName}...`, window.TOAST_TYPES.INFO, 2000);
    }
    
    // Deshabilitar botón de upload durante el proceso
    const btn = document.querySelector(`.pad-upload-btn[data-pad="${padIndex}"]`);
    if (btn) {
        btn.disabled = true;
        btn.textContent = '⏳';
    }
    
    // Crear FormData (sin el pad, irá en la URL)
    const formData = new FormData();
    formData.append('file', file);
    
    // Enviar via fetch con pad como query parameter
    fetch(`/api/upload?pad=${padIndex}`, {
        method: 'POST',
        body: formData
    })
    .then(response => response.json())
    .then(data => {
    })
    .catch(error => {
        console.error('[Upload] Error:', error);
        if (window.showToast) {
            window.showToast(`❌ Error al subir archivo: ${error.message}`, window.TOAST_TYPES.ERROR, 4000);
        }
        
        // Re-habilitar botón
        if (btn) {
            btn.disabled = false;
            btn.textContent = '📤';
        }
        currentUploadPad = -1;
    });
}

function handleUploadProgress(data) {
    if (data.pad !== currentUploadPad) return;
    
    const btn = document.querySelector(`.pad-upload-btn[data-pad="${data.pad}"]`);
    if (btn) {
        btn.textContent = `${data.percent}%`;
    }
}

function handleUploadComplete(data) {
    const btn = document.querySelector(`.pad-upload-btn[data-pad="${data.pad}"]`);
    if (btn) {
        btn.disabled = false;
        btn.textContent = '📤';
    }
    
    if (data.success) {
        if (window.showToast) {
            const padName = padNames[data.pad];
            window.showToast(`✅ ${padName}: ${data.message}`, window.TOAST_TYPES.SUCCESS, 3000);
        }
        
        // Actualizar info del pad
        refreshPadSampleInfo(data.pad);
        
        // Animación de éxito en el pad
        const pad = document.querySelector(`.pad[data-pad="${data.pad}"]`);
        if (pad) {
            pad.style.animation = 'padPulseSuccess 0.5s ease-out';
            setTimeout(() => {
                pad.style.animation = '';
            }, 500);
        }
    } else {
        if (window.showToast) {
            window.showToast(`❌ Error: ${data.message}`, window.TOAST_TYPES.ERROR, 4000);
        }
    }
    
    currentUploadPad = -1;
}

// ============================================
// MIDI MAPPING EDITOR
// ============================================

let isEditingMapping = false;
let originalMappings = {};
let midiMonitorFilter = 'all';

// Presets de mapeo MIDI para diferentes controladores
const MIDI_MAPPING_PRESETS = {
    gm: [
        {pad:0, note:36}, {pad:1, note:38}, {pad:2, note:42}, {pad:3, note:46},
        {pad:4, note:49}, {pad:5, note:39}, {pad:6, note:37}, {pad:7, note:56},
        {pad:8, note:41}, {pad:9, note:47}, {pad:10, note:50}, {pad:11, note:70},
        {pad:12, note:75}, {pad:13, note:62}, {pad:14, note:63}, {pad:15, note:64}
    ],
    roland: [
        // Roland TR-8S / TD pads
        {pad:0, note:36}, {pad:1, note:38}, {pad:2, note:42}, {pad:3, note:46},
        {pad:4, note:49}, {pad:5, note:39}, {pad:6, note:37}, {pad:7, note:56},
        {pad:8, note:43}, {pad:9, note:47}, {pad:10, note:48}, {pad:11, note:70},
        {pad:12, note:75}, {pad:13, note:62}, {pad:14, note:63}, {pad:15, note:64}
    ],
    mpc: [
        // Akai MPC default pad layout (A01-A16 = 60-75)
        {pad:0, note:60}, {pad:1, note:61}, {pad:2, note:62}, {pad:3, note:63},
        {pad:4, note:64}, {pad:5, note:65}, {pad:6, note:66}, {pad:7, note:67},
        {pad:8, note:68}, {pad:9, note:69}, {pad:10, note:70}, {pad:11, note:71},
        {pad:12, note:72}, {pad:13, note:73}, {pad:14, note:74}, {pad:15, note:75}
    ]
};

function setMidiMonitorFilter(filter) {
    midiMonitorFilter = filter;
}

async function loadMIDIMapping() {
    try {
        const response = await fetch('/api/midi/mapping');
        const data = await response.json();
        
        if (data.mappings) {
            // Solo actualizar los pads 0-15 (mapeos principales, no alias)
            const primaryMappings = data.mappings.filter(m => m.pad >= 0 && m.pad <= 15);
            // Crear mapa pad→note para búsqueda rápida
            const padNoteMap = {};
            // En caso de múltiples notas por pad, usar la primera
            primaryMappings.forEach(m => {
                if (padNoteMap[m.pad] === undefined) padNoteMap[m.pad] = m.note;
            });
            
            for (let pad = 0; pad <= 15; pad++) {
                const item = document.querySelector(`.mapping-item[data-pad="${pad}"]`);
                if (!item) continue;
                const note = padNoteMap[pad];
                if (note === undefined) continue;
                
                const input   = item.querySelector('.note-input');
                const valueEl = item.querySelector('.note-value');
                const nameEl  = item.querySelector('.note-name');
                try {
                    if (input)   { input.value = note; item.dataset.note = note; }
                    if (valueEl) valueEl.textContent = note;
                    if (nameEl)  nameEl.textContent  = getNoteNameFromNumber(note);
                } catch(ex) { /* elemento no visible aún */ }
            }
        }
    } catch (error) {
        console.error('[MIDI Mapping] Error loading:', error);
    }
}

// Bloquear atajos globales cuando el slider de mapping tiene el foco
function stopKeyPropForSlider(e) {
    e.stopPropagation();
}

function toggleMappingEdit() {
    isEditingMapping = !isEditingMapping;
    
    const editBtn   = document.getElementById('editMappingBtn');
    const resetBtn  = document.getElementById('resetMappingBtn');
    const saveBtn   = document.getElementById('saveMappingBtn');
    const cancelBtn = document.getElementById('cancelMappingBtn');
    const presets   = document.getElementById('mappingPresets');
    const mappingGrid = document.getElementById('mappingGrid');
    const inputs    = document.querySelectorAll('.note-input');
    
    if (isEditingMapping) {
        editBtn.style.display  = 'none';
        resetBtn.style.display = 'inline-block';
        saveBtn.style.display  = 'inline-block';
        cancelBtn.style.display = 'inline-block';
        if (presets) presets.style.display = 'flex';
        mappingGrid.classList.add('editing');
        
        // Guardar valores originales y habilitar sliders
        inputs.forEach(input => {
            const item = input.closest('.mapping-item');
            originalMappings[item.dataset.pad] = input.value;
            input.disabled = false;
            input.classList.add('editing');
            input.addEventListener('input', onNoteInputChange);
            // Evitar que el teclado global intercepte las flechas del slider
            input.addEventListener('keydown', stopKeyPropForSlider);
        });
        
        if (window.showToast) window.showToast('✏️ Modo edición — arrastra los sliders y pulsa Guardar', window.TOAST_TYPES?.INFO, 3000);
    } else {
        // Cancelar → restaurar
        inputs.forEach(input => {
            const item = input.closest('.mapping-item');
            input.value = originalMappings[item.dataset.pad] ?? input.value;
            item.dataset.note = input.value;
            input.disabled = true;
            input.classList.remove('editing');
            input.removeEventListener('input', onNoteInputChange);
            input.removeEventListener('keydown', stopKeyPropForSlider);
            const val = parseInt(input.value);
            const valueEl = item.querySelector('.note-value');
            const nameEl  = item.querySelector('.note-name');
            if (valueEl) valueEl.textContent = val;
            if (nameEl)  nameEl.textContent  = getNoteNameFromNumber(val);
        });
        editBtn.style.display   = 'inline-block';
        resetBtn.style.display  = 'none';
        saveBtn.style.display   = 'none';
        cancelBtn.style.display = 'none';
        if (presets) presets.style.display = 'none';
        mappingGrid.classList.remove('editing');
        originalMappings = {};
    }
}

function cancelMappingEdit() {
    if (isEditingMapping) toggleMappingEdit(); // restaura y sale del modo edición
}

function onNoteInputChange(e) {
    const input = e.target;
    const val = parseInt(input.value);
    const item = input.closest('.mapping-item');
    const valueEl = item.querySelector('.note-value');
    const nameEl  = item.querySelector('.note-name');
    if (valueEl) valueEl.textContent = val;
    if (nameEl)  nameEl.textContent  = getNoteNameFromNumber(val);
}

function applyMappingPreset(name) {
    const preset = MIDI_MAPPING_PRESETS[name];
    if (!preset) return;
    
    preset.forEach(({pad, note}) => {
        const item = document.querySelector(`.mapping-item[data-pad="${pad}"]`);
        if (!item) return;
        const input   = item.querySelector('.note-input');
        const valueEl = item.querySelector('.note-value');
        const nameEl  = item.querySelector('.note-name');
        if (input)   { input.value = note; input.classList.remove('error'); }
        if (valueEl) valueEl.textContent = note;
        if (nameEl)  nameEl.textContent  = getNoteNameFromNumber(note);
    });
    
    if (window.showToast) window.showToast(`✅ Preset "${name.toUpperCase()}" aplicado — pulsa Guardar para confirmar`, window.TOAST_TYPES?.INFO, 3000);
}

async function saveMIDIMapping() {
    const inputs = document.querySelectorAll('.note-input');
    const mappings = [];
    let hasErrors = false;
    
    inputs.forEach(input => {
        const item = input.closest('.mapping-item');
        const pad  = parseInt(item.dataset.pad);
        const note = parseInt(input.value);
        
        if (isNaN(note) || note < 0 || note > 127) {
            input.classList.add('error');
            hasErrors = true;
            return;
        }
        input.classList.remove('error');
        mappings.push({ note, pad });
    });
    
    if (hasErrors) {
        if (window.showToast) window.showToast('❌ Notas inválidas (0-127)', window.TOAST_TYPES?.ERROR, 3000);
        return;
    }
    
    try {
        for (const mapping of mappings) {
            const resp = await fetch('/api/midi/mapping', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify(mapping)
            });
            if (!resp.ok) throw new Error(`Pad ${mapping.pad}`);
        }
        
        // Actualizar dataset y note-names
        inputs.forEach(input => {
            const item    = input.closest('.mapping-item');
            const note    = parseInt(input.value);
            const valueEl = item.querySelector('.note-value');
            const nameEl  = item.querySelector('.note-name');
            item.dataset.note = note;
            if (valueEl) valueEl.textContent = note;
            if (nameEl)  nameEl.textContent  = getNoteNameFromNumber(note);
        });
        
        // Salir modo edición
        isEditingMapping = true;  // forzar a que toggleMappingEdit lo desactive
        cancelMappingEdit();
        const editBtn = document.getElementById('editMappingBtn');
        if (editBtn) editBtn.style.display = 'inline-block';
        
        // Recargar desde ESP32 para confirmar valores guardados
        await loadMIDIMapping();
        
        if (window.showToast) window.showToast('✅ Mapeo MIDI guardado', window.TOAST_TYPES?.SUCCESS, 3000);
    } catch (error) {
        console.error('[MIDI Mapping] Error saving:', error);
        if (window.showToast) window.showToast('❌ Error al guardar mapeo', window.TOAST_TYPES?.ERROR, 3000);
    }
}

async function resetMIDIMapping() {
    if (!confirm('¿Resetear el mapeo MIDI al mapa GM estándar (16 pads)?')) return;
    
    try {
        const resp = await fetch('/api/midi/mapping', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ reset: true })
        });
        
        if (resp.ok) {
            await loadMIDIMapping();
            if (window.showToast) window.showToast('🔄 Mapeo reseteado a GM (16 pads)', window.TOAST_TYPES?.SUCCESS, 3000);
        } else {
            throw new Error('Reset failed');
        }
    } catch (error) {
        console.error('[MIDI Mapping] Error resetting:', error);
        if (window.showToast) window.showToast('❌ Error al resetear mapeo', window.TOAST_TYPES?.ERROR, 3000);
    }
}

// Cargar mapeo al abrir la tab MIDI
document.addEventListener('DOMContentLoaded', () => {
    const midiTab = document.querySelector('[data-tab="midi"]');
    if (midiTab) {
        midiTab.addEventListener('click', () => {
            setTimeout(loadMIDIMapping, 100);
        });
    }
    if (window.location.hash === '#midi' || document.getElementById('tab-midi')?.classList.contains('active')) {
        setTimeout(loadMIDIMapping, 500);
    }
});

// Export to window
window.applyFilterPreset = applyFilterPreset;

// ============= FX Sub-Tabs System =============
function initFxSubtabs() {
    const subtabBtns = document.querySelectorAll('.fx-subtab-btn');
    subtabBtns.forEach(btn => {
        btn.addEventListener('click', () => {
            const tabId = btn.getAttribute('data-fxtab');
            // Update active button
            subtabBtns.forEach(b => b.classList.remove('active'));
            btn.classList.add('active');
            // Update active content
            document.querySelectorAll('.fx-subtab-content').forEach(content => {
                if (content.id === `fxtab-${tabId}`) {
                    content.classList.add('active');
                } else {
                    content.classList.remove('active');
                }
            });
        });
    });
    // Init Track FX system
    initTrackFx();
}

// ============= TRACK FX System (per-track combinable effects) =============
// State: per-track FX { reverse: bool, pitch: float, stutter: bool, stutterMs: int }
let trackFxEffects = new Array(16).fill(null).map(() => ({
    reverse: false,
    pitch: 1.0,
    stutter: false,
    stutterMs: 100
}));
let selectedFxTrack = 0;

function initTrackFx() {
    // Track selector buttons
    const trackBtns = document.querySelectorAll('.track-fx-btn');
    trackBtns.forEach(btn => {
        btn.addEventListener('click', () => {
            const track = parseInt(btn.dataset.track);
            selectFxTrack(track);
        });
    });
    // Initial render
    updateTrackFxUI();
    updateTrackFxStatusGrid();
}

function selectFxTrack(trackIndex) {
    selectedFxTrack = trackIndex;
    window.lastSelectedTrack = trackIndex;
    
    // Update selector buttons
    document.querySelectorAll('.track-fx-btn').forEach(btn => {
        btn.classList.toggle('active', parseInt(btn.dataset.track) === trackIndex);
    });
    
    // Update toggle states to reflect selected track's FX
    updateTrackFxUI();
}

function updateTrackFxUI() {
    const fx = trackFxEffects[selectedFxTrack];
    if (!fx) return;
    
    // Reverse toggle
    const reverseToggle = document.getElementById('fxReverseToggle');
    if (reverseToggle) reverseToggle.checked = fx.reverse;
    document.getElementById('fxCardReverse')?.classList.toggle('fx-active', fx.reverse);
    
    // Pitch toggle & slider
    const pitchToggle = document.getElementById('fxPitchToggle');
    const pitchActive = fx.pitch !== 1.0;
    if (pitchToggle) pitchToggle.checked = pitchActive;
    document.getElementById('fxCardPitch')?.classList.toggle('fx-active', pitchActive);
    const pitchSlider = document.getElementById('fxPitchSlider');
    if (pitchSlider) pitchSlider.value = Math.round(fx.pitch * 100);
    const pitchValue = document.getElementById('fxPitchValue');
    if (pitchValue) pitchValue.textContent = fx.pitch.toFixed(2);
    // Update pitch preset buttons
    document.querySelectorAll('#fxCardPitch .pitch-preset-btn').forEach(btn => {
        const val = parseFloat(btn.getAttribute('onclick')?.match(/setTrackFxPitch\(([\d.]+)\)/)?.[1] || '0');
        btn.classList.toggle('active', Math.abs(val - fx.pitch) < 0.01);
    });
    
    // Stutter toggle & slider
    const stutterToggle = document.getElementById('fxStutterToggle');
    if (stutterToggle) stutterToggle.checked = fx.stutter;
    document.getElementById('fxCardStutter')?.classList.toggle('fx-active', fx.stutter);
    const stutterSlider = document.getElementById('fxStutterSlider');
    if (stutterSlider) stutterSlider.value = fx.stutterMs;
    const stutterValue = document.getElementById('fxStutterValue');
    if (stutterValue) stutterValue.textContent = fx.stutterMs;
    // Update stutter preset buttons
    document.querySelectorAll('#fxCardStutter .pitch-preset-btn').forEach(btn => {
        const val = parseInt(btn.getAttribute('onclick')?.match(/setTrackFxStutter\((\d+)\)/)?.[1] || '0');
        btn.classList.toggle('active', val === fx.stutterMs && fx.stutter);
    });
    

}

function toggleTrackFx(fxType, active) {
    const track = selectedFxTrack;
    const fx = trackFxEffects[track];
    
    switch (fxType) {
        case 'reverse':
            fx.reverse = active;
            sendWebSocket({ cmd: 'setReverse', track: track, value: active });
            document.getElementById('fxCardReverse')?.classList.toggle('fx-active', active);
            if (window.showToast) window.showToast(`${active ? '⏪ REVERSE ON' : '▶️ REVERSE OFF'} → ${padNames[track]}`, window.TOAST_TYPES?.SUCCESS, 2000);
            break;
            
        case 'pitch':
            if (!active) {
                fx.pitch = 1.0;
                sendWebSocket({ cmd: 'setPitchShift', track: track, value: 1.0 });
                document.getElementById('fxCardPitch')?.classList.remove('fx-active');
                if (window.showToast) window.showToast(`▶️ PITCH NORMAL → ${padNames[track]}`, window.TOAST_TYPES?.SUCCESS, 2000);
            } else {
                // When toggling on, apply current pitch (default to 0.5 if was 1.0)
                if (fx.pitch === 1.0) fx.pitch = 0.5;
                sendWebSocket({ cmd: 'setPitchShift', track: track, value: fx.pitch });
                document.getElementById('fxCardPitch')?.classList.add('fx-active');
                if (window.showToast) window.showToast(`🎵 PITCH ${fx.pitch.toFixed(2)}× → ${padNames[track]}`, window.TOAST_TYPES?.SUCCESS, 2000);
            }
            updateTrackFxUI();
            break;
            
        case 'stutter':
            fx.stutter = active;
            sendWebSocket({ cmd: 'setStutter', track: track, value: active, interval: fx.stutterMs });
            document.getElementById('fxCardStutter')?.classList.toggle('fx-active', active);
            if (window.showToast) window.showToast(`${active ? '🔁 STUTTER ON ' + fx.stutterMs + 'ms' : '🔁 STUTTER OFF'} → ${padNames[track]}`, window.TOAST_TYPES?.SUCCESS, 2000);
            break;
            
    }
    
    updateTrackFxStatusGrid();
    updateTrackFxBtnIndicators();
}

function setTrackFxPitch(value) {
    const track = selectedFxTrack;
    const fx = trackFxEffects[track];
    fx.pitch = value;
    
    // Auto-enable pitch toggle
    const pitchToggle = document.getElementById('fxPitchToggle');
    if (value !== 1.0) {
        if (pitchToggle) pitchToggle.checked = true;
        document.getElementById('fxCardPitch')?.classList.add('fx-active');
    } else {
        if (pitchToggle) pitchToggle.checked = false;
        document.getElementById('fxCardPitch')?.classList.remove('fx-active');
    }
    
    sendWebSocket({ cmd: 'setPitchShift', track: track, value: value });
    
    // Update UI
    const pitchSlider = document.getElementById('fxPitchSlider');
    if (pitchSlider) pitchSlider.value = Math.round(value * 100);
    const pitchValue = document.getElementById('fxPitchValue');
    if (pitchValue) pitchValue.textContent = value.toFixed(2);
    
    // Update preset buttons
    document.querySelectorAll('#fxCardPitch .pitch-preset-btn').forEach(btn => {
        const val = parseFloat(btn.getAttribute('onclick')?.match(/setTrackFxPitch\(([\d.]+)\)/)?.[1] || '0');
        btn.classList.toggle('active', Math.abs(val - value) < 0.01);
    });
    
    updateTrackFxStatusGrid();
    updateTrackFxBtnIndicators();
}

function setTrackFxStutter(intervalMs) {
    const track = selectedFxTrack;
    const fx = trackFxEffects[track];
    fx.stutterMs = intervalMs;
    
    // Auto-enable stutter toggle
    const stutterToggle = document.getElementById('fxStutterToggle');
    if (stutterToggle) stutterToggle.checked = true;
    fx.stutter = true;
    document.getElementById('fxCardStutter')?.classList.add('fx-active');
    
    sendWebSocket({ cmd: 'setStutter', track: track, value: true, interval: intervalMs });
    
    // Update UI
    const stutterSlider = document.getElementById('fxStutterSlider');
    if (stutterSlider) stutterSlider.value = intervalMs;
    const stutterValue = document.getElementById('fxStutterValue');
    if (stutterValue) stutterValue.textContent = intervalMs;
    
    // Update preset buttons
    document.querySelectorAll('#fxCardStutter .pitch-preset-btn').forEach(btn => {
        const val = parseInt(btn.getAttribute('onclick')?.match(/setTrackFxStutter\((\d+)\)/)?.[1] || '0');
        btn.classList.toggle('active', val === intervalMs);
    });
    
    updateTrackFxStatusGrid();
    updateTrackFxBtnIndicators();
}

// Update the track selector buttons to show which have FX active
function updateTrackFxBtnIndicators() {
    document.querySelectorAll('.track-fx-btn').forEach(btn => {
        const track = parseInt(btn.dataset.track);
        const fx = trackFxEffects[track];
        const hasFx = fx && (fx.reverse || fx.pitch !== 1.0 || fx.stutter);
        btn.classList.toggle('has-fx', hasFx);
    });
}

// Update the status grid showing all 16 tracks with their active FX
function updateTrackFxStatusGrid() {
    const container = document.getElementById('trackFxStatus');
    if (!container) return;
    
    // Build DOM once, then update in-place
    if (!container.children.length || container.children.length !== 16) {
        let html = '';
        for (let i = 0; i < 16; i++) {
            html += `<div class="track-fx-status-item" data-track="${i}">
                <span class="status-name">${padNames[i]}</span>
                <div class="status-fx">
                    <span class="fx-dot" data-fx="reverse" title="Reverse"></span>
                    <span class="fx-dot" data-fx="pitch" title="Pitch"></span>
                    <span class="fx-dot" data-fx="stutter" title="Stutter"></span>
                </div>
            </div>`;
        }
        container.innerHTML = html;
    }
    
    // Update only changed elements
    for (let i = 0; i < 16; i++) {
        const item = container.children[i];
        if (!item) continue;
        const fx = trackFxEffects[i];
        const hasAny = fx.reverse || fx.pitch !== 1.0 || fx.stutter;
        const bg = hasAny ? 'rgba(168,85,247,0.1)' : '';
        if (item.style.background !== bg) item.style.background = bg;
        
        const dots = item.querySelectorAll('.fx-dot');
        if (dots[0]) {
            dots[0].classList.toggle('reverse-on', fx.reverse);
            dots[0].title = 'Reverse';
        }
        if (dots[1]) {
            dots[1].classList.toggle('pitch-on', fx.pitch !== 1.0);
            dots[1].title = `Pitch ${fx.pitch.toFixed(2)}×`;
        }
        if (dots[2]) {
            dots[2].classList.toggle('stutter-on', fx.stutter);
            dots[2].title = `Stutter ${fx.stutterMs}ms`;
        }
    }
}

// Legacy functions - now use trackFxEffects system
function applyReverseFilter() {
    const context = getSelectedFilterContext();
    if (!context) {
        if (window.showToast) window.showToast('⚠️ Selecciona primero un track o pad', window.TOAST_TYPES?.WARNING, 3000);
        return;
    }
    if (context.type === 'track') {
        trackFxEffects[context.index].reverse = true;
        updateTrackFxUI();
        updateTrackFxStatusGrid();
        updateTrackFxBtnIndicators();
    }
    sendWebSocket({ cmd: 'setReverse', [context.type]: context.index, value: true });
    if (window.showToast) window.showToast(`⏪ REVERSE ON → ${context.type === 'track' ? padNames[context.index] : 'Pad ' + context.index}`, window.TOAST_TYPES?.SUCCESS, 2000);
}

function removeReverseFilter() {
    const context = getSelectedFilterContext();
    if (!context) {
        if (window.showToast) window.showToast('⚠️ Selecciona primero un track o pad', window.TOAST_TYPES?.WARNING, 3000);
        return;
    }
    if (context.type === 'track') {
        trackFxEffects[context.index].reverse = false;
        updateTrackFxUI();
        updateTrackFxStatusGrid();
        updateTrackFxBtnIndicators();
    }
    sendWebSocket({ cmd: 'setReverse', [context.type]: context.index, value: false });
    if (window.showToast) window.showToast(`▶️ Normal → ${context.type === 'track' ? padNames[context.index] : 'Pad ' + context.index}`, window.TOAST_TYPES?.SUCCESS, 2000);
}

// ============= HALF-SPEED / DOUBLE-SPEED Filter =============
function applyHalfSpeedFilter() {
    const context = getSelectedFilterContext();
    if (!context) {
        if (window.showToast) window.showToast('⚠️ Selecciona primero un track o pad', window.TOAST_TYPES?.WARNING, 3000);
        return;
    }
    if (context.type === 'track') {
        trackFxEffects[context.index].pitch = 0.5;
        updateTrackFxUI();
        updateTrackFxStatusGrid();
        updateTrackFxBtnIndicators();
    }
    sendWebSocket({ cmd: 'setPitchShift', [context.type]: context.index, value: 0.5 });
    if (window.showToast) window.showToast(`🐢 HALF-SPEED → ${context.type === 'track' ? padNames[context.index] : 'Pad ' + context.index}`, window.TOAST_TYPES?.SUCCESS, 2000);
}

function applyDoubleSpeedFilter() {
    const context = getSelectedFilterContext();
    if (!context) {
        if (window.showToast) window.showToast('⚠️ Selecciona primero un track o pad', window.TOAST_TYPES?.WARNING, 3000);
        return;
    }
    if (context.type === 'track') {
        trackFxEffects[context.index].pitch = 2.0;
        updateTrackFxUI();
        updateTrackFxStatusGrid();
        updateTrackFxBtnIndicators();
    }
    sendWebSocket({ cmd: 'setPitchShift', [context.type]: context.index, value: 2.0 });
    if (window.showToast) window.showToast(`🐇 DOUBLE-SPEED → ${context.type === 'track' ? padNames[context.index] : 'Pad ' + context.index}`, window.TOAST_TYPES?.SUCCESS, 2000);
}

function applyNormalSpeedFilter() {
    const context = getSelectedFilterContext();
    if (!context) {
        if (window.showToast) window.showToast('⚠️ Selecciona primero un track o pad', window.TOAST_TYPES?.WARNING, 3000);
        return;
    }
    if (context.type === 'track') {
        trackFxEffects[context.index].pitch = 1.0;
        updateTrackFxUI();
        updateTrackFxStatusGrid();
        updateTrackFxBtnIndicators();
    }
    sendWebSocket({ cmd: 'setPitchShift', [context.type]: context.index, value: 1.0 });
    if (window.showToast) window.showToast(`▶️ Normal Speed → ${context.type === 'track' ? padNames[context.index] : 'Pad ' + context.index}`, window.TOAST_TYPES?.SUCCESS, 2000);
}

// ============= STUTTER Filter =============
function applyStutterFilter(intervalMs) {
    const context = getSelectedFilterContext();
    if (!context) {
        if (window.showToast) window.showToast('⚠️ Selecciona primero un track o pad', window.TOAST_TYPES?.WARNING, 3000);
        return;
    }
    if (context.type === 'track') {
        trackFxEffects[context.index].stutter = true;
        trackFxEffects[context.index].stutterMs = intervalMs;
        updateTrackFxUI();
        updateTrackFxStatusGrid();
        updateTrackFxBtnIndicators();
    }
    sendWebSocket({ cmd: 'setStutter', [context.type]: context.index, interval: intervalMs, value: true });
    if (window.showToast) window.showToast(`🔁 STUTTER ${intervalMs}ms → ${context.type === 'track' ? padNames[context.index] : 'Pad ' + context.index}`, window.TOAST_TYPES?.SUCCESS, 2000);
}

// Helper to get current filter target context (selected track or pad)
function getSelectedFilterContext() {
    // Check if there's a selected track in the sequencer
    const selectedTrack = document.querySelector('.seq-track-label.selected, .seq-label.selected');
    if (selectedTrack) {
        const trackIdx = parseInt(selectedTrack.dataset.track || selectedTrack.dataset.trackIndex);
        if (!isNaN(trackIdx)) return { type: 'track', index: trackIdx };
    }
    // Check if there's a selected pad
    const selectedPad = document.querySelector('.pad.selected, .pad-active-selected');
    if (selectedPad) {
        const padIdx = parseInt(selectedPad.dataset.pad || selectedPad.dataset.padIndex);
        if (!isNaN(padIdx)) return { type: 'pad', index: padIdx };
    }
    // Fallback: use last triggered pad/track if available
    if (typeof window.lastSelectedTrack === 'number') return { type: 'track', index: window.lastSelectedTrack };
    if (typeof window.lastSelectedPad === 'number') return { type: 'pad', index: window.lastSelectedPad };
    return null;
}

window.applyReverseFilter = applyReverseFilter;
window.removeReverseFilter = removeReverseFilter;
window.applyHalfSpeedFilter = applyHalfSpeedFilter;
window.applyDoubleSpeedFilter = applyDoubleSpeedFilter;
window.applyNormalSpeedFilter = applyNormalSpeedFilter;
window.applyStutterFilter = applyStutterFilter;
window.getSelectedFilterContext = getSelectedFilterContext;
window.toggleTrackFx = toggleTrackFx;
window.setTrackFxPitch = setTrackFxPitch;
window.setTrackFxStutter = setTrackFxStutter;
window.selectFxTrack = selectFxTrack;


// ============= Update XTRA Pads Filter Status =============
function updateXtraFiltersStatus() {
    const statusEl = document.getElementById('xtraFiltersStatus');
    if (!statusEl) return;
    
    if (xtraPads.length === 0) {
        statusEl.innerHTML = '<span class="no-filters">Añade XTRA pads para aplicar filtros individuales</span>';
        return;
    }
    
    let html = '';
    xtraPads.forEach(pad => {
        const filterIdx = padFilterState[pad.padIndex] || 0;
        const fx = padFxState[pad.padIndex];
        const filterName = FILTER_TYPES[filterIdx] ? FILTER_TYPES[filterIdx].name : 'OFF';
        const filterIcon = FILTER_TYPES[filterIdx] ? FILTER_TYPES[filterIdx].icon : '🚫';
        const hasFx = fx && ((fx.distortion && fx.distortion > 0) || (fx.bitcrush && fx.bitcrush < 16));
        
        html += `<div style="display:flex;align-items:center;gap:8px;padding:6px 0;border-bottom:1px solid rgba(255,255,255,0.05);">
            <span style="color:#ff6600;font-weight:bold;min-width:70px;">XTRA ${pad.padIndex - 15}</span>
            <span style="font-size:18px;">${filterIcon}</span>
            <span style="color:${filterIdx > 0 ? '#1abc9c' : '#666'};">${filterName}</span>
            ${hasFx ? '<span style="color:#ff3366;font-size:11px;">🎸 FX</span>' : ''}
        </div>`;
    });
    statusEl.innerHTML = html;
}
window.updateXtraFiltersStatus = updateXtraFiltersStatus;

// ============= TRACK VOLUME MENU =============
let activeVolumeMenu = null;
let trackVolumes = new Array(16).fill(100); // Default 100%

function showVolumeMenu(track, button) {
    // Cerrar menú activo si existe
    if (activeVolumeMenu) {
        activeVolumeMenu.remove();
        if (activeVolumeMenu.dataset.track === track.toString()) {
            activeVolumeMenu = null;
            return; // Toggle off
        }
    }
    
    // Crear menú
    const menu = document.createElement('div');
    menu.className = 'volume-menu';
    menu.dataset.track = track;
    
    // Valor actual
    const valueDisplay = document.createElement('div');
    valueDisplay.className = 'volume-value';
    valueDisplay.textContent = trackVolumes[track] + '%';
    
    // Slider vertical
    const sliderContainer = document.createElement('div');
    sliderContainer.className = 'volume-slider-container';
    
    const slider = document.createElement('input');
    slider.type = 'range';
    slider.className = 'volume-slider';
    slider.min = '0';
    slider.max = '100';
    slider.value = trackVolumes[track];
    slider.orient = 'vertical'; // Para navegadores antiguos
    
    slider.addEventListener('input', (e) => {
        const volume = parseInt(e.target.value);
        trackVolumes[track] = volume;
        valueDisplay.textContent = volume + '%';
        
        // Enviar a ESP32
        sendWebSocket({
            cmd: 'setTrackVolume',
            track: track,
            volume: volume
        });
    });
    
    sliderContainer.appendChild(slider);
    menu.appendChild(valueDisplay);
    menu.appendChild(sliderContainer);
    
    // Posicionar menú
    document.body.appendChild(menu);
    const rect = button.getBoundingClientRect();
    menu.style.left = rect.left + 'px';
    menu.style.top = (rect.bottom + 5) + 'px';
    
    activeVolumeMenu = menu;
    
    // Cerrar al hacer click fuera
    setTimeout(() => {
        document.addEventListener('click', closeVolumeMenuOnClickOutside);
    }, 10);
}

function closeVolumeMenuOnClickOutside(e) {
    if (activeVolumeMenu && !activeVolumeMenu.contains(e.target) && 
        !e.target.classList.contains('volume-btn')) {
        activeVolumeMenu.remove();
        activeVolumeMenu = null;
        document.removeEventListener('click', closeVolumeMenuOnClickOutside);
    }
}

function updateTrackVolume(track, volume) {
    if (track >= 0 && track < 16) {
        if (trackVolumes[track] === volume) return;
        trackVolumes[track] = volume;
        // Actualizar display si el menú está abierto para este track
        if (activeVolumeMenu && activeVolumeMenu.dataset.track === track.toString()) {
            const valueDisplay = activeVolumeMenu.querySelector('.volume-value');
            const slider = activeVolumeMenu.querySelector('.volume-slider');
            if (valueDisplay) valueDisplay.textContent = volume + '%';
            if (slider) slider.value = volume;
        }
        
        // Update track label background alpha based on volume
        const trackLabel = document.querySelector(`.track-label[data-track="${track}"]`);
        if (trackLabel) {
            updateTrackLabelBackground(trackLabel, track, volume);
        }
        
        // Update volume bar in volumes section
        if (window.updateVolumeBar) {
            window.updateVolumeBar(track, volume);
        }
    }
}

function updateTrackLabelBackground(label, track, volume) {
    const trackColors = [
        '#ff0000', '#ffa500', '#ffff00', '#00ffff',
        '#e6194b', '#ff00ff', '#00ff00', '#f58231',
        '#911eb4', '#46f0f0', '#f032e6', '#bcf60c',
        '#38ceff', '#fabebe', '#008080', '#484dff'
    ];
    const color = trackColors[track];
    if (!color) return;
    
    // Convert hex to RGB
    const r = parseInt(color.slice(1, 3), 16);
    const g = parseInt(color.slice(3, 5), 16);
    const b = parseInt(color.slice(5, 7), 16);
    
    // Calculate alpha based on volume (0-100 -> 0.1-0.7)
    // Min alpha 0.1 for low volume, max 0.7 for full volume (más vivo)
    const alpha = 0.1 + (volume / 100) * 0.6;
    
    label.style.background = `rgba(${r}, ${g}, ${b}, ${alpha})`;
}

window.showVolumeMenu = showVolumeMenu;
window.updateTrackVolume = updateTrackVolume;

// ============================================
// LIVE PADS X - Independent / Free Pads
// ============================================

const xtraPads = []; // Array of { id, padIndex, family, filename, element }
let xtraPadCounter = 0;
const xtraTremoloIntervals = {};

function initLivePadsX() {
    const grid = document.getElementById('padsXtraGrid');
    if (!grid) return;
    grid.innerHTML = '';
    renderXtraAddButton();
}

function renderXtraAddButton() {
    const grid = document.getElementById('padsXtraGrid');
    if (!grid) return;

    // Remove existing add button if present
    const existingAdd = grid.querySelector('.pad-xtra-add');
    if (existingAdd) existingAdd.remove();

    const addBtn = document.createElement('div');
    addBtn.className = 'pad-xtra-add';
    addBtn.innerHTML = '<span>+</span>';
    addBtn.title = 'Add XTRA Pad';
    addBtn.addEventListener('click', () => showXtraPadPicker());
    grid.appendChild(addBtn);
}

// Next available XTRA pad slot (16-23)
let nextXtraSlot = 16;

function showXtraPadPicker() {
    // Count used XTRA slots
    const usedSlots = xtraPads.map(p => p.padIndex);
    let freeSlot = -1;
    for (let s = 16; s < 24; s++) {
        if (!usedSlots.includes(s)) { freeSlot = s; break; }
    }
    if (freeSlot < 0) {
        if (window.showToast) window.showToast('❌ Maximum 8 XTRA pads', window.TOAST_TYPES?.ERROR, 3000);
        return;
    }

    const modal = document.createElement('div');
    modal.className = 'sample-modal';
    modal.innerHTML = `
        <div class="sample-modal-content" style="max-width:440px;">
            <h3 style="margin:0 0 8px;">🎲 XTRA Pad — Slot ${freeSlot - 15}/8</h3>
            <p style="color:#aaa;margin:0 0 14px;font-size:11px;">Pads independientes (no Sequencer). Elige sample de la librería o sube un WAV.</p>
            
            <!-- XTRA Library Browser -->
            <div id="xtraLibrarySection" style="margin-bottom:14px;">
                <div style="display:flex;justify-content:space-between;align-items:center;margin-bottom:8px;">
                    <span style="color:#ff6600;font-weight:bold;font-size:13px;">📚 LIBRERÍA XTRA</span>
                    <button id="xtraRefreshBtn" style="background:#333;border:1px solid #555;color:#aaa;padding:3px 8px;border-radius:4px;cursor:pointer;font-size:11px;">🔄</button>
                </div>
                <div id="xtraLibraryList" style="max-height:180px;overflow-y:auto;border:1px solid #333;border-radius:8px;background:#111;padding:4px;">
                    <div style="text-align:center;color:#666;padding:16px;font-size:12px;">⏳ Cargando samples...</div>
                </div>
            </div>
            
            <!-- Upload Zone -->
            <div id="xtraUploadZone" style="border:2px dashed #ff6600;border-radius:10px;padding:18px 16px;text-align:center;cursor:pointer;transition:all .2s;">
                <div style="font-size:28px;margin-bottom:4px;">📤</div>
                <div style="color:#ff6600;font-weight:bold;font-size:13px;" id="xtraUploadMsg">Click para subir WAV</div>
                <div style="color:#666;font-size:10px;margin-top:4px;">Max 2MB · WAV format</div>
            </div>
            <div style="margin-top:12px;text-align:right;">
                <button class="btn-close-modal">Cancelar</button>
            </div>
        </div>
    `;

    // Request XTRA samples list
    sendWebSocket({ cmd: 'getXtraSamples' });
    
    // Listen for xtraSampleList response
    const xtraListHandler = (event) => {
        if (typeof event.data !== 'string') return;
        try {
            const data = JSON.parse(event.data);
            if (data.type === 'xtraSampleList') {
                ws.removeEventListener('message', xtraListHandler);
                const listEl = modal.querySelector('#xtraLibraryList');
                if (!listEl) return;
                
                if (!data.samples || data.samples.length === 0) {
                    listEl.innerHTML = '<div style="text-align:center;color:#666;padding:16px;font-size:12px;">📭 No hay samples en /xtra<br><span style="font-size:10px;">Sube WAV con el botón de abajo</span></div>';
                    return;
                }
                
                listEl.innerHTML = '';
                data.samples.forEach(sample => {
                    const item = document.createElement('div');
                    item.style.cssText = 'display:flex;justify-content:space-between;align-items:center;padding:8px 10px;border-bottom:1px solid #222;cursor:pointer;transition:background 0.15s;border-radius:4px;';
                    item.innerHTML = `
                        <div>
                            <div style="color:#eee;font-size:12px;font-weight:bold;">${sample.name}</div>
                            <div style="color:#666;font-size:10px;">${(sample.size / 1024).toFixed(1)} KB</div>
                        </div>
                        <button style="background:#ff6600;border:none;color:#fff;padding:4px 12px;border-radius:4px;cursor:pointer;font-size:11px;font-weight:bold;">CARGAR</button>
                    `;
                    item.addEventListener('mouseenter', () => item.style.background = '#1a1a1a');
                    item.addEventListener('mouseleave', () => item.style.background = '');
                    item.querySelector('button').addEventListener('click', (e) => {
                        e.stopPropagation();
                        sendWebSocket({ cmd: 'loadXtraSample', filename: sample.name, pad: freeSlot });
                        createXtraPad(freeSlot, sample.name.replace(/\.wav$/i, ''));
                        modal.remove();
                        if (window.showToast) window.showToast(`✅ ${sample.name} → XTRA ${freeSlot - 15}`, window.TOAST_TYPES?.SUCCESS, 2000);
                    });
                    listEl.appendChild(item);
                });
            }
        } catch(e) {}
    };
    if (ws) ws.addEventListener('message', xtraListHandler);
    
    // Refresh button
    modal.querySelector('#xtraRefreshBtn').addEventListener('click', () => {
        const listEl = modal.querySelector('#xtraLibraryList');
        if (listEl) listEl.innerHTML = '<div style="text-align:center;color:#666;padding:16px;font-size:12px;">⏳ Cargando samples...</div>';
        if (ws) ws.addEventListener('message', xtraListHandler);
        sendWebSocket({ cmd: 'getXtraSamples' });
    });

    const uploadZone = modal.querySelector('#xtraUploadZone');
    const uploadMsg = modal.querySelector('#xtraUploadMsg');

    uploadZone.addEventListener('click', () => {
        const input = document.createElement('input');
        input.type = 'file';
        input.accept = '.wav,.WAV';
        input.style.display = 'none';
        input.addEventListener('change', (e) => {
            const file = e.target.files[0];
            if (!file) return;
            if (!file.name.toLowerCase().endsWith('.wav')) {
                if (window.showToast) window.showToast('❌ Solo archivos WAV', window.TOAST_TYPES?.ERROR, 3000);
                return;
            }
            if (file.size > 2 * 1024 * 1024) {
                if (window.showToast) window.showToast('❌ Máximo 2MB', window.TOAST_TYPES?.ERROR, 3000);
                return;
            }
            uploadMsg.textContent = `⏳ Uploading ${file.name}...`;
            uploadZone.style.pointerEvents = 'none';
            uploadZone.style.opacity = '0.6';

            const slot = freeSlot;
            const formData = new FormData();
            formData.append('file', file);
            fetch(`/api/upload?pad=${slot}`, { method: 'POST', body: formData })
                .then(r => r.json())
                .then(data => {
                    if (data.success) {
                        if (window.showToast) window.showToast(`✅ ${file.name} → XTRA ${slot - 15}`, window.TOAST_TYPES?.SUCCESS, 2000);
                        setTimeout(() => {
                            createXtraPad(slot, file.name.replace(/\.wav$/i, ''));
                            modal.remove();
                        }, 300);
                    } else {
                        if (window.showToast) window.showToast(`❌ ${data.message || 'Error'}`, window.TOAST_TYPES?.ERROR, 3000);
                        uploadMsg.textContent = 'Click para subir WAV';
                        uploadZone.style.pointerEvents = ''; uploadZone.style.opacity = '';
                    }
                })
                .catch(err => {
                    if (window.showToast) window.showToast(`❌ ${err.message}`, window.TOAST_TYPES?.ERROR, 3000);
                    uploadMsg.textContent = 'Click para subir WAV';
                    uploadZone.style.pointerEvents = ''; uploadZone.style.opacity = '';
                });
        });
        document.body.appendChild(input);
        input.click();
        setTimeout(() => input.remove(), 1000);
    });

    modal.querySelector('.btn-close-modal').addEventListener('click', () => {
        if (ws) ws.removeEventListener('message', xtraListHandler);
        modal.remove();
    });
    modal.addEventListener('click', (e) => {
        if (e.target === modal) {
            if (ws) ws.removeEventListener('message', xtraListHandler);
            modal.remove();
        }
    });
    document.body.appendChild(modal);
}

function createXtraPad(padIndex, label) {
    const grid = document.getElementById('padsXtraGrid');
    if (!grid) return;

    const id = ++xtraPadCounter;
    const displayName = label || `XTRA ${padIndex - 15}`;

    const padEl = document.createElement('div');
    padEl.className = 'pad-xtra';
    padEl.dataset.xtraId = id;
    padEl.dataset.padIndex = padIndex;
    padEl.innerHTML = `
        <div class="pad-xtra-name">${displayName}</div>
        <div class="pad-xtra-sample" title="Slot ${padIndex - 15}">XTRA ${padIndex - 15}</div>
        <div class="pad-xtra-controls">
            <button class="pad-xtra-btn xtra-loop" title="Loop">🔁</button>
            <button class="pad-xtra-btn xtra-filter" title="Filter">F</button>
            <button class="pad-xtra-btn xtra-fx" title="FX">🎸</button>
            <button class="pad-xtra-btn xtra-delete" title="Remove">🗑️</button>
        </div>
    `;

    // ── Touch/Click → trigger + tremolo ──
    padEl.addEventListener('touchstart', (e) => {
        if (e.target.closest('.pad-xtra-controls')) return;
        e.preventDefault();
        startXtraTremolo(id, padIndex, padEl);
    });
    padEl.addEventListener('touchend', (e) => {
        if (e.target.closest('.pad-xtra-controls')) return;
        e.preventDefault();
        stopXtraTremolo(id, padEl);
    });
    padEl.addEventListener('mousedown', (e) => {
        if (e.target.closest('.pad-xtra-controls')) return;
        startXtraTremolo(id, padIndex, padEl);
    });
    padEl.addEventListener('mouseup', (e) => {
        if (e.target.closest('.pad-xtra-controls')) return;
        stopXtraTremolo(id, padEl);
    });
    padEl.addEventListener('mouseleave', () => {
        stopXtraTremolo(id, padEl);
    });

    // ── Controls ──
    const loopBtn = padEl.querySelector('.xtra-loop');
    loopBtn.addEventListener('click', (e) => {
        e.stopPropagation();
        // XTRA pads: continuous audio loop (toggle directly)
        sendWebSocket({ cmd: 'toggleLoop', track: padIndex });
    });

    const filterBtn = padEl.querySelector('.xtra-filter');
    filterBtn.addEventListener('click', (e) => { e.stopPropagation(); showPadFilterSelector(padIndex, padEl); });

    const fxBtn = padEl.querySelector('.xtra-fx');
    fxBtn.addEventListener('click', (e) => { e.stopPropagation(); showPadFxPopup(padIndex, padEl); });

    const deleteBtn = padEl.querySelector('.xtra-delete');
    deleteBtn.addEventListener('click', (e) => {
        e.stopPropagation();
        removeXtraPad(id);
    });

    // Store reference
    xtraPads.push({ id, padIndex, label: displayName, element: padEl });

    // Insert before "+" button
    const addBtn = grid.querySelector('.pad-xtra-add');
    grid.insertBefore(padEl, addBtn);
}

function removeXtraPad(id) {
    const idx = xtraPads.findIndex(p => p.id === id);
    if (idx === -1) return;
    const pad = xtraPads[idx];
    stopXtraTremolo(id, pad.element);
    pad.element.remove();
    xtraPads.splice(idx, 1);
}

function startXtraTremolo(id, padIndex, padEl) {
    triggerPad(padIndex);
    padEl.classList.add('active');
    padEl.style.filter = 'brightness(1.4)';
    setTimeout(() => { padEl.style.filter = ''; }, 120);

    xtraTremoloIntervals[id] = setTimeout(() => {
        padEl.classList.add('tremolo-active');
        xtraTremoloIntervals[id] = setInterval(() => {
            triggerPad(padIndex);
            padEl.style.filter = 'brightness(1.35)';
            setTimeout(() => { padEl.style.filter = 'brightness(1.1)'; }, 22);
        }, 55);
    }, 100);
}

function stopXtraTremolo(id, padEl) {
    if (xtraTremoloIntervals[id]) {
        clearTimeout(xtraTremoloIntervals[id]);
        clearInterval(xtraTremoloIntervals[id]);
        delete xtraTremoloIntervals[id];
    }
    padEl.classList.remove('active', 'tremolo-active');
    padEl.style.filter = '';
}

window.initLivePadsX = initLivePadsX;

// ============================================
// VOLUMES SECTION - Initialization & Updates
// ============================================

function initVolumesSection() {
    // Initialize all track volumes to default
    for (let track = 0; track < 16; track++) {
        updateVolumeBar(track, trackVolumes[track]);
        updateVolumeMutedState(track, trackMutedState[track]);
    }
}

function updateVolumeBar(track, volume) {
    if (track < 0 || track >= 16) return;
    
    const volumeBar = document.getElementById(`trackVolumeBar${track}`);
    const volumeValue = document.getElementById(`trackVolumeValue${track}`);
    const volumeCard = document.querySelector(`.track-volume-card[data-track="${track}"]`);
    
    if (volumeBar) {
        const clampedVolume = Math.min(Math.max(volume, 0), 150);
        
        // Contenedor siempre es 330px (para 150%)
        // Al 100%: barra debe ocupar 220px (66.67% del contenedor)
        // Al 150%: barra debe ocupar 330px (100% del contenedor)
        // Fórmula: heightPercentage = (volume / 150) * 100
        const heightPercentage = (clampedVolume / 150) * 100;
        
        volumeBar.style.height = `${heightPercentage}%`;
        
        // Agregar clases overboost si supera 100%
        if (volume > 100) {
            volumeBar.classList.add('overboost');
            if (volumeCard) {
                volumeCard.classList.add('overboost-container');
            }
        } else {
            volumeBar.classList.remove('overboost');
            if (volumeCard) {
                volumeCard.classList.remove('overboost-container');
            }
        }
    }
    
    if (volumeValue) {
        volumeValue.textContent = `${volume}%`;
    }
}

function updateVolumeMutedState(track, isMuted) {
    if (track < 0 || track >= 16) return;
    
    const volumeCard = document.querySelector(`.track-volume-card[data-track="${track}"]`);
    
    if (volumeCard) {
        if (isMuted) {
            volumeCard.classList.add('muted');
        } else {
            volumeCard.classList.remove('muted');
        }
    }
}

function updateMasterVolumeDisplays(sequencerVolume, padsVolume) {
    // Update Sequencer Volume
    const displaySequencerVolume = document.getElementById('displaySequencerVolume');
    const barSequencerVolume = document.getElementById('barSequencerVolume');
    
    if (displaySequencerVolume) {
        displaySequencerVolume.textContent = `${sequencerVolume}%`;
    }
    
    if (barSequencerVolume) {
        const percentage = Math.min(Math.max(sequencerVolume, 0), 150);
        barSequencerVolume.style.width = `${percentage}%`;
        
        // Agregar clase overboost si supera 100%
        if (sequencerVolume > 100) {
            barSequencerVolume.classList.add('overboost');
        } else {
            barSequencerVolume.classList.remove('overboost');
        }
    }
    
    // Update Pads Volume
    const displayPadsVolume = document.getElementById('displayPadsVolume');
    const barPadsVolume = document.getElementById('barPadsVolume');
    
    if (displayPadsVolume) {
        displayPadsVolume.textContent = `${padsVolume}%`;
    }
    
    if (barPadsVolume) {
        const percentage = Math.min(Math.max(padsVolume, 0), 150);
        barPadsVolume.style.width = `${percentage}%`;
        
        // Agregar clase overboost si supera 100%
        if (padsVolume > 100) {
            barPadsVolume.classList.add('overboost');
        } else {
            barPadsVolume.classList.remove('overboost');
        }
    }
}

// Export functions
window.initVolumesSection = initVolumesSection;
window.updateVolumeBar = updateVolumeBar;
window.updateVolumeMutedState = updateVolumeMutedState;
window.updateMasterVolumeDisplays = updateMasterVolumeDisplays;

// ============================================
// WAVEFORM MARKER HELPERS (drag start/end)
// ============================================

function _drawWaveformWithMarkers(canvas, state) {
    if (!canvas || !state.peaks) return;
    const color = (state.padIndex < 16) ? WaveformRenderer.trackColors[state.padIndex] : '#00ff88';
    WaveformRenderer.drawStatic(canvas, state.peaks, {
        color: color,
        startPoint: state.startNorm,
        endPoint: state.endNorm,
        accentColor: '#ff3366'
    });
}

function _updateTrimLabels(modal, state) {
    const startLabel = modal.querySelector('#trimStartValue');
    const endLabel = modal.querySelector('#trimEndValue');
    if (startLabel) startLabel.textContent = `Start: ${Math.round(state.startNorm * 100)}%`;
    if (endLabel) endLabel.textContent = `End: ${Math.round(state.endNorm * 100)}%`;
    
    // Reset markers position
    const wrapper = modal.querySelector('#waveformCanvasWrapper');
    const markerS = modal.querySelector('#waveformMarkerStart');
    const markerE = modal.querySelector('#waveformMarkerEnd');
    if (wrapper && markerS && markerE) {
        const w = wrapper.offsetWidth;
        markerS.style.left = (state.startNorm * w - 8) + 'px';
        markerE.style.left = (state.endNorm * w - 8) + 'px';
    }
}

function _setupWaveformMarkers(modal, canvas, state) {
    const wrapper = modal.querySelector('#waveformCanvasWrapper');
    const markerS = modal.querySelector('#waveformMarkerStart');
    const markerE = modal.querySelector('#waveformMarkerEnd');
    if (!wrapper || !markerS || !markerE) return;
    
    // Function to get normalized X position from pointer event
    function getNormX(e) {
        const rect = wrapper.getBoundingClientRect();
        let clientX = e.touches ? e.touches[0].clientX : e.clientX;
        let x = (clientX - rect.left) / rect.width;
        return Math.max(0, Math.min(1, x));
    }
    
    function makeDraggable(marker, isStart) {
        let dragging = false;
        
        function onStart(e) {
            e.preventDefault();
            e.stopPropagation();
            dragging = true;
            marker.classList.add('dragging');
        }
        
        function onMove(e) {
            if (!dragging) return;
            e.preventDefault();
            const norm = getNormX(e);
            if (isStart) {
                state.startNorm = Math.min(norm, state.endNorm - 0.02);
            } else {
                state.endNorm = Math.max(norm, state.startNorm + 0.02);
            }
            _updateTrimLabels(modal, state);
            _drawWaveformWithMarkers(canvas, state);
        }
        
        function onEnd() {
            if (!dragging) return;
            dragging = false;
            marker.classList.remove('dragging');
        }
        
        marker.addEventListener('mousedown', onStart);
        marker.addEventListener('touchstart', onStart, { passive: false });
        document.addEventListener('mousemove', onMove);
        document.addEventListener('touchmove', onMove, { passive: false });
        document.addEventListener('mouseup', onEnd);
        document.addEventListener('touchend', onEnd);
    }
    
    makeDraggable(markerS, true);
    makeDraggable(markerE, false);
    
    // Click on canvas to set nearest marker
    canvas.addEventListener('click', (e) => {
        const rect = wrapper.getBoundingClientRect();
        const norm = (e.clientX - rect.left) / rect.width;
        const distStart = Math.abs(norm - state.startNorm);
        const distEnd = Math.abs(norm - state.endNorm);
        if (distStart < distEnd) {
            state.startNorm = Math.min(norm, state.endNorm - 0.02);
        } else {
            state.endNorm = Math.max(norm, state.startNorm + 0.02);
        }
        _updateTrimLabels(modal, state);
        _drawWaveformWithMarkers(canvas, state);
    });
    
    // Initial marker positions
    setTimeout(() => _updateTrimLabels(modal, state), 50);
}

// ── Header M / S buttons ────────────────────────────────────────────────────
(function initHeaderMSButtons() {
    const volBtn  = document.getElementById('hdrVolBtn');
    const muteBtn = document.getElementById('hdrMuteBtn');
    const soloBtn = document.getElementById('hdrSoloBtn');
    const volPopup  = document.getElementById('hdrVolPopup');
    const volSlider = document.getElementById('hdrVolPopupSlider');
    const volVal    = document.getElementById('hdrVolPopupValue');
    const volLabel  = document.getElementById('hdrVolPopupLabel');
    const trackSel  = document.getElementById('hdrTrackSelect');
    const trackNames16 = ['BD','SD','CH','OH','CY','CP','RS','CB','LT','MT','HT','MA','CL','HC','MC','LC'];

    function getSelectedTrack() {
        // Primero el selector del header
        if (trackSel) {
            const v = parseInt(trackSel.value);
            if (!isNaN(v) && v >= 0) return v;
        }
        if (typeof window.lastSelectedPad === 'number') return window.lastSelectedPad;
        const selEl = document.querySelector('.pad.selected, .pad-active-selected, .pad[data-selected="true"]');
        if (selEl) {
            const idx = parseInt(selEl.dataset.pad ?? selEl.dataset.padIndex ?? '-1');
            if (!isNaN(idx) && idx >= 0) return idx;
        }
        return -1;
    }

    // ── Sync selector → estado M/S ──
    function refreshBtnStates(t) {
        if (muteBtn) {
            const muted = typeof trackMutedState !== 'undefined' ? !!trackMutedState[t] : false;
            muteBtn.classList.toggle('active', muted);
        }
        if (soloBtn) {
            const isSolo = typeof trackSoloState !== 'undefined' && trackSoloState === t;
            soloBtn.classList.toggle('active', isSolo);
        }
    }

    if (trackSel) {
        trackSel.addEventListener('change', () => {
            const t = parseInt(trackSel.value);
            if (t >= 0) {
                window.lastSelectedPad = t;
                refreshBtnStates(t);
            }
        });
        // API pública para que otros módulos sincronicen el selector
        window.updateHdrTrackSelect = function(trackIdx) {
            if (trackIdx >= 0 && trackIdx < 16) {
                trackSel.value = trackIdx;
                window.lastSelectedPad = trackIdx;
                refreshBtnStates(trackIdx);
            }
        };
    }

    // ── V button ──
    if (volBtn && volPopup) {
        volBtn.addEventListener('click', (e) => {
            e.stopPropagation();
            const track = getSelectedTrack();
            if (track < 0) {
                if (window.showToast) window.showToast('Selecciona un pad primero', 'warning');
                return;
            }
            const cur = typeof trackVolumes !== 'undefined' ? (trackVolumes[track] ?? 100) : 100;
            volSlider.value = cur;
            if (volVal) volVal.textContent = cur;
            if (volLabel) volLabel.textContent = (trackNames16[track] || `T${track}`) + ' VOL';
            volPopup.style.display = volPopup.style.display === 'flex' ? 'none' : 'flex';
            volBtn.classList.toggle('active', volPopup.style.display === 'flex');
        });

        volSlider.addEventListener('input', () => {
            if (volVal) volVal.textContent = volSlider.value;
        });

        volSlider.addEventListener('change', () => {
            const track = getSelectedTrack();
            if (track < 0) return;
            const volume = parseInt(volSlider.value);
            sendWebSocket({ cmd: 'setTrackVolume', track, volume });
            if (typeof trackVolumes !== 'undefined') trackVolumes[track] = volume;
            if (typeof updateTrackVolume === 'function') updateTrackVolume(track, volume);
        });

        document.addEventListener('click', (e) => {
            if (!volPopup.contains(e.target) && e.target !== volBtn) {
                volPopup.style.display = 'none';
                volBtn.classList.remove('active');
            }
        });
    }

    // ── M button ──
    if (muteBtn) {
        muteBtn.addEventListener('click', () => {
            const track = getSelectedTrack();
            if (track < 0) {
                if (window.showToast) window.showToast('Selecciona un pad primero', 'warning');
                return;
            }
            const nowMuted = typeof trackMutedState !== 'undefined' ? trackMutedState[track] : false;
            if (typeof setTrackMuted === 'function') setTrackMuted(track, !nowMuted, true);
            muteBtn.classList.toggle('active', !nowMuted);
        });
    }

    // ── S button ──
    if (soloBtn) {
        soloBtn.addEventListener('click', () => {
            const track = getSelectedTrack();
            if (track < 0) {
                if (window.showToast) window.showToast('Selecciona un pad primero', 'warning');
                return;
            }
            if (typeof setSoloTrack === 'function') setSoloTrack(track);
            const isSolo = typeof trackSoloState !== 'undefined' && trackSoloState === track;
            soloBtn.classList.toggle('active', !isSolo);
        });
    }
})();

// ── Pad layout selector (4 / 8 / 16 per row) ────────────────────────────────
(function initPadLayoutSelector() {
    const grid = document.getElementById('padsGrid');
    const buttons = document.querySelectorAll('.pls-btn');

    const applyColsFromButton = (btn) => {
        const cols = parseInt(btn.dataset.cols, 10);
        if (grid && !Number.isNaN(cols)) {
            grid.style.gridTemplateColumns = `repeat(${cols}, 1fr)`;
        }
    };

    buttons.forEach(btn => {
        btn.addEventListener('click', () => {
            buttons.forEach(b => b.classList.remove('active'));
            btn.classList.add('active');
            applyColsFromButton(btn);
        });
    });

    const defaultBtn = document.querySelector('.pls-btn.active') || document.querySelector('.pls-btn[data-cols="8"]');
    if (defaultBtn) {
        applyColsFromButton(defaultBtn);
    }
})();

// ── Volume layout selector (4 / 8 / 16 per row) ─────────────────────────────
(function initVolLayoutSelector() {
    const grid = document.getElementById('trackVolumesGrid');
    document.querySelectorAll('.vls-btn').forEach(btn => {
        btn.addEventListener('click', () => {
            document.querySelectorAll('.vls-btn').forEach(b => b.classList.remove('active'));
            btn.classList.add('active');
            const cols = parseInt(btn.dataset.cols);
            if (grid) grid.style.gridTemplateColumns = `repeat(${cols}, 1fr)`;
        });
    });
})();

// ══════════════════════════════════════════════════════════════════════════════
//  DAISY SD CARD — Kit Browser & File Manager
// ══════════════════════════════════════════════════════════════════════════════
let _sdSelectedFile = null;   // {folder, file}
let _sdSelectedPad  = -1;

function sdRefreshStatus() {
    sendWebSocket({cmd: 'sdGetStatus'});
    sendWebSocket({cmd: 'sdListKits'});
}

function sdListFolders() {
    sendWebSocket({cmd: 'sdListFolders'});
    const bc = document.getElementById('sdBreadcrumb');
    if (bc) bc.innerHTML = '<span class="sd-crumb sd-crumb-root" onclick="sdListFolders()">/ root</span>';
    _sdSelectedFile = null;
    sdUpdateAssignInfo();
}

function sdListFiles(folder) {
    sendWebSocket({cmd: 'sdListFiles', folder});
    const bc = document.getElementById('sdBreadcrumb');
    if (bc) bc.innerHTML = `<span class="sd-crumb sd-crumb-root" onclick="sdListFolders()">/ root</span> <span class="sd-crumb-sep">›</span> <span class="sd-crumb">${folder}</span>`;
    _sdSelectedFile = null;
    sdUpdateAssignInfo();
}

function sdLoadKit(kitName) {
    sendWebSocket({cmd: 'sdLoadKit', kit: kitName});
    sdLog(`Loading kit: ${kitName}...`);
}

function sdUnloadKit() {
    sendWebSocket({cmd: 'sdUnloadKit'});
}

function sdAssignFile(pad) {
    if (!_sdSelectedFile) return;
    sendWebSocket({cmd: 'sdLoadSample', pad, folder: _sdSelectedFile.folder, file: _sdSelectedFile.file});
    sdLog(`Loading "${_sdSelectedFile.file}" → pad ${pad}...`);
}

function sdRenderStatus(d) {
    const ind = document.getElementById('sdIndicator');
    const txt = document.getElementById('sdStatusText');
    const stats = document.getElementById('sdStats');
    const kitSec = document.getElementById('sdCurrentKit');
    if (ind) ind.style.color = d.present ? '#0f0' : '#f00';
    if (txt) txt.textContent = d.present ? 'SD Card OK' : 'No SD Card';
    if (stats && d.present) stats.textContent = `${d.freeMB}/${d.totalMB} MB free`;
    if (kitSec) {
        const hasKit = d.kit && d.kit.length > 0;
        kitSec.style.display = hasKit ? 'flex' : 'none';
        if (hasKit) {
            const kn = document.getElementById('sdKitName');
            const kp = document.getElementById('sdKitPads');
            if (kn) kn.textContent = d.kit;
            if (kp) kp.textContent = `(${d.loaded} pads)`;
        }
    }
}

function sdRenderKitList(kits, error) {
    const el = document.getElementById('sdKitList');
    if (!el) return;
    if (error) { el.innerHTML = `<div class="sd-error">${error}</div>`; return; }
    if (!kits.length) { el.innerHTML = '<div class="sd-empty">No kits found</div>'; return; }
    el.innerHTML = kits.map(k => `<div class="sd-kit-item" onclick="sdLoadKit('${k}')" title="Click to load"><span class="sd-kit-icon">📁</span> ${k}</div>`).join('');
}

function sdRenderFolders(folders) {
    const el = document.getElementById('sdFileList');
    if (!el) return;
    if (!folders.length) { el.innerHTML = '<div class="sd-empty">Empty</div>'; return; }
    el.innerHTML = folders.map(f => `<div class="sd-folder-item" onclick="sdListFiles('${f}')"><span class="sd-icon">📂</span> ${f}</div>`).join('');
}

function sdRenderFiles(folder, files) {
    const el = document.getElementById('sdFileList');
    if (!el) return;
    if (!files.length) { el.innerHTML = '<div class="sd-empty">No files</div>'; return; }
    el.innerHTML = files.map(f => {
        const sizeKB = (f.size / 1024).toFixed(1);
        return `<div class="sd-file-item" onclick="sdSelectFile('${folder}','${f.name}',this)" title="${sizeKB} KB"><span class="sd-icon">🔊</span> ${f.name} <small>${sizeKB}K</small></div>`;
    }).join('');
}

function sdSelectFile(folder, file, el) {
    document.querySelectorAll('.sd-file-item.selected').forEach(e => e.classList.remove('selected'));
    if (el) el.classList.add('selected');
    _sdSelectedFile = {folder, file};
    sdUpdateAssignInfo();
}

function sdUpdateAssignInfo() {
    const el = document.getElementById('sdAssignInfo');
    if (!el) return;
    if (_sdSelectedFile) {
        el.textContent = `"${_sdSelectedFile.file}" → click a pad to assign`;
        el.classList.add('ready');
    } else {
        el.textContent = 'Select a file, then click a pad';
        el.classList.remove('ready');
    }
}

function sdHandleEvent(data) {
    // Events from Daisy: load progress, completion, errors
    const evtNames = {1: 'Kit loaded', 2: 'Load error', 3: 'Kit unloaded', 4: 'Sample loaded', 5: 'Boot loaded', 6: 'XTRA loaded'};
    const name = evtNames[data.event] || `Event ${data.event}`;
    const extra = data.name ? ` "${data.name}"` : '';
    sdLog(`${name}${extra} (${data.padCount} pads)`);
    if (data.event === 1 || data.event === 3 || data.event === 4) sdRefreshStatus();
}

function sdLog(msg) {
    const el = document.getElementById('sdLog');
    if (!el) return;
    const line = document.createElement('div');
    line.className = 'sd-log-line';
    line.textContent = `[${new Date().toLocaleTimeString()}] ${msg}`;
    el.prepend(line);
    while (el.children.length > 20) el.removeChild(el.lastChild);
}

// Build pad assignment grid
(function initSdPadGrid() {
    const grid = document.getElementById('sdPadGrid');
    if (!grid) return;
    const names = ['BD','SD','CH','OH','CY','CP','RS','CB','LT','MT','HT','MA','CL','HC','MC','LC'];
    for (let i = 0; i < 16; i++) {
        const btn = document.createElement('button');
        btn.className = 'sd-pad-btn';
        btn.textContent = names[i] || i;
        btn.dataset.pad = i;
        btn.onclick = () => {
            _sdSelectedPad = i;
            document.querySelectorAll('.sd-pad-btn.active').forEach(b => b.classList.remove('active'));
            btn.classList.add('active');
            sdAssignFile(i);
        };
        grid.appendChild(btn);
    }
})();


// ══════════════════════════════════════════════════════════════════════════════
//  LFO ENGINE — Per-Pad Organic Modulation
// ══════════════════════════════════════════════════════════════════════════════
const LFO_WAVES   = ['Sine','Triangle','Square','Saw','S&H'];
const LFO_DIVS    = ['1/4','1/8','1/16','1/32','Free'];
const LFO_TARGETS = ['Pitch','Decay','Filter','Pan','Volume'];
const LFO_COLORS  = [ // Per-pad colors for scope visualization
    '#f44','#f80','#ff0','#8f0','#0f8','#0ff','#08f','#80f',
    '#f0f','#f48','#fa0','#cf0','#0fa','#0cf','#40f','#c0f',
    '#f66','#f96','#ff6','#9f6','#6f9','#6ff','#69f','#96f'
];

let lfoState = new Array(24).fill(null).map(() => ({
    active: false, wave: 0, div: 0, target: 0, depth: 50, freeHz: 2.0, retrig: false, phase: 0
}));
let lfoScopeData = new Float32Array(24); // -1..+1 current values
let lfoActiveMask = 0;   // 24-bit mask

function lfoSend(pad, cmd, extra) {
    sendWebSocket(Object.assign({cmd, pad}, extra));
}

function lfoSetActive(pad, on) {
    lfoState[pad].active = on;
    lfoSend(pad, 'lfoSetActive', {active: on});
    lfoUpdateCard(pad);
    lfoUpdateActiveCount();
}

function lfoSetWave(pad, w) {
    lfoState[pad].wave = w;
    lfoSend(pad, 'lfoSetWave', {wave: w});
    lfoUpdateCard(pad);
}

function lfoSetRate(pad, d) {
    lfoState[pad].div = d;
    lfoSend(pad, 'lfoSetRate', {division: d});
    lfoUpdateCard(pad);
}

function lfoSetDepth(pad, v) {
    lfoState[pad].depth = v;
    lfoSend(pad, 'lfoSetDepth', {depth: v});
}

function lfoSetTarget(pad, t) {
    lfoState[pad].target = t;
    lfoSend(pad, 'lfoSetTarget', {target: t});
    lfoUpdateCard(pad);
}

function lfoSetFreeHz(pad, hz) {
    lfoState[pad].freeHz = hz;
    lfoSend(pad, 'lfoSetFreeHz', {hz});
}

function lfoSetRetrigger(pad, on) {
    lfoState[pad].retrig = on;
    lfoSend(pad, 'lfoSetRetrigger', {retrigger: on});
}

function lfoResetAll() {
    sendWebSocket({cmd: 'lfoReset'});
}

function lfoRequestState() {
    sendWebSocket({cmd: 'lfoGetState'});
}

function lfoHandleFullState(pads) {
    for (let i = 0; i < Math.min(pads.length, 24); i++) {
        const p = pads[i];
        lfoState[i] = {
            active: p.active, wave: p.wave, div: p.div,
            target: p.target, depth: p.depth, freeHz: p.freeHz,
            retrig: p.retrig, phase: p.phase || 0
        };
        lfoUpdateCard(i);
    }
    lfoUpdateActiveCount();
}

function lfoHandleReset() {
    for (let i = 0; i < 24; i++) {
        lfoState[i] = {active: false, wave: 0, div: 0, target: 0, depth: 50, freeHz: 2.0, retrig: false, phase: 0};
        lfoUpdateCard(i);
    }
    lfoUpdateActiveCount();
}

function lfoUpdateActiveCount() {
    const cnt = lfoState.filter(l => l.active).length;
    const el = document.getElementById('lfoActiveCount');
    if (el) el.textContent = cnt;
}

function lfoUpdateCard(pad) {
    const card = document.getElementById(`lfoCard${pad}`);
    if (!card) return;
    const s = lfoState[pad];
    card.classList.toggle('lfo-active', s.active);
    
    const tog = card.querySelector('.lfo-toggle');
    if (tog) tog.checked = s.active;
    
    const wSel = card.querySelector('.lfo-wave-sel');
    if (wSel) wSel.value = s.wave;
    
    const dSel = card.querySelector('.lfo-div-sel');
    if (dSel) dSel.value = s.div;
    
    const tSel = card.querySelector('.lfo-target-sel');
    if (tSel) tSel.value = s.target;
    
    const depthSlider = card.querySelector('.lfo-depth-slider');
    if (depthSlider) depthSlider.value = s.depth;
    
    const depthVal = card.querySelector('.lfo-depth-val');
    if (depthVal) depthVal.textContent = s.depth + '%';
    
    const freeRow = card.querySelector('.lfo-free-row');
    if (freeRow) freeRow.style.display = (s.div === 4) ? 'flex' : 'none';
    
    const freeSlider = card.querySelector('.lfo-free-slider');
    if (freeSlider) freeSlider.value = s.freeHz;
    
    const retrig = card.querySelector('.lfo-retrig');
    if (retrig) retrig.checked = s.retrig;
}

// Build LFO pad cards
(function initLfoGrid() {
    const grid = document.getElementById('lfoGrid');
    if (!grid) return;
    const names = ['BD','SD','CH','OH','CY','CP','RS','CB','LT','MT','HT','MA','CL','HC','MC','LC',
                   'X1','X2','X3','X4','X5','X6','X7','X8'];
    for (let i = 0; i < 24; i++) {
        const card = document.createElement('div');
        card.className = 'lfo-card';
        card.id = `lfoCard${i}`;
        card.style.setProperty('--lfo-color', LFO_COLORS[i]);
        card.innerHTML = `
            <div class="lfo-card-header">
                <label class="lfo-toggle-label">
                    <input type="checkbox" class="lfo-toggle" onchange="lfoSetActive(${i},this.checked)">
                    <span class="lfo-pad-name">${names[i]}</span>
                </label>
            </div>
            <div class="lfo-card-body">
                <div class="lfo-row">
                    <select class="lfo-wave-sel" onchange="lfoSetWave(${i},+this.value)">
                        ${LFO_WAVES.map((w,j) => `<option value="${j}">${w}</option>`).join('')}
                    </select>
                    <select class="lfo-div-sel" onchange="lfoSetRate(${i},+this.value)">
                        ${LFO_DIVS.map((d,j) => `<option value="${j}">${d}</option>`).join('')}
                    </select>
                </div>
                <div class="lfo-row">
                    <select class="lfo-target-sel" onchange="lfoSetTarget(${i},+this.value)">
                        ${LFO_TARGETS.map((t,j) => `<option value="${j}">${t}</option>`).join('')}
                    </select>
                    <label class="lfo-retrig-label"><input type="checkbox" class="lfo-retrig" onchange="lfoSetRetrigger(${i},this.checked)"> Retrig</label>
                </div>
                <div class="lfo-row">
                    <span class="lfo-label">Depth</span>
                    <input type="range" class="lfo-depth-slider" min="0" max="100" value="50" oninput="lfoSetDepth(${i},+this.value);this.nextElementSibling.textContent=this.value+'%'">
                    <span class="lfo-depth-val">50%</span>
                </div>
                <div class="lfo-free-row" style="display:none">
                    <span class="lfo-label">Hz</span>
                    <input type="range" class="lfo-free-slider" min="0.1" max="20" step="0.1" value="2.0" oninput="lfoSetFreeHz(${i},+this.value)">
                </div>
            </div>`;
        grid.appendChild(card);
    }
})();

// Build LFO scope canvases
(function initLfoScopes() {
    const container = document.getElementById('lfoScopes');
    if (!container) return;
    const names = ['BD','SD','CH','OH','CY','CP','RS','CB','LT','MT','HT','MA','CL','HC','MC','LC',
                   'X1','X2','X3','X4','X5','X6','X7','X8'];
    for (let i = 0; i < 24; i++) {
        const wrap = document.createElement('div');
        wrap.className = 'lfo-scope-wrap';
        wrap.id = `lfoScopeWrap${i}`;
        wrap.style.display = 'none';
        wrap.innerHTML = `<span class="lfo-scope-label">${names[i]}</span><canvas class="lfo-scope-canvas" id="lfoScope${i}" width="100" height="40"></canvas>`;
        container.appendChild(wrap);
    }
})();

// LFO scope history buffers (30 samples each = 1.5s at 20fps)
const lfoScopeHistory = new Array(24).fill(null).map(() => []);

function handleLfoScopeBinary(view) {
    // 0xBB + 3 mask bytes + 24×int8 values = 28 bytes
    const m0 = view[1], m1 = view[2], m2 = view[3];
    lfoActiveMask = m0 | (m1 << 8) | (m2 << 16);
    for (let i = 0; i < 24; i++) {
        const raw = view[4 + i];
        lfoScopeData[i] = (raw > 127 ? raw - 256 : raw) / 127.0; // int8 → -1..+1
        if (lfoActiveMask & (1 << i)) {
            lfoScopeHistory[i].push(lfoScopeData[i]);
            if (lfoScopeHistory[i].length > 60) lfoScopeHistory[i].shift();
        }
    }
}

// LFO scope rendering at ~20fps (driven by requestAnimationFrame)
let _lfoScopeRafId = 0;
let _lfoScopeLastDraw = 0;
function lfoScopeLoop(ts) {
    _lfoScopeRafId = requestAnimationFrame(lfoScopeLoop);
    if (ts - _lfoScopeLastDraw < 50) return; // 20fps cap
    _lfoScopeLastDraw = ts;
    
    for (let i = 0; i < 24; i++) {
        const wrap = document.getElementById(`lfoScopeWrap${i}`);
        if (!wrap) continue;
        const active = !!(lfoActiveMask & (1 << i));
        wrap.style.display = active ? '' : 'none';
        if (!active) continue;
        
        const canvas = document.getElementById(`lfoScope${i}`);
        if (!canvas) continue;
        const ctx = canvas.getContext('2d');
        const W = canvas.width, H = canvas.height;
        const hist = lfoScopeHistory[i];
        
        ctx.clearRect(0, 0, W, H);
        
        // Center line
        ctx.strokeStyle = '#333';
        ctx.lineWidth = 0.5;
        ctx.beginPath();
        ctx.moveTo(0, H / 2);
        ctx.lineTo(W, H / 2);
        ctx.stroke();
        
        // Waveform
        if (hist.length < 2) continue;
        ctx.strokeStyle = LFO_COLORS[i];
        ctx.lineWidth = 1.5;
        ctx.beginPath();
        const step = W / (hist.length - 1);
        for (let j = 0; j < hist.length; j++) {
            const x = j * step;
            const y = H / 2 - hist[j] * (H / 2 - 2);
            j === 0 ? ctx.moveTo(x, y) : ctx.lineTo(x, y);
        }
        ctx.stroke();
    }
}
requestAnimationFrame(lfoScopeLoop);

// Request LFO state on tab switch
document.addEventListener('DOMContentLoaded', () => {
    const tabBtns = document.querySelectorAll('.tab-btn[data-tab]');
    tabBtns.forEach(btn => {
        btn.addEventListener('click', () => {
            if (btn.dataset.tab === 'lfo') lfoRequestState();
            if (btn.dataset.tab === 'daisy-sd') sdRefreshStatus();
        });
    });
});

