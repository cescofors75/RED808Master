// RED808 Drum Machine - JavaScript Application

let ws = null;
let isConnected = false;
let currentStep = 0;
let tremoloIntervals = {};
let padLoopState = {};
let isRecording = false;
let recordedSteps = [];
let recordStartTime = 0;

// Visualizer data
let spectrumData = new Array(64).fill(0);
let waveformData = new Array(128).fill(0);
let isVisualizerActive = true;

// Sample counts per family
let sampleCounts = {};

// Keyboard state
let keyboardPadsActive = {};
let keyboardHoldTimers = {};
let keyboardTremoloState = {};

// Pad hold timers for long press detection
let padHoldTimers = {};
let trackMutedState = new Array(16).fill(false);

// 16 instrumentos RED808
const padNames = ['BD', 'SD', 'CH', 'OH', 'CP', 'CB', 'RS', 'CL', 'MA', 'CY', 'HT', 'LT', 'MC', 'MT', 'HC', 'LC'];

// Tecla asociada a cada pad (mostrar en UI y para accesos directos)
const padKeyBindings = ['1', '2', '3', '4', '5', '6', '7', '8', '9', '0', 'Q', 'W', 'E', 'R', 'T', 'Y'];

// Descripción completa de cada instrumento
const padDescriptions = [
    'Bass Drum (Bombo)',
    'Snare Drum (Caja)',
    'Closed Hi-Hat',
    'Open Hi-Hat',
    'Hand Clap (Palmas)',
    'Cowbell (Cencerro)',
    'Rim Shot (Aro)',
    'Claves',
    'Maracas',
    'Cymbal (Platillo)',
    'Hi Tom (Agudo)',
    'Low Tom (Grave)',
    'Mid Conga',
    'Mid Tom (Medio)',
    'Hi Conga',
    'Low Conga'
];

const filterTypeLabels = {
    0: 'OFF',
    1: 'LOW PASS',
    2: 'HIGH PASS',
    3: 'BAND PASS',
    4: 'NOTCH'
};

const instrumentPalette = [
    '#ff6b6b', '#f7b731', '#26de81', '#45aaf2',
    '#a55eea', '#fd9644', '#2bcbba', '#778ca3',
    '#fed330', '#0fb9b1', '#fc5c65', '#4b7bec',
    '#f368e0', '#20bf6b', '#a5b1c2', '#e84393'
];

const padSampleMetadata = new Array(16).fill(null);
const DEFAULT_SAMPLE_QUALITY = '44.1kHz • 16-bit mono';

// Initialize
document.addEventListener('DOMContentLoaded', () => {
    initWebSocket();
    createPads();
    createSequencer();
    setupControls();
    initHeaderMeters();
    initVisualizers();
    setupKeyboardControls();
    initSectionManager();
});

