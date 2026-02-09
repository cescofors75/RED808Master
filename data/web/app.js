// RED808 Drum Machine - JavaScript Application

let ws = null;
let isConnected = false;
let currentStep = 0;
let tremoloIntervals = {};
let padLoopState = {};
let padFxState = new Array(16).fill(null); // Per-pad FX state
let trackFxState = new Array(16).fill(null); // Per-track FX state
let isPlaying = false;

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

// Pad hold timers for long press detection
let padHoldTimers = {};
let trackMutedState = new Array(16).fill(false);

// Pad filter state (stores active filter type for each pad)
let padFilterState = new Array(16).fill(0); // 0 = FILTER_NONE
let trackFilterState = new Array(16).fill(0); // 0 = FILTER_NONE

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
    { icon: '⚡', name: 'RESONANT' },
    { icon: '💿', name: 'SCRATCH', padOnly: true },
    { icon: '🎧', name: 'TURNTABLISM', padOnly: true }
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
    
    // Initialize keyboard system from keyboard-controls.js first
    if (window.initKeyboardControls) {
        window.initKeyboardControls();
    }
    
    setupKeyboardControls(); // Then setup pad handlers in app.js
    initSampleBrowser();
    initInstrumentTabs();
    initTabSystem(); // Tab navigation system
});

