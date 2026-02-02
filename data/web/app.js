// RED808 Drum Machine - JavaScript Application

let ws = null;
let isConnected = false;
let currentStep = 0;
let tremoloIntervals = {};
let padLoopState = {};
let isRecording = false;
let recordedSteps = [];
let recordStartTime = 0;

// Sequencer caches
let stepDots = [];
let stepColumns = Array.from({ length: 16 }, () => []);
let lastCurrentStep = null;

// Visualizer data
let spectrumData = new Array(64).fill(0);
let waveformData = new Array(128).fill(0);
let isVisualizerActive = true;
let visualizerNeedsRedraw = true;
let lastVisualizerFrameTime = 0;
const VISUALIZER_MAX_FPS = 30;
const VISUALIZER_IDLE_FPS = 8;

// Sample counts per family
let sampleCounts = {};

// Keyboard state
let keyboardPadsActive = {};
let keyboardHoldTimers = {};
let keyboardTremoloState = {};

// Pad hold timers for long press detection
let padHoldTimers = {};
let trackMutedState = new Array(8).fill(false);

// Pad filter state (stores active filter type for each pad)
let padFilterState = new Array(8).fill(0); // 0 = FILTER_NONE

// 8 instrumentos principales
const padNames = ['BD', 'SD', 'CH', 'OH', 'CP', 'RS', 'CL', 'CY'];

// Tecla asociada a cada pad (mostrar en UI y para accesos directos)
const padKeyBindings = ['1', '2', '3', '4', '5', '6', '7', '8'];

// Descripción completa de cada instrumento
const padDescriptions = [
    'Bass Drum (Bombo)',
    'Snare Drum (Caja)',
    'Closed Hi-Hat',
    'Open Hi-Hat',
    'Hand Clap (Palmas)',
    'Rim Shot (Aro)',
    'Claves',
    'Cymbal (Platillo)'
];

const filterTypeLabels = {
    0: 'OFF',
    1: 'LOW PASS',
    2: 'HIGH PASS',
    3: 'BAND PASS',
    4: 'NOTCH'
};

// Filter types for track filter panel
const FILTER_TYPES = [
    { icon: '⭕', name: 'NONE' },
    { icon: '🔽', name: 'LOW PASS' },
    { icon: '🔼', name: 'HIGH PASS' },
    { icon: '🎯', name: 'BAND PASS' },
    { icon: '🚫', name: 'NOTCH' },
    { icon: '📊', name: 'LOW SHELF' },
    { icon: '📈', name: 'HIGH SHELF' },
    { icon: '⛰️', name: 'PEAK' },
    { icon: '🌀', name: 'ALL PASS' },
    { icon: '💫', name: 'RESONANT' }
];
window.FILTER_TYPES = FILTER_TYPES;

const instrumentPalette = [
    '#ff6b6b', '#f7b731', '#26de81', '#45aaf2',
    '#a55eea', '#fd9644', '#2bcbba', '#778ca3'
];

const padSampleMetadata = new Array(8).fill(null);
const DEFAULT_SAMPLE_QUALITY = '44.1kHz • 16-bit mono';
const sampleCatalog = {};
let sampleSelectorContext = null;
let pendingAutoPlayPad = null;
let activeSampleFilter = 'ALL';
let sampleBrowserRenderTimer = null;
let sampleRequestTimers = [];
let sampleRetryTimer = null;

// Simple notification function
function showNotification(message) {
    console.log('[Notification]', message);
}