// WebSocket Connection
function initWebSocket() {
    const wsUrl = `ws://${window.location.hostname}/ws`;
    ws = new WebSocket(wsUrl);
    
    ws.onopen = () => {
        console.log('WebSocket Connected');
        isConnected = true;
        updateStatus(true);
        syncLedMonoMode();
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
            // Audio visualization data
            if (data.spectrum) {
                spectrumData = data.spectrum;
                // Debug: mostrar primer y último valor
                if (spectrumData.length > 0) {
                    console.log(`Spectrum received: length=${spectrumData.length}, first=${spectrumData[0]}, last=${spectrumData[spectrumData.length-1]}, max=${Math.max(...spectrumData)}`);
                }
            }
            break;
        case 'state':
            updateSequencerState(data);
            updateDeviceStats(data);
            if (Array.isArray(data.samples)) {
                applySampleMetadataFromState(data.samples);
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
    }
}

function loadPatternData(data) {
    console.log('loadPatternData called, data keys:', Object.keys(data));
    
    // Limpiar sequencer
    document.querySelectorAll('.seq-step').forEach(el => {
        el.classList.remove('active');
    });
    
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
                        console.warn(`Step element not found for track ${track}, step ${step}`);
                    }
                }
            });
            if (track >= 8 && trackSteps > 0) {
                console.log(`Track ${track} (${padNames[track]}): ${trackSteps} steps activated`);
            }
        }
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
    
    for (let i = 0; i < 16; i++) {
        const padContainer = document.createElement('div');
        padContainer.className = 'pad-container';
        
        const pad = document.createElement('div');
        pad.className = 'pad';
        pad.dataset.pad = i;
        
        pad.innerHTML = `
            <div class="pad-number">${(i + 1).toString().padStart(2, '0')}</div>
            <div class="pad-name">${padNames[i]}</div>
            <div class="pad-sample-info" id="sampleInfo-${i}">...</div>
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
        
        // Botón para seleccionar sample (se añade después según count)
        const selectBtn = document.createElement('button');
        selectBtn.className = 'pad-select-btn';
        selectBtn.style.display = 'none';  // Oculto por defecto
        selectBtn.dataset.padIndex = i;
        selectBtn.dataset.family = families[i];
        selectBtn.addEventListener('click', (e) => {
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

// Actualizar botones de selección de samples según conteo
function updateSampleButtons() {
    document.querySelectorAll('.pad-select-btn').forEach((btn, index) => {
        const family = padNames[index];
        const count = sampleCounts[family] || 0;
        
        if (count > 1) {
            btn.style.display = 'flex';
            btn.innerHTML = `📂<span class="sample-count-badge">${count}</span>`;
            btn.title = `${count} ${family} samples available`;
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
    console.log('Sample counts received:', sanitizedCounts);
}

function updateInstrumentCounts(totalFiles) {
    padNames.forEach((family) => {
        const label = document.getElementById(`instCount-${family}`);
        if (label) {
            const count = sampleCounts[family] || 0;
            label.textContent = count > 0 ? `${count} library files` : 'No files found';
        }
    });
    const totalsEl = document.getElementById('libraryTotals');
    if (totalsEl) {
        const files = typeof totalFiles === 'number' ? totalFiles : Object.values(sampleCounts).reduce((sum, val) => sum + (val || 0), 0);
        totalsEl.textContent = `${files} files / ${padNames.length} families`;
    }
}

function refreshPadSampleInfo(padIndex) {
    const infoEl = document.getElementById(`sampleInfo-${padIndex}`);
    const meta = padSampleMetadata[padIndex];
    if (!infoEl) return;
    if (!meta) {
        infoEl.textContent = '—';
        infoEl.title = 'No sample loaded';
    } else {
        infoEl.textContent = `${meta.filename} • ${meta.sizeKB}KB`;
        infoEl.title = `${meta.filename} - ${meta.sizeKB} KB - ${meta.format}`;
    }
    updateInstrumentMetadata(padIndex);
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
    const trackNames = ['BD', 'SD', 'CH', 'OH', 'CP', 'CB', 'RS', 'CL', 'MA', 'CY', 'HT', 'LT', 'MC', 'MT', 'HC', 'LC'];
    const trackColors = ['#ff6b6b', '#f7b731', '#26de81', '#45aaf2', '#a55eea', '#fd9644', '#2bcbba', '#778ca3', '#fed330', '#0fb9b1', '#fc5c65', '#4b7bec', '#f368e0', '#20bf6b', '#a5b1c2', '#e84393'];
    
    // 16 tracks x 16 steps (con labels)
    for (let track = 0; track < 16; track++) {
        // Track label con botón mute
        const label = document.createElement('div');
        label.className = 'track-label';
        label.dataset.track = track;
        
        const muteBtn = document.createElement('button');
        muteBtn.className = 'mute-btn';
        muteBtn.textContent = '🔇';
        muteBtn.dataset.track = track;
        muteBtn.addEventListener('click', (e) => {
            e.stopPropagation();
            setTrackMuted(track, !trackMutedState[track], true);
        });
        
        const name = document.createElement('span');
        name.textContent = trackNames[track];
        name.style.color = trackColors[track];

        const loopIndicator = document.createElement('span');
        loopIndicator.className = 'loop-indicator';
        loopIndicator.textContent = 'LOOP';
        
        label.appendChild(muteBtn);
        label.appendChild(name);
        label.appendChild(loopIndicator);
        label.style.borderColor = trackColors[track];
        grid.appendChild(label);
        
        // 16 steps
        for (let step = 0; step < 16; step++) {
            const stepEl = document.createElement('div');
            stepEl.className = 'seq-step';
            stepEl.dataset.track = track;
            stepEl.dataset.step = step;
            
            stepEl.addEventListener('click', () => {
                toggleStep(track, step, stepEl);
            });
            
            grid.appendChild(stepEl);
        }
    }
    
    // Step indicator dots
    for (let i = 0; i < 16; i++) {
        const dot = document.createElement('div');
        dot.className = 'step-dot';
        dot.dataset.step = i;
        indicator.appendChild(dot);
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
    currentStep = step;
    
    // Update indicator
    document.querySelectorAll('.step-dot').forEach((dot, i) => {
        dot.classList.toggle('current', i === step);
    });
    
    // Highlight current column
    document.querySelectorAll('.seq-step').forEach(el => {
        const elStep = parseInt(el.dataset.step);
        el.classList.toggle('current', elStep === step);
    });
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
            sendWebSocket({
                cmd: 'selectPattern',
                index: pattern
            });
            
            // Solicitar datos del nuevo pattern después de un breve delay
            setTimeout(() => {
                sendWebSocket({ cmd: 'getPattern' });
            }, 150);
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
    
    // FX Controls
    setupFXControls();
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
        tempoSlider.value = data.tempo;
        tempoValue.textContent = data.tempo;
        updateBpmMeter(parseFloat(data.tempo));
    }
    if (data.sequencerVolume !== undefined) {
        const sequencerVolumeSlider = document.getElementById('sequencerVolumeSlider');
        const sequencerVolumeValue = document.getElementById('sequencerVolumeValue');
        if (sequencerVolumeSlider && sequencerVolumeValue) {
            sequencerVolumeSlider.value = data.sequencerVolume;
            sequencerVolumeValue.textContent = data.sequencerVolume;
            updateSequencerVolumeMeter(parseInt(data.sequencerVolume, 10));
        }
    }
    if (data.liveVolume !== undefined) {
        const liveVolumeSlider = document.getElementById('liveVolumeSlider');
        const liveVolumeValue = document.getElementById('liveVolumeValue');
        if (liveVolumeSlider && liveVolumeValue) {
            liveVolumeSlider.value = data.liveVolume;
            liveVolumeValue.textContent = data.liveVolume;
            updateLiveVolumeMeter(parseInt(data.liveVolume, 10));
        }
    }
    if (Array.isArray(data.loopActive)) {
        data.loopActive.forEach((active, track) => {
            if (!padLoopState[track]) {
                padLoopState[track] = { active: false, paused: false };
            }
            padLoopState[track].active = !!active;
        });
    }
    if (Array.isArray(data.loopPaused)) {
        data.loopPaused.forEach((paused, track) => {
            if (!padLoopState[track]) {
                padLoopState[track] = { active: false, paused: false };
            }
            padLoopState[track].paused = !!paused;
        });
    }
    if (Array.isArray(data.loopActive) || Array.isArray(data.loopPaused)) {
        for (let track = 0; track < padNames.length; track++) {
            updatePadLoopVisual(track);
        }
    }
    if (Array.isArray(data.trackMuted)) {
        data.trackMuted.forEach((muted, track) => {
            setTrackMuted(track, !!muted, false);
        });
    }
    
    // Update playing state
    isPlaying = data.playing || false;
    
    // Update pattern button
    if (data.pattern !== undefined) {
        document.querySelectorAll('.btn-pattern').forEach(btn => {
            btn.classList.toggle('active', parseInt(btn.dataset.pattern) === data.pattern);
        });
    }
    
    // Request current pattern data
    sendWebSocket({ cmd: 'getPattern' });
}

// Send WebSocket message
function sendWebSocket(data) {
    if (ws && ws.readyState === WebSocket.OPEN) {
        ws.send(JSON.stringify(data));
    }
}

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
    const spectrumCanvas = document.getElementById('spectrumCanvas');
    
    if (!spectrumCanvas) {
        console.error('Spectrum canvas not found!');
        return;
    }
    
    const spectrumCtx = spectrumCanvas.getContext('2d');
    
    // Set actual canvas size for crisp rendering
    spectrumCanvas.width = 600;
    spectrumCanvas.height = 200;
    
    console.log('Visualizers initialized successfully');
    
    function drawSpectrum() {
        const width = spectrumCanvas.width;
        const height = spectrumCanvas.height;
        
        // Clear canvas with slight fade
        spectrumCtx.fillStyle = 'rgba(0, 0, 0, 0.1)';
        spectrumCtx.fillRect(0, 0, width, height);
        
        // Draw grid
        spectrumCtx.strokeStyle = 'rgba(255, 255, 255, 0.05)';
        spectrumCtx.lineWidth = 1;
        for (let i = 0; i < 5; i++) {
            const y = (height / 4) * i;
            spectrumCtx.beginPath();
            spectrumCtx.moveTo(0, y);
            spectrumCtx.lineTo(width, y);
            spectrumCtx.stroke();
        }
        
        // Check if we have valid data
        let hasData = false;
        for (let i = 0; i < spectrumData.length; i++) {
            if (spectrumData[i] > 0) {
                hasData = true;
                break;
            }
        }
        
        if (!hasData) {
            // Draw "No Signal" message
            spectrumCtx.fillStyle = '#666';
            spectrumCtx.font = '14px Roboto Mono';
            spectrumCtx.textAlign = 'center';
            spectrumCtx.fillText('No Audio Signal', width / 2, height / 2);
            spectrumCtx.textAlign = 'left';
        }
        
        // Draw spectrum bars
        const barWidth = width / spectrumData.length;
        
        for (let i = 0; i < spectrumData.length; i++) {
            const value = spectrumData[i];
            const barHeight = (value / 255) * height;
            
            const x = i * barWidth;
            const y = height - barHeight;
            
            // Create gradient based on instrument palette
            const gradient = spectrumCtx.createLinearGradient(x, y, x, height);
            if (document.body.classList.contains('mono-mode')) {
                gradient.addColorStop(0, 'rgba(255, 80, 80, 0.95)');
                gradient.addColorStop(0.55, 'rgba(255, 0, 0, 0.75)');
                gradient.addColorStop(1, 'rgba(120, 0, 0, 0.35)');
            } else {
                const paletteColor = instrumentPalette[i % instrumentPalette.length];
                const { r, g, b } = hexToRgb(paletteColor);
                gradient.addColorStop(0, `rgba(${r}, ${g}, ${b}, 0.95)`);
                gradient.addColorStop(0.5, `rgba(${r}, ${g}, ${b}, 0.65)`);
                gradient.addColorStop(1, `rgba(${r}, ${g}, ${b}, 0.25)`);
            }
            
            spectrumCtx.fillStyle = gradient;
            spectrumCtx.fillRect(x, y, barWidth - 1, barHeight);
            
            // Glow effect on peaks
            if (value > 200) {
                spectrumCtx.shadowBlur = 10;
                spectrumCtx.shadowColor = document.body.classList.contains('mono-mode')
                    ? '#ff3b3b'
                    : instrumentPalette[i % instrumentPalette.length];
                spectrumCtx.fillRect(x, y, barWidth - 1, barHeight);
                spectrumCtx.shadowBlur = 0;
            }
        }
        
        // Draw labels
        spectrumCtx.fillStyle = '#666';
        spectrumCtx.font = '10px Roboto Mono';
        spectrumCtx.fillText('20Hz', 5, height - 5);
        spectrumCtx.fillText('20kHz', width - 40, height - 5);
    }
    
    // Animation loop
    function animate() {
        if (isVisualizerActive) {
            drawSpectrum();
        }
        requestAnimationFrame(animate);
    }
    
    animate();
    console.log('✓ Audio visualizers initialized');
}

// ============= KEYBOARD CONTROLS =============

let isPlaying = false;

function setupKeyboardControls() {
    // Mapeo de teclas a pads (16 pads)
    const keyToPad = padKeyBindings.reduce((mapping, key, idx) => {
        mapping[key.toUpperCase()] = idx;
        return mapping;
    }, {});
    
    document.addEventListener('keydown', (e) => {
        // Evitar repetición si ya está presionada
        if (e.repeat) return;
        
        const key = e.key.toUpperCase();
        
        // Pads 1-9, 0, Q-Y con tremolo
        if (keyToPad.hasOwnProperty(key)) {
            e.preventDefault();
            const padIndex = keyToPad[key];
            
            if (!keyboardPadsActive[padIndex]) {
                keyboardPadsActive[padIndex] = true;
                const padElement = document.querySelector(`.pad[data-pad="${padIndex}"]`);
                if (padElement) {
                    startKeyboardTremolo(padIndex, padElement);
                }
            }
        }
        
        // SPACE: Toggle Play/Pause
        else if (key === ' ') {
            e.preventDefault();
            togglePlayPause();
        }
        
        // [: Bajar BPM
        else if (key === '[') {
            e.preventDefault();
            adjustBPM(-5);
        }
        
        // ]: Subir BPM
        else if (key === ']') {
            e.preventDefault();
            adjustBPM(5);
        }
        
        // A: Bajar volumen del sequencer
        else if (key === 'A') {
            e.preventDefault();
            adjustSequencerVolume(-5);
        }
        
        // S: Subir volumen del sequencer
        else if (key === 'S') {
            e.preventDefault();
            adjustSequencerVolume(5);
        }
        
        // -: Bajar Volumen
        else if (key === '-' || key === '_') {
            e.preventDefault();
            adjustVolume(-5);
        }
        
        // +: Subir Volumen
        else if (key === '+' || key === '=') {
            e.preventDefault();
            adjustVolume(5);
        }
    });
    
    document.addEventListener('keyup', (e) => {
        const key = e.key.toUpperCase();
        
        // Soltar pads
        if (keyToPad.hasOwnProperty(key)) {
            e.preventDefault();
            const padIndex = keyToPad[key];
            
            if (keyboardPadsActive[padIndex]) {
                keyboardPadsActive[padIndex] = false;
                const padElement = document.querySelector(`.pad[data-pad="${padIndex}"]`);
                if (padElement) {
                    stopKeyboardTremolo(padIndex, padElement);
                }
            }
        }
    });
    
    console.log('✓ Keyboard controls initialized (16 pads)');
    console.log('  Keys: 1-9,0,Q-Y=Pads, SPACE=Play/Pause, [/]=BPM, -/+=Volume');
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
// SECTION MANAGER
// ========================================

let sectionOrder = [];
let sectionVisibility = {};

function initSectionManager() {
    // Obtener todas las secciones
    const sections = document.querySelectorAll('[data-section]');
    const container = document.querySelector('.container');
    
    // Cargar configuración guardada
    loadSectionConfig();
    
    // Si no hay configuración, crear una por defecto
    if (sectionOrder.length === 0) {
        sections.forEach(section => {
            const sectionId = section.getAttribute('data-section');
            sectionOrder.push(sectionId);
            sectionVisibility[sectionId] = true;
        });
        saveSectionConfig();
    }
    
    // Aplicar configuración
    applySectionConfig();
    
    // Generar lista de secciones en el panel
    generateSectionList();
    
    // Eventos del menú
    const menuToggle = document.getElementById('menuToggle');
    const closeManager = document.getElementById('closeManager');
    const managerOverlay = document.getElementById('managerOverlay');
    const sectionManager = document.getElementById('sectionManager');
    
    menuToggle.addEventListener('click', () => {
        sectionManager.classList.add('active');
        managerOverlay.classList.add('active');
        menuToggle.classList.add('active');
    });
    
    const closePanel = () => {
        saveSectionConfig();
        applySectionConfig();
        generateSectionList();
        sectionManager.classList.remove('active');
        managerOverlay.classList.remove('active');
        menuToggle.classList.remove('active');
    };
    
    closeManager.addEventListener('click', closePanel);
    managerOverlay.addEventListener('click', closePanel);
}

function loadSectionConfig() {
    try {
        const orderData = localStorage.getItem('sectionOrder');
        const visibilityData = localStorage.getItem('sectionVisibility');
        
        if (orderData) {
            sectionOrder = JSON.parse(orderData);
        }
        
        if (visibilityData) {
            sectionVisibility = JSON.parse(visibilityData);
        }
    } catch (e) {
        console.error('Error loading section config:', e);
    }
}

function saveSectionConfig() {
    try {
        localStorage.setItem('sectionOrder', JSON.stringify(sectionOrder));
        localStorage.setItem('sectionVisibility', JSON.stringify(sectionVisibility));
    } catch (e) {
        console.error('Error saving section config:', e);
    }
}

function applySectionConfig() {
    const container = document.querySelector('.container');
    const footer = document.querySelector('.footer');
    
    // Reordenar secciones según el orden guardado
    sectionOrder.forEach((sectionId, index) => {
        const section = document.getElementById(`section-${sectionId}`);
        if (section) {
            // Mover la sección antes del footer
            container.insertBefore(section, footer);
            
            // Aplicar visibilidad
            if (sectionVisibility[sectionId] === false) {
                section.style.display = 'none';
            } else {
                section.style.display = '';
            }
        }
    });
}

function generateSectionList() {
    const sectionList = document.getElementById('sectionList');
    sectionList.innerHTML = '';
    
    sectionOrder.forEach((sectionId, index) => {
        const section = document.getElementById(`section-${sectionId}`);
        if (!section) return;
        
        const title = section.getAttribute('data-title') || sectionId;
        const isVisible = sectionVisibility[sectionId] !== false;
        
        const item = document.createElement('div');
        item.className = 'section-item';
        item.draggable = true;
        item.dataset.sectionId = sectionId;
        
        item.innerHTML = `
            <div class="section-item-header">
                <span class="drag-handle">☰</span>
                <span class="section-item-title">${title}</span>
                <label class="visibility-toggle">
                    <input type="checkbox" ${isVisible ? 'checked' : ''} 
                           onchange="toggleSectionVisibility('${sectionId}', this.checked)">
                    <span class="toggle-slider"></span>
                </label>
            </div>
            <div class="section-item-controls">
                <button class="move-btn" onclick="moveSectionUp('${sectionId}')" 
                        ${index === 0 ? 'disabled' : ''}>↑ Arriba</button>
                <button class="move-btn" onclick="moveSectionDown('${sectionId}')" 
                        ${index === sectionOrder.length - 1 ? 'disabled' : ''}>↓ Abajo</button>
            </div>
        `;
        
        sectionList.appendChild(item);
        
        // Drag and Drop events
        item.addEventListener('dragstart', handleDragStart);
        item.addEventListener('dragover', handleDragOver);
        item.addEventListener('drop', handleDrop);
        item.addEventListener('dragend', handleDragEnd);
    });
}

function toggleSectionVisibility(sectionId, isVisible) {
    sectionVisibility[sectionId] = isVisible;
    
    const section = document.getElementById(`section-${sectionId}`);
    if (section) {
        section.style.display = isVisible ? '' : 'none';
    }
    
    saveSectionConfig();
}

function moveSectionUp(sectionId) {
    const index = sectionOrder.indexOf(sectionId);
    if (index > 0) {
        // Intercambiar con el anterior
        [sectionOrder[index - 1], sectionOrder[index]] = 
        [sectionOrder[index], sectionOrder[index - 1]];
        
        saveSectionConfig();
        applySectionConfig();
        generateSectionList();
    }
}

function moveSectionDown(sectionId) {
    const index = sectionOrder.indexOf(sectionId);
    if (index < sectionOrder.length - 1) {
        // Intercambiar con el siguiente
        [sectionOrder[index], sectionOrder[index + 1]] = 
        [sectionOrder[index + 1], sectionOrder[index]];
        
        saveSectionConfig();
        applySectionConfig();
        generateSectionList();
    }
}

// Drag and Drop handlers
let draggedItem = null;

function handleDragStart(e) {
    draggedItem = this;
    this.classList.add('dragging');
    e.dataTransfer.effectAllowed = 'move';
}

function handleDragOver(e) {
    if (e.preventDefault) {
        e.preventDefault();
    }
    e.dataTransfer.dropEffect = 'move';
    
    const afterElement = getDragAfterElement(this.parentElement, e.clientY);
    if (afterElement == null) {
        this.parentElement.appendChild(draggedItem);
    } else {
        this.parentElement.insertBefore(draggedItem, afterElement);
    }
    
    return false;
}

function handleDrop(e) {
    if (e.stopPropagation) {
        e.stopPropagation();
    }
    
    // Actualizar el orden
    const items = document.querySelectorAll('.section-item');
    sectionOrder = [];
    items.forEach(item => {
        sectionOrder.push(item.dataset.sectionId);
    });
    
    saveSectionConfig();
    applySectionConfig();
    generateSectionList();
    
    return false;
}

function handleDragEnd(e) {
    this.classList.remove('dragging');
}

function getDragAfterElement(container, y) {
    const draggableElements = [...container.querySelectorAll('.section-item:not(.dragging)')];
    
    return draggableElements.reduce((closest, child) => {
        const box = child.getBoundingClientRect();
        const offset = y - box.top - box.height / 2;
        
        if (offset < 0 && offset > closest.offset) {
            return { offset: offset, element: child };
        } else {
            return closest;
        }
    }, { offset: Number.NEGATIVE_INFINITY }).element;
}

// Sample Selector Functions
function showSampleSelector(padIndex, family) {
    // Solicitar lista de samples
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
        alert(`No samples found for ${family}`);
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
            document.body.removeChild(modal);
        });
        sampleList.appendChild(sampleItem);
    });
    
    modal.querySelector('.btn-close-modal').addEventListener('click', () => {
        document.body.removeChild(modal);
    });
    
    document.body.appendChild(modal);
}

function loadSampleToPad(padIndex, family, filename) {
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
}