// WebSocket Connection
function initWebSocket() {
    const wsUrl = `ws://${window.location.hostname}/ws`;
    ws = new WebSocket(wsUrl);
    
    ws.onopen = () => {
        isConnected = true;
        updateStatus(true);
        syncLedMonoMode();
        
        setTimeout(() => { sendWebSocket({ cmd: 'init' }); }, 100);
        setTimeout(() => { sendWebSocket({ cmd: 'getPattern' }); }, 300);
        setTimeout(() => { requestSampleCounts(); }, 1000);
    };
    
    ws.onclose = () => {
        isConnected = false;
        updateStatus(false);
        setTimeout(initWebSocket, 3000);
    };
    
    ws.onerror = (error) => {
        console.error('WebSocket Error:', error);
    };
    
    ws.onmessage = (event) => {
        const data = JSON.parse(event.data);
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
            }
            break;
        case 'audioData':
            break;
        case 'state':
            updateSequencerState(data);
            updateDeviceStats(data);
            if (Array.isArray(data.samples)) {
                applySampleMetadataFromState(data.samples);
            }
            // Load pad filter states
            if (Array.isArray(data.padFilters)) {
                data.padFilters.forEach((filterType, padIndex) => {
                    if (padIndex < 16) {
                        padFilterState[padIndex] = filterType;
                        updatePadFilterIndicator(padIndex);
                    }
                });
            }
            // Load track filter states
            if (Array.isArray(data.trackFilters)) {
                data.trackFilters.forEach((filterType, trackIndex) => {
                    if (trackIndex < 16) {
                        trackFilterState[trackIndex] = filterType;
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
            break;
        case 'trackFilterSet':
            if (data.success) {
                const trackName = padNames[data.track] || `Track ${data.track + 1}`;
                if (window.showToast) {
                    window.showToast(`✅ Filtro aplicado a ${trackName}`, window.TOAST_TYPES.SUCCESS, 1500);
                }
                
                // Create or update badge on track label
                const trackLabel = document.querySelector(`.track-label[data-track="${data.track}"]`);
                if (trackLabel && data.filterType !== undefined) {
                    // Remove ALL existing badges from this track to avoid duplicates
                    trackLabel.querySelectorAll('.track-filter-badge').forEach(b => b.remove());
                    
                    // Only create badge if filter type is NOT 0 (NONE) and is valid
                    if (data.filterType > 0 && data.filterType < 10) {
                        // Create new badge with icon + initials
                        const badge = document.createElement('div');
                        badge.className = 'track-filter-badge';
                        const filterIcons = ['⭕', '🔽', '🔼', '🎯', '🚫', '📊', '📈', '⛰️', '🌀', '💫'];
                        const filterInitials = ['', 'LP', 'HP', 'BP', 'NT', 'LS', 'HS', 'PK', 'AP', 'RS'];
                        badge.innerHTML = `${filterIcons[data.filterType]} <span class="badge-initials">${filterInitials[data.filterType]}</span>`;
                        trackLabel.appendChild(badge);
                    }
                }
            }
            break;
        case 'trackFilterCleared':
            if (window.showToast) {
                const trackName = padNames[data.track] || `Track ${data.track + 1}`;
                window.showToast(`🔄 Filtro eliminado de ${trackName}`, window.TOAST_TYPES.INFO, 1500);
            }
            
            // Remove ALL badges from track label
            const trackLabel = document.querySelector(`.track-label[data-track="${data.track}"]`);
            if (trackLabel) {
                trackLabel.querySelectorAll('.track-filter-badge').forEach(badge => badge.remove());
            }
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
    }
    
    // Call keyboard controls handler if function exists
    if (typeof window.handleKeyboardWebSocketMessage === 'function') {
        window.handleKeyboardWebSocketMessage(data);
    }
}

function loadPatternData(data) {
    // Limpiar sequencer
    document.querySelectorAll('.seq-step').forEach(el => {
        el.classList.remove('active');
    });
    
    // Clear circular data
    circularSequencerData = Array.from({ length: 16 }, () => Array(16).fill(false));
    
    // Cargar datos del pattern (16 tracks)
    let activatedSteps = 0;
    for (let track = 0; track < 16; track++) {
        // Las keys pueden ser strings o números
        const trackData = data[track] || data[track.toString()];
        if (trackData) {
            let trackSteps = 0;
            trackData.forEach((active, step) => {
                if (active) {
                    const stepEl = document.querySelector(`[data-track="${track}"][data-step="${step}"]`);
                    if (stepEl) {
                        stepEl.classList.add('active');
                        activatedSteps++;
                        trackSteps++;
                    } else if (track >= 8) {
                    }
                    // Update circular data
                    if (circularSequencerData[track]) {
                        circularSequencerData[track][step] = true;
                    }
                }
            });

        }
    }
    
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
    const loopBtn = document.querySelector(`.loop-btn[data-pad="${padIndex}"]`);
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
        <span class="pfe-pad-name">🎛️ ${padNames[padIndex]}</span>
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
    
    const currentFx = padFxState[padIndex] || {};
    const distortion = currentFx.distortion || 0;
    const distMode = currentFx.distMode || 0;
    const bitcrush = currentFx.bitcrush || 16;
    
    const padName = padNames[padIndex] || `Pad ${padIndex + 1}`;
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
                        <button class="loop-type-btn pad-fx-mode-btn ${m.id === distMode ? 'active' : ''}" 
                                data-mode="${m.id}" onclick="setPadFxDistMode(${padIndex}, ${m.id})">
                            <span class="loop-type-icon">${m.icon}</span>
                            <span class="loop-type-name">${m.name}</span>
                        </button>
                    `).join('')}
                </div>
                <div class="pad-fx-slider-row">
                    <label>Drive <span id="padFxDriveVal">${distortion}</span>%</label>
                    <input type="range" id="padFxDrive" min="0" max="100" value="${distortion}" 
                           oninput="setPadFxDrive(${padIndex}, this.value)" class="fx-slider">
                </div>
            </div>
            <div class="pad-fx-section">
                <h4>📼 BIT CRUSH</h4>
                <div class="pad-fx-slider-row">
                    <label>Bits <span id="padFxBitsVal">${bitcrush}</span></label>
                    <input type="range" id="padFxBits" min="4" max="16" value="${bitcrush}" 
                           oninput="setPadFxBits(${padIndex}, this.value)" class="fx-slider">
                </div>
            </div>
            <button class="pad-fx-clear-btn" onclick="clearPadFxAll(${padIndex})">🚫 CLEAR ALL FX</button>
        </div>
    `;
    
    document.body.appendChild(backdrop);
    document.body.appendChild(popup);
    requestAnimationFrame(() => {
        backdrop.classList.add('visible');
        popup.classList.add('visible');
    });
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

function clearPadFxAll(padIndex) {
    padFxState[padIndex] = null;
    sendWebSocket({ cmd: 'clearPadFX', pad: padIndex });
    updatePadFxIndicator(padIndex);
    closePadFxPopup();
    // Sync to track if enabled
    if (padSeqSyncEnabled && padIndex < 16) {
        trackFxState[padIndex] = null;
        sendWebSocket({ cmd: 'clearTrackFX', track: padIndex });
    }
}

function updatePadFxIndicator(padIndex) {
    const pad = document.querySelector(`.pad[data-pad="${padIndex}"]`);
    if (!pad) return;
    let badge = pad.querySelector('.pad-fx-badge');
    const fx = padFxState[padIndex];
    const hasFx = fx && ((fx.distortion && fx.distortion > 0) || (fx.bitcrush && fx.bitcrush < 16));
    
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

// Track FX functions (same concept but for sequencer tracks)
function showTrackFxPopup(trackIndex) {
    closePadFxPopup(); // Reuse close
    
    const backdrop = document.createElement('div');
    backdrop.id = 'padFxBackdrop';
    backdrop.className = 'loop-popup-backdrop';
    backdrop.addEventListener('click', closePadFxPopup);
    
    const currentFx = trackFxState[trackIndex] || {};
    const distortion = currentFx.distortion || 0;
    const distMode = currentFx.distMode || 0;
    const bitcrush = currentFx.bitcrush || 16;
    
    const trackName = padNames[trackIndex] || `Track ${trackIndex + 1}`;
    const popup = document.createElement('div');
    popup.id = 'padFxModal';
    popup.className = 'pad-fx-modal';
    popup.innerHTML = `
        <div class="loop-popup-header">
            <span class="loop-popup-title">🎸 TRACK FX: ${trackName}</span>
            <button class="loop-popup-close" onclick="closePadFxPopup()">&times;</button>
        </div>
        <div class="pad-fx-content">
            <div class="pad-fx-section">
                <h4>🎸 DISTORTION</h4>
                <div class="pad-fx-modes">
                    ${DISTORTION_MODES.map(m => `
                        <button class="loop-type-btn pad-fx-mode-btn ${m.id === distMode ? 'active' : ''}" 
                                data-mode="${m.id}" onclick="setTrackFxDistMode(${trackIndex}, ${m.id})">
                            <span class="loop-type-icon">${m.icon}</span>
                            <span class="loop-type-name">${m.name}</span>
                        </button>
                    `).join('')}
                </div>
                <div class="pad-fx-slider-row">
                    <label>Drive <span id="padFxDriveVal">${distortion}</span>%</label>
                    <input type="range" id="padFxDrive" min="0" max="100" value="${distortion}" 
                           oninput="setTrackFxDrive(${trackIndex}, this.value)" class="fx-slider">
                </div>
            </div>
            <div class="pad-fx-section">
                <h4>📼 BIT CRUSH</h4>
                <div class="pad-fx-slider-row">
                    <label>Bits <span id="padFxBitsVal">${bitcrush}</span></label>
                    <input type="range" id="padFxBits" min="4" max="16" value="${bitcrush}" 
                           oninput="setTrackFxBits(${trackIndex}, this.value)" class="fx-slider">
                </div>
            </div>
            <button class="pad-fx-clear-btn" onclick="clearTrackFxAll(${trackIndex})">🚫 CLEAR ALL FX</button>
        </div>
    `;
    
    document.body.appendChild(backdrop);
    document.body.appendChild(popup);
    requestAnimationFrame(() => {
        backdrop.classList.add('visible');
        popup.classList.add('visible');
    });
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
    sendWebSocket({ cmd: 'clearTrackFX', track: trackIndex });
    closePadFxPopup();
    // Sync to pad if enabled
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
window.clearPadFxAll = clearPadFxAll;
window.setTrackFxDistMode = setTrackFxDistMode;
window.setTrackFxDrive = setTrackFxDrive;
window.setTrackFxBits = setTrackFxBits;
window.clearTrackFxAll = clearTrackFxAll;

// Update pad filter indicator visual
function updatePadFilterIndicator(padIndex) {
    const indicator = document.querySelector(`.pad-filter-indicator[data-pad="${padIndex}"]`);
    if (indicator) {
        const filterType = padFilterState[padIndex];
        if (filterType > 0) {
            indicator.style.display = 'flex';
            const filter = FILTER_TYPES[filterType];
            indicator.innerHTML = `
                <span class="filter-icon">${filter.icon}</span>
                <span class="filter-name">${filter.name}</span>
            `;
        } else {
            indicator.style.display = 'none';
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
    sampleList.forEach(sample => {
        const padIndex = sample.pad;
        if (typeof padIndex !== 'number' || padIndex < 0 || padIndex >= padNames.length) {
            return;
        }
        if (sample.loaded && sample.name) {
            const sizeBytes = typeof sample.size === 'number' ? sample.size : 0;
            padSampleMetadata[padIndex] = {
                filename: sample.name,
                sizeKB: (sizeBytes / 1024).toFixed(1),
                format: sample.format ? sample.format.toUpperCase() : inferFormatFromName(sample.name),
                quality: sample.quality || DEFAULT_SAMPLE_QUALITY
            };
        } else {
            padSampleMetadata[padIndex] = null;
        }
        refreshPadSampleInfo(padIndex);
    });
    scheduleSampleBrowserRender();
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
    // Enviar al ESP32 (Protocolo Binario para baja latencia)
    if (ws && ws.readyState === WebSocket.OPEN) {
        const data = new Uint8Array(3);
        data[0] = 0x90; // Comando Trigger (0x90)
        data[1] = padIndex;
        data[2] = 127;  // Velocity
        ws.send(data);
    }
}



function flashPad(padIndex) {
    const pad = document.querySelector(`[data-pad="${padIndex}"]`);
    if (pad) {
        pad.classList.add('triggered');
        setTimeout(() => pad.classList.remove('triggered'), 600);
    }
    
    // También resaltar la fila en el secuenciador
    document.querySelectorAll(`.seq-step[data-track="${padIndex}"]`).forEach(step => {
        step.style.borderColor = '#fff';
        setTimeout(() => step.style.borderColor = '', 200);
    });
}

function updatePadLoopVisual(padIndex) {
    const pad = document.querySelector(`[data-pad="${padIndex}"]`);
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
        
        const name = document.createElement('span');
        name.textContent = trackNames[track];
        name.style.color = trackColors[track];

        const loopIndicator = document.createElement('span');
        loopIndicator.className = 'loop-indicator';
        loopIndicator.textContent = 'LOOP';
        
        label.appendChild(volumeBtn);
        label.appendChild(name);
        label.appendChild(loopIndicator);
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
        trackFxBtn.textContent = '🎸';
        trackFxBtn.addEventListener('click', (e) => {
            e.stopPropagation();
            showTrackFxPopup(track);
        });
        
        fxCell.appendChild(filterBtn);
        fxCell.appendChild(trackFxBtn);
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
}

function toggleStep(track, step, element) {
    const isActive = element.classList.toggle('active');
    
    // Update circular data
    if (circularSequencerData[track]) {
        circularSequencerData[track][step] = isActive;
    }
    
    sendWebSocket({
        cmd: 'setStep',
        track: track,
        step: step,
        active: isActive
    });
}

function updateCurrentStep(step) {
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

    lastCurrentStep = step;
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

function updateSequencerStatusMeter() {
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

// Send WebSocket message
function sendWebSocket(data) {
    if (ws && ws.readyState === WebSocket.OPEN) {
        ws.send(JSON.stringify(data));
    }
}

// Export to window for keyboard-controls.js
window.sendWebSocket = sendWebSocket;

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
    const tabContents = document.querySelectorAll('.tab-content');
    
    // Cargar el tab guardado
    const savedTab = localStorage.getItem('currentTab');
    if (savedTab) {
        switchTab(savedTab);
    }
    
    // Event listeners para los botones de tabs
    tabBtns.forEach(btn => {
        btn.addEventListener('click', () => {
            const tabId = btn.getAttribute('data-tab');
            switchTab(tabId);
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
            <div class="sample-list"></div>
            <button class="btn-close-modal">Close</button>
        </div>
    `;
    
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
            loadSampleToPad(padIndex, family, sample.name);
            const modalToRemove = modal;
            if (modalToRemove && modalToRemove.parentNode) {
                modalToRemove.parentNode.removeChild(modalToRemove);
            }
            sampleSelectorContext = null;
        });
        sampleList.appendChild(sampleItem);
    });
    
    modal.querySelector('.btn-close-modal').addEventListener('click', () => {
        const modalToRemove = modal;
        if (modalToRemove && modalToRemove.parentNode) {
            modalToRemove.parentNode.removeChild(modalToRemove);
        }
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

function loadSampleToPad(padIndex, family, filename, autoPlay = false) {
    if (autoPlay) {
        pendingAutoPlayPad = padIndex;
        setTimeout(() => {
            if (pendingAutoPlayPad === padIndex) {
                triggerPad(padIndex);
            }
        }, 350);
    }
    sendWebSocket({
        cmd: 'loadSample',
        family: family,
        filename: filename,
        pad: padIndex
    });
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
        
        // Create or update badge on track label
        const trackLabel = document.querySelector(`.track-label[data-track="${track}"]`);
        if (trackLabel) {
            trackLabel.querySelectorAll('.track-filter-badge').forEach(b => b.remove());
            
            if (filterType > 0 && filterType < 10) {
                const badge = document.createElement('div');
                badge.className = 'track-filter-badge';
                const filterInitials = ['', 'LP', 'HP', 'BP', 'NT', 'BS', 'TB', 'PK', 'PH', 'RS'];
                badge.innerHTML = `${filterIcons[filterType]} <span class="badge-initials">${filterInitials[filterType]}</span>`;
                badge.style.borderColor = filterColors[filterType];
                badge.style.boxShadow = `0 0 6px ${filterColors[filterType]}40`;
                trackLabel.appendChild(badge);
                // Pulse animation on apply
                trackLabel.classList.add('filter-applied-pulse');
                setTimeout(() => trackLabel.classList.remove('filter-applied-pulse'), 600);
            }
        }
        
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
    const percent = Math.round((velocity / 127) * 100);
    velocityFill.style.width = `${percent}%`;
    
    // Reset after 500ms
    setTimeout(() => {
        velocityFill.style.width = '0%';
    }, 500);
}

function highlightMappingItem(note) {
    const item = document.querySelector(`.mapping-item[data-note="${note}"]`);
    if (!item) return;
    
    item.classList.add('active');
    setTimeout(() => {
        item.classList.remove('active');
    }, 300);
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

async function loadMIDIMapping() {
    try {
        const response = await fetch('/api/midi/mapping');
        const data = await response.json();
        
        if (data.mappings) {
            // Actualizar el grid con los mapeos actuales
            data.mappings.forEach(mapping => {
                const item = document.querySelector(`.mapping-item[data-pad="${mapping.pad}"]`);
                if (item) {
                    const badge = item.querySelector('.note-badge');
                    badge.textContent = mapping.note;
                    item.dataset.note = mapping.note;
                    
                    // Actualizar nombre de nota
                    const noteName = getNoteNameFromNumber(mapping.note);
                    item.querySelector('.note-name').textContent = noteName;
                }
            });
            
        }
    } catch (error) {
        console.error('[MIDI Mapping] Error loading:', error);
    }
}

function toggleMappingEdit() {
    isEditingMapping = !isEditingMapping;
    
    const editBtn = document.getElementById('editMappingBtn');
    const resetBtn = document.getElementById('resetMappingBtn');
    const saveBtn = document.getElementById('saveMappingBtn');
    const badges = document.querySelectorAll('.note-badge.editable');
    const mappingGrid = document.getElementById('mappingGrid');
    
    if (isEditingMapping) {
        // Modo edición activado
        editBtn.style.display = 'none';
        resetBtn.style.display = 'inline-block';
        saveBtn.style.display = 'inline-block';
        mappingGrid.classList.add('editing');
        
        // Guardar valores originales
        badges.forEach(badge => {
            const item = badge.closest('.mapping-item');
            originalMappings[item.dataset.pad] = badge.textContent;
            badge.contentEditable = 'true';
            badge.classList.add('editing');
        });
        
        if (window.showToast) {
            window.showToast('✏️ Modo edición activado - Haz clic en las notas para editarlas', window.TOAST_TYPES.INFO, 3000);
        }
    } else {
        // Cancelar edición
        editBtn.style.display = 'inline-block';
        resetBtn.style.display = 'none';
        saveBtn.style.display = 'none';
        mappingGrid.classList.remove('editing');
        
        // Restaurar valores originales
        badges.forEach(badge => {
            const item = badge.closest('.mapping-item');
            badge.textContent = originalMappings[item.dataset.pad];
            badge.contentEditable = 'false';
            badge.classList.remove('editing');
        });
        
        originalMappings = {};
    }
}

async function saveMIDIMapping() {
    const badges = document.querySelectorAll('.note-badge.editable');
    const mappings = [];
    let hasErrors = false;
    
    // Validar y recopilar mappings
    badges.forEach(badge => {
        const item = badge.closest('.mapping-item');
        const pad = parseInt(item.dataset.pad);
        const note = parseInt(badge.textContent.trim());
        
        if (isNaN(note) || note < 0 || note > 127) {
            badge.classList.add('error');
            hasErrors = true;
            return;
        }
        
        badge.classList.remove('error');
        mappings.push({ note, pad });
    });
    
    if (hasErrors) {
        if (window.showToast) {
            window.showToast('❌ Notas inválidas (deben ser 0-127)', window.TOAST_TYPES.ERROR, 3000);
        }
        return;
    }
    
    // Enviar cada mapeo al servidor
    try {
        for (const mapping of mappings) {
            const response = await fetch('/api/midi/mapping', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify(mapping)
            });
            
            if (!response.ok) {
                throw new Error(`Failed to save mapping for pad ${mapping.pad}`);
            }
        }
        
        // Actualizar nombres de notas
        badges.forEach(badge => {
            const note = parseInt(badge.textContent.trim());
            const item = badge.closest('.mapping-item');
            const noteName = getNoteNameFromNumber(note);
            item.querySelector('.note-name').textContent = noteName;
            item.dataset.note = note;
        });
        
        // Salir del modo edición
        toggleMappingEdit();
        
        if (window.showToast) {
            window.showToast('✅ Mapeo MIDI guardado correctamente', window.TOAST_TYPES.SUCCESS, 3000);
        }
    } catch (error) {
        console.error('[MIDI Mapping] Error saving:', error);
        if (window.showToast) {
            window.showToast('❌ Error al guardar mapeo', window.TOAST_TYPES.ERROR, 3000);
        }
    }
}

async function resetMIDIMapping() {
    if (!confirm('¿Resetear el mapeo MIDI a los valores por defecto (36-43)?')) {
        return;
    }
    
    try {
        const response = await fetch('/api/midi/mapping', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ reset: true })
        });
        
        if (response.ok) {
            // Recargar mapeo desde el servidor
            await loadMIDIMapping();
            
            // Salir del modo edición
            if (isEditingMapping) {
                toggleMappingEdit();
            }
            
            if (window.showToast) {
                window.showToast('🔄 Mapeo MIDI reseteado a valores por defecto', window.TOAST_TYPES.SUCCESS, 3000);
            }
        } else {
            throw new Error('Reset failed');
        }
    } catch (error) {
        console.error('[MIDI Mapping] Error resetting:', error);
        if (window.showToast) {
            window.showToast('❌ Error al resetear mapeo', window.TOAST_TYPES.ERROR, 3000);
        }
    }
}

// Cargar mapeo al iniciar la página MIDI
document.addEventListener('DOMContentLoaded', () => {
    // Cargar mapeo cuando se abre la tab MIDI
    const midiTab = document.querySelector('[data-tab="midi"]');
    if (midiTab) {
        midiTab.addEventListener('click', () => {
            setTimeout(loadMIDIMapping, 100);
        });
    }
    
    // Cargar inmediatamente si ya estamos en la tab MIDI
    if (window.location.hash === '#midi' || document.getElementById('tab-midi')?.classList.contains('active')) {
        setTimeout(loadMIDIMapping, 500);
    }
});

// Export to window
window.applyFilterPreset = applyFilterPreset;

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

function showXtraPadPicker() {
    // Modal to pick which of the 16 instruments to map
    const modal = document.createElement('div');
    modal.className = 'sample-modal';
    modal.innerHTML = `
        <div class="sample-modal-content">
            <h3>🎲 New XTRA Pad</h3>
            <p style="color:#aaa;margin:0 0 12px;font-size:12px;">Pick an instrument to clone as an independent pad</p>
            <div class="xtra-picker-grid" style="display:grid;grid-template-columns:repeat(4,1fr);gap:8px;margin-bottom:14px;"></div>
            <button class="btn-close-modal">Cancel</button>
        </div>
    `;

    const pickerGrid = modal.querySelector('.xtra-picker-grid');

    for (let i = 0; i < 16; i++) {
        const btn = document.createElement('button');
        btn.style.cssText = 'padding:10px 4px;border:1px solid #555;border-radius:6px;background:#1a1a2e;color:#fff;cursor:pointer;font-size:13px;font-weight:bold;transition:all .15s;';
        const sampleMeta = padSampleMetadata[i];
        const label = padNames[i];
        const sub = sampleMeta && sampleMeta.filename ? sampleMeta.filename : '...';
        btn.innerHTML = `<div>${label}</div><div style="font-size:9px;color:#888;margin-top:2px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;">${sub}</div>`;
        btn.addEventListener('mouseenter', () => { btn.style.background = '#ff6600'; btn.style.color = '#000'; });
        btn.addEventListener('mouseleave', () => { btn.style.background = '#1a1a2e'; btn.style.color = '#fff'; });
        btn.addEventListener('click', () => {
            createXtraPad(i, padNames[i]);
            modal.remove();
        });
        pickerGrid.appendChild(btn);
    }

    modal.querySelector('.btn-close-modal').addEventListener('click', () => modal.remove());
    modal.addEventListener('click', (e) => { if (e.target === modal) modal.remove(); });
    document.body.appendChild(modal);
}

function createXtraPad(padIndex, family) {
    const grid = document.getElementById('padsXtraGrid');
    if (!grid) return;

    const id = ++xtraPadCounter;
    const sampleMeta = padSampleMetadata[padIndex];
    const filename = sampleMeta ? sampleMeta.filename : '...';

    const padEl = document.createElement('div');
    padEl.className = 'pad-xtra';
    padEl.dataset.xtraId = id;
    padEl.dataset.padIndex = padIndex;
    padEl.innerHTML = `
        <div class="pad-xtra-name">${family}</div>
        <div class="pad-xtra-sample" title="${filename}">${filename}</div>
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
    loopBtn.addEventListener('click', (e) => { e.stopPropagation(); showLoopTypePopup(padIndex); });

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
    xtraPads.push({ id, padIndex, family, filename, element: padEl });

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