// Initialize
document.addEventListener('DOMContentLoaded', () => {
    initWebSocket();
    createPads();
    createSequencer();
    setupControls();
    initHeaderMeters();
    initVisualizers();
    
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
        console.log('✅ WebSocket Connected');
        isConnected = true;
        updateStatus(true);
        syncLedMonoMode();
        
        // Solicitar inicialización completa de forma controlada
        console.log('[WS] Requesting initialization...');
        setTimeout(() => {
            sendWebSocket({ cmd: 'init' });
        }, 100);
        
        // Solicitar patrón después de init (ahora no se envía automáticamente)
        setTimeout(() => {
            console.log('[WS] Requesting pattern...');
            sendWebSocket({ cmd: 'getPattern' });
        }, 300);
        
        // Solicitar samples de forma lazy (no bloquea UI)
        setTimeout(() => {
            console.log('[WS] Requesting sample counts (lazy)...');
            requestSampleCounts();
        }, 1000);
    };
    
    ws.onclose = () => {
        console.log('WebSocket Disconnected');
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
                paused: data.paused
            };
            updatePadLoopVisual(data.track);
            break;
        case 'audioData':
            // Audio visualization disabled
            // if (data.spectrum) {
            //     spectrumData = data.spectrum;
            //     visualizerNeedsRedraw = true;
            // }
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
                    if (padIndex < 8) {
                        padFilterState[padIndex] = filterType;
                        updatePadFilterIndicator(padIndex);
                    }
                });
            }
            break;
        case 'step':
            updateCurrentStep(data.step);
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
                        document.getElementById('currentPatternName').textContent = btn.textContent.trim();
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
            console.log(`Track ${data.track} filter set, active filters: ${data.activeFilters}`);
            break;
        case 'trackFilterCleared':
            if (window.showToast) {
                const trackName = padNames[data.track] || `Track ${data.track + 1}`;
                window.showToast(`🔄 Filtro eliminado de ${trackName}`, window.TOAST_TYPES.INFO, 1500);
            }
            console.log(`Track ${data.track} filter cleared, active filters: ${data.activeFilters}`);
            
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
            console.log(`Pad ${data.pad} filter set, active filters: ${data.activeFilters}`);
            break;
        case 'padFilterCleared':
            if (window.showToast) {
                const padName = padNames[data.pad] || `Pad ${data.pad + 1}`;
                window.showToast(`🔄 Filtro eliminado de ${padName}`, window.TOAST_TYPES.INFO, 1500);
            }
            console.log(`Pad ${data.pad} filter cleared, active filters: ${data.activeFilters}`);
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
            console.log(`Step velocity set: Track ${data.track}, Step ${data.step}, Velocity ${data.velocity}`);
            break;
        case 'stepVelocity':
            // Response to getStepVelocity query
            console.log(`Step velocity: Track ${data.track}, Step ${data.step} = ${data.velocity}`);
            break;
        case 'filterPresets':
            // Store filter presets for future use
            if (data.presets) {
                window.filterPresets = data.presets;
                console.log(`Received ${data.presets.length} filter presets`);
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
    console.log('loadPatternData called, data keys:', Object.keys(data));
    
    // Limpiar sequencer
    document.querySelectorAll('.seq-step').forEach(el => {
        el.classList.remove('active');
    });
    
    // Cargar datos del pattern (8 tracks)
    let activatedSteps = 0;
    for (let track = 0; track < 8; track++) {
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
                        console.warn(`Step element not found for track ${track}, step ${step}`);
                    }
                }
            });
            if (track >= 8 && trackSteps > 0) {
                console.log(`Track ${track} (${padNames[track]}): ${trackSteps} steps activated`);
            }
        }
    }
    
    // Cargar velocidades si están disponibles
    if (data.velocities) {
        for (let track = 0; track < 8; track++) {
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
        console.log('Velocities loaded from pattern data');
    }
    
    console.log(`Total steps activated: ${activatedSteps}`);
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

// Create Pads
function createPads() {
    const grid = document.getElementById('padsGrid');
    
    const families = padNames;
    
    for (let i = 0; i < 8; i++) {
        const padContainer = document.createElement('div');
        padContainer.className = 'pad-container';
        
        const pad = document.createElement('div');
        pad.className = 'pad';
        pad.dataset.pad = i;
        
        pad.innerHTML = `
            <button class="pad-upload-btn" data-pad="${i}" title="Load Sample">+</button>
            <button class="pad-filter-btn" data-pad="${i}" title="Filter">F</button>
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
            uploadBtn.addEventListener('click', (e) => {
                e.preventDefault();
                e.stopPropagation();
                showUploadDialog(i);
            });
        }
        
        // Event listener para botón F de filtro
        const filterBtn = pad.querySelector('.pad-filter-btn');
        if (filterBtn) {
            filterBtn.addEventListener('click', (e) => {
                e.preventDefault();
                e.stopPropagation();
                showPadFilterSelector(i, pad);
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
    // Trigger inicial con animación intensa y más brillo
    triggerPad(padIndex);
    padElement.style.animation = 'padRipple 0.35s ease-out';
    
    // Limpiar animación después
    setTimeout(() => {
        padElement.style.animation = '';
    }, 350);
    
    // Tremolo: triggers repetidos cada 180ms (reducido para evitar saturación)
    tremoloIntervals[padIndex] = setTimeout(() => {
        // Añadir clase para mantener brillo constante
        padElement.classList.add('tremolo-active');
        
        tremoloIntervals[padIndex] = setInterval(() => {
            triggerPad(padIndex);
            // Flash sutil en cada trigger
            padElement.style.filter = 'brightness(1.3)';
            setTimeout(() => {
                padElement.style.filter = 'brightness(1.1)';
            }, 60);
        }, 180); // Tremolo cada 180ms (~5.5 triggers/segundo)
    }, 300);
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

    const state = {
        startTime: Date.now(),
        currentRate: 220,
        minRate: 60,
        timeoutId: null
    };
    keyboardTremoloState[padIndex] = state;
    padElement.classList.add('keyboard-tremolo');

    const tick = () => {
        triggerPad(padIndex);
        padElement.classList.add('active');
        padElement.style.filter = 'brightness(1.4)';
        setTimeout(() => {
            padElement.style.filter = 'brightness(1.1)';
        }, 60);

        const elapsed = Date.now() - state.startTime;
        const rampFactor = Math.max(state.minRate, Math.round(220 * Math.pow(0.93, elapsed / 200)));
        state.currentRate = rampFactor;
        state.timeoutId = setTimeout(tick, state.currentRate);
    };

    tick();
}

function stopKeyboardTremolo(padIndex, padElement) {
    const state = keyboardTremoloState[padIndex];
    if (state && state.timeoutId) {
        clearTimeout(state.timeoutId);
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
    // Remove any existing overlay
    const existingOverlay = document.querySelector('.pad-filter-overlay');
    if (existingOverlay) {
        existingOverlay.remove();
    }
    
    // Create overlay
    const overlay = document.createElement('div');
    overlay.className = 'pad-filter-overlay';
    
    // Create filter grid (10 filters in 3x4 grid)
    const filterGrid = document.createElement('div');
    filterGrid.className = 'pad-filter-grid';
    
    FILTER_TYPES.forEach((filter, index) => {
        const filterBtn = document.createElement('button');
        filterBtn.className = 'pad-filter-option';
        filterBtn.dataset.filterType = index;
        if (index === padFilterState[padIndex]) {
            filterBtn.classList.add('active');
        }
        
        filterBtn.innerHTML = `
            <span class="filter-icon">${filter.icon}</span>
            <span class="filter-name">${filter.name}</span>
        `;
        
        filterBtn.addEventListener('click', (e) => {
            e.stopPropagation();
            setPadFilter(padIndex, index);
            overlay.remove();
        });
        
        filterGrid.appendChild(filterBtn);
    });
    
    overlay.appendChild(filterGrid);
    
    // Close overlay when clicking outside
    overlay.addEventListener('click', (e) => {
        if (e.target === overlay) {
            overlay.remove();
        }
    });
    
    // Position overlay over the pad
    const padContainer = padElement.closest('.pad-container');
    padContainer.style.position = 'relative';
    padContainer.appendChild(overlay);
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
        console.log(`[Pad Filter] Set pad ${padIndex} to filter ${FILTER_TYPES[filterType].name}`);
    }
}

// Clear filter for a specific pad
function clearPadFilter(padIndex) {
    setPadFilter(padIndex, 0);
}

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
    console.log(`Sample buttons updated: ${buttonsShown} buttons shown`);
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
    console.log('Sample counts received:', sanitizedCounts, `Total: ${totalFiles}`);
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
    
    // Toast removed - too many notifications when playing
    
    // Grabar en loop si está activo
    if (isRecording) {
        const currentTime = Date.now() - recordStartTime;
        recordedSteps.push({ pad: padIndex, time: currentTime });
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
    const trackNames = ['BD', 'SD', 'CH', 'OH', 'CP', 'RS', 'CL', 'CY'];
    const trackColors = ['#ff6b6b', '#f7b731', '#26de81', '#45aaf2', '#a55eea', '#fd9644', '#2bcbba', '#778ca3'];

    stepDots = [];
    stepColumns = Array.from({ length: 16 }, () => []);
    lastCurrentStep = null;
    
    // 8 tracks x 16 steps (con labels)
    for (let track = 0; track < 8; track++) {
        // Track label con botón mute
        const label = document.createElement('div');
        label.className = 'track-label';
        label.dataset.track = track;
        
        const muteBtn = document.createElement('button');
        muteBtn.className = 'mute-btn';
        muteBtn.setAttribute('aria-label', 'Mute');
        muteBtn.title = 'Mute';
        muteBtn.dataset.track = track;
        muteBtn.addEventListener('click', (e) => {
            e.stopPropagation();
            setTrackMuted(track, !trackMutedState[track], true);
        });
        
        const filterBtn = document.createElement('button');
        filterBtn.className = 'track-filter-btn';
        filterBtn.setAttribute('aria-label', 'Filter');
        filterBtn.title = 'Aplicar filtro (F1-F10)';
        filterBtn.textContent = 'F';
        filterBtn.dataset.track = track;
        filterBtn.addEventListener('click', (e) => {
            e.stopPropagation();
            // Set selected track globally so filter panel can use it
            if (window.keyboard_controls_module) {
                window.keyboard_controls_module.selectedTrack = track;
            }
            if (window.showTrackFilterPanel) {
                window.showTrackFilterPanel(track);
            }
        });
        
        const name = document.createElement('span');
        name.textContent = trackNames[track];
        name.style.color = trackColors[track];

        const loopIndicator = document.createElement('span');
        loopIndicator.className = 'loop-indicator';
        loopIndicator.textContent = 'LOOP';
        
        label.appendChild(muteBtn);
        label.appendChild(filterBtn);
        label.appendChild(name);
        label.appendChild(loopIndicator);
        label.style.borderColor = trackColors[track];
        
        // Hacer click en label selecciona el track para filtros
        label.addEventListener('click', (e) => {
            if (e.target !== muteBtn && window.selectTrack) {
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

// Controls
function setupControls() {
    // Play/Stop
    document.getElementById('playBtn').addEventListener('click', () => {
        sendWebSocket({ cmd: 'start' });
    });
    
    document.getElementById('stopBtn').addEventListener('click', () => {
        sendWebSocket({ cmd: 'stop' });
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
        console.log(`Sequencer volume set to ${volume}%`);
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
        console.log(`Live volume set to ${volume}%`);
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
        visualizerNeedsRedraw = true;
        syncLedMonoMode();
    });
    
    // Botón para cargar listas de samplers
    const loadSampleListsBtn = document.getElementById('loadSampleListsBtn');
    if (loadSampleListsBtn) {
        loadSampleListsBtn.addEventListener('click', () => {
            console.log('=== CARGANDO LISTAS DE SAMPLERS ===');
            const statusEl = document.getElementById('sampleLoadStatus');
            if (statusEl) statusEl.textContent = 'Cargando...';
            
            requestAllSamples();
            
            setTimeout(() => {
                const totalLoaded = Object.keys(sampleCatalog).length;
                console.log(`Total familias cargadas: ${totalLoaded}`);
                if (statusEl) statusEl.textContent = `${totalLoaded}/8 familias cargadas`;
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
            console.log('[reloadCountsBtn] Recargando conteos...');
            requestSampleCounts();
        });
    }
    
    // FX Controls - deprecated, now using preset system
    // setupFXControls();
}

function setupFXControls() {
    // Filter Type
    const filterType = document.getElementById('filterType');
    filterType.addEventListener('change', (e) => {
        sendWebSocket({
            cmd: 'setFilter',
            type: parseInt(e.target.value)
        });
        updateFilterMeter();
    });
    
    // Filter Cutoff
    const filterCutoff = document.getElementById('filterCutoff');
    const filterCutoffValue = document.getElementById('filterCutoffValue');
    filterCutoff.addEventListener('input', (e) => {
        filterCutoffValue.textContent = e.target.value;
        sendWebSocket({
            cmd: 'setFilterCutoff',
            value: parseFloat(e.target.value)
        });
        updateFilterMeter();
    });
    
    // Filter Resonance
    const filterResonance = document.getElementById('filterResonance');
    const filterResonanceValue = document.getElementById('filterResonanceValue');
    filterResonance.addEventListener('input', (e) => {
        filterResonanceValue.textContent = parseFloat(e.target.value).toFixed(1);
        sendWebSocket({
            cmd: 'setFilterResonance',
            value: parseFloat(e.target.value)
        });
        updateFilterMeter();
    });
    
    // Bit Crush
    const bitCrush = document.getElementById('bitCrush');
    const bitCrushValue = document.getElementById('bitCrushValue');
    bitCrush.addEventListener('input', (e) => {
        bitCrushValue.textContent = e.target.value;
        sendWebSocket({
            cmd: 'setBitCrush',
            value: parseInt(e.target.value)
        });
    });
    
    // Distortion
    const distortion = document.getElementById('distortion');
    const distortionValue = document.getElementById('distortionValue');
    distortion.addEventListener('input', (e) => {
        distortionValue.textContent = e.target.value;
        sendWebSocket({
            cmd: 'setDistortion',
            value: parseFloat(e.target.value)
        });
    });
    
    // Sample Rate Reducer
    const sampleRate = document.getElementById('sampleRate');
    const sampleRateValue = document.getElementById('sampleRateValue');
    sampleRate.addEventListener('input', (e) => {
        sampleRateValue.textContent = e.target.value;
        sendWebSocket({
            cmd: 'setSampleRate',
            value: parseInt(e.target.value)
        });
    });
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
    updateFilterMeter();
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
    const min = parseInt(slider.min, 10) || 0;
    const max = parseInt(slider.max, 10) || 100;
    bar.style.width = `${getNormalizedPercentage(value, min, max).toFixed(1)}%`;
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
    const min = parseInt(slider.min, 10) || 0;
    const max = parseInt(slider.max, 10) || 100;
    bar.style.width = `${getNormalizedPercentage(value, min, max).toFixed(1)}%`;
    if (bar.parentElement) {
        bar.parentElement.classList.add('active');
    }
}

function updateFilterMeter() {
    const meterValue = document.getElementById('meterFilterValue');
    const meterBar = document.getElementById('meterFilterBar');
    const filterType = document.getElementById('filterType');
    const filterCutoff = document.getElementById('filterCutoff');
    const filterResonance = document.getElementById('filterResonance');
    if (!meterValue || !meterBar || !filterType || !filterCutoff) return;
    const typeValue = parseInt(filterType.value, 10) || 0;
    const barWrapper = meterBar.parentElement;
    if (typeValue === 0) {
        meterValue.textContent = 'OFF';
        meterBar.style.width = '0%';
        if (barWrapper) {
            barWrapper.classList.remove('active');
        }
        return;
    }
    const cutoffVal = parseInt(filterCutoff.value, 10);
    const resonanceVal = filterResonance ? parseFloat(filterResonance.value) : 1.0;
    const min = parseInt(filterCutoff.min, 10) || 100;
    const max = parseInt(filterCutoff.max, 10) || 16000;
    meterValue.textContent = `${filterTypeLabels[typeValue] || 'FILTER'} • ${cutoffVal}Hz • Q${resonanceVal.toFixed(1)}`;
    meterBar.style.width = `${getNormalizedPercentage(cutoffVal, min, max).toFixed(1)}%`;
    if (barWrapper) {
        barWrapper.classList.add('active');
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

    if (data.step !== undefined) {
        updateCurrentStep(data.step);
    }
    
    // Update playing state
    isPlaying = data.playing || false;
    
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
}

// Send WebSocket message
function sendWebSocket(data) {
    if (ws && ws.readyState === WebSocket.OPEN) {
        ws.send(JSON.stringify(data));
    }
}

// Export to window for keyboard-controls.js
window.sendWebSocket = sendWebSocket;

// ============= AUDIO VISUALIZERS =============

function hexToRgb(hex) {
    if (!hex || typeof hex !== 'string') return { r: 255, g: 0, b: 0 };
    const normalized = hex.replace('#', '');
    if (normalized.length !== 6) return { r: 255, g: 0, b: 0 };
    const intVal = parseInt(normalized, 16);
    return {
        r: (intVal >> 16) & 255,
        g: (intVal >> 8) & 255,
        b: intVal & 255
    };
}

function initVisualizers() {
    // Spectrum visualizer disabled - not used
    // const spectrumCanvas = document.getElementById('spectrumCanvas');
    // if (!spectrumCanvas) {
    //     console.warn('Spectrum canvas not found (disabled)');
    //     return;
    // }
    // Spectrum visualizer disabled - not used
    return;
}

// ============= KEYBOARD CONTROLS =============

let isPlaying = false;

function setupKeyboardControls() {
    // Mapeo de teclas a pads (8 pads)
    const keyToPad = padKeyBindings.reduce((mapping, key, idx) => {
        mapping[key.toUpperCase()] = idx;
        return mapping;
    }, {});

    const codeToPad = {
        Digit1: 0, Digit2: 1, Digit3: 2, Digit4: 3, Digit5: 4,
        Digit6: 5, Digit7: 6, Digit8: 7, Digit9: 8, Digit0: 9,
        KeyQ: 10, KeyW: 11, KeyE: 12, KeyR: 13, KeyT: 14, KeyY: 15
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
    
    console.log('✓ Keyboard controls initialized (8 pads)');
    console.log('  Keys: 1-8=Pads, SPACE=Play/Pause, [/]=BPM, -/+=Volume');
    
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
    
    // Export pad filter functions and state
    window.padFilterState = padFilterState;
    window.updatePadFilterIndicator = updatePadFilterIndicator;
    window.setPadFilter = setPadFilter;
    window.clearPadFilter = clearPadFilter;
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
        console.log('Paused');
    } else {
        // Play
        sendWebSocket({ cmd: 'start' });
        isPlaying = true;
        console.log('Playing');
    }
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
        
        console.log(`BPM: ${newTempo}`);
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
        
        console.log(`Live Volume: ${newVolume}%`);
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
        
        console.log(`Sequencer Volume: ${newVolume}%`);
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
    
    // Actualizar visualizador si estamos en performance
    if (tabId === 'performance') {
        isVisualizerActive = true;
        visualizerNeedsRedraw = true;
    }
}

// Sample Selector Functions
function showSampleSelector(padIndex, family) {
    sampleSelectorContext = { padIndex, family };
    console.log(`[showSampleSelector] Requesting samples for ${family}...`);
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
    console.log('[requestSampleCounts] Requesting sample counts from ESP32');
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
    console.log(`Loading ${family}/${filename} to pad ${padIndex}`);
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

// Apply filter preset from FX library
function applyFilterPreset(filterType, cutoffFreq) {
    // Default resonance for presets
    const resonance = filterType === 9 ? 10.0 : 1.5; // Resonant filter gets high Q
    
    const filterNames = ['NONE', 'LOW PASS', 'HIGH PASS', 'BAND PASS', 'NOTCH', 
                        'LOW SHELF', 'HIGH SHELF', 'PEAK', 'ALL PASS', 'RESONANT'];
    
    console.log(`Applying filter preset: ${filterNames[filterType]} @ ${cutoffFreq}Hz`);
    
    // Check if track is selected
    if (window.selectedTrack !== null && window.selectedTrack !== undefined) {
        const track = window.selectedTrack;
        const trackNames = ['BD', 'SD', 'CH', 'OH', 'CP', 'CB', 'RS', 'CL', 'MA', 'CY', 'HT', 'LT', 'MC', 'MT', 'HC', 'LC'];
        
        sendWebSocket({
            cmd: 'setTrackFilter',
            track: track,
            type: filterType,
            cutoff: cutoffFreq,
            resonance: resonance
        });
        
        if (window.showToast) {
            window.showToast(
                `Track ${track + 1} (${trackNames[track]}): ${filterNames[filterType]} @ ${cutoffFreq}Hz`,
                window.TOAST_TYPES?.SUCCESS || 'success',
                2500
            );
        }
        
        // Create or update badge on track label
        const trackLabel = document.querySelector(`.track-label[data-track="${track}"]`);
        if (trackLabel) {
            // Remove ALL existing badges first to avoid duplicates
            trackLabel.querySelectorAll('.track-filter-badge').forEach(b => b.remove());
            
            // Only create badge if filter type is NOT 0 (NONE) and is valid
            if (filterType > 0 && filterType < 10) {
                // Create new badge with icon + initials
                const badge = document.createElement('div');
                badge.className = 'track-filter-badge';
                const filterIcons = ['⭕', '🔽', '🔼', '🎯', '🚫', '📊', '📈', '⛰️', '🌀', '💫'];
                const filterInitials = ['', 'LP', 'HP', 'BP', 'NT', 'LS', 'HS', 'PK', 'AP', 'RS'];
                badge.innerHTML = `${filterIcons[filterType]} <span class="badge-initials">${filterInitials[filterType]}</span>`;
                trackLabel.appendChild(badge);
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
            resonance: resonance
        });
        
        if (window.showToast) {
            window.showToast(
                `Pad ${pad + 1} (${names[pad]}): ${filterNames[filterType]} @ ${cutoffFreq}Hz`,
                window.TOAST_TYPES?.SUCCESS || 'success',
                2500
            );
        }
        
        // Create or update badge on pad
        const padElement = document.querySelector(`.pad[data-pad="${pad}"]`);
        if (padElement) {
            let badge = padElement.querySelector('.pad-filter-badge');
            if (filterType === 0) {
                // Remove badge if NONE
                if (badge) badge.remove();
            } else {
                if (!badge) {
                    badge = document.createElement('div');
                    badge.className = 'pad-filter-badge';
                    padElement.appendChild(badge);
                }
                const filterIcons = ['⭕', '🔽', '🔼', '🎯', '🚫', '📊', '📈', '⛰️', '🌀', '💫'];
                badge.innerHTML = `${filterIcons[filterType]} <span class="pad-num">${cutoffFreq}Hz</span>`;
            }
        }
        
        return;
    }
    
    // No selection - show info toast
    if (window.showToast) {
        window.showToast(
            'Selecciona un track (click en nombre) o pad (click en pad LIVE) primero',
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
        
        console.log('[MIDI] Device connected:', data.deviceName);
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
        
        console.log('[MIDI] Device disconnected');
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
    
    console.log(`[MIDI] ${data.messageType} Ch:${data.channel} Data1:${data.data1} Data2:${data.data2}`);
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
    
    console.log(`[Upload] Starting upload: ${file.name} to pad ${padIndex} (${padName})`);
    
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
        console.log('[Upload] Response:', data);
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
    
    console.log(`[Upload] Progress: ${data.percent}%`);
    
    const btn = document.querySelector(`.pad-upload-btn[data-pad="${data.pad}"]`);
    if (btn) {
        btn.textContent = `${data.percent}%`;
    }
}

function handleUploadComplete(data) {
    console.log(`[Upload] Complete:`, data);
    
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
            
            console.log('[MIDI Mapping] Loaded:', data.mappings);
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
        
        console.log('[MIDI Mapping] Saved:', mappings);
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
            
            console.log('[MIDI Mapping] Reset to default');
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
