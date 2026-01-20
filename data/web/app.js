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

// Keyboard state
let keyboardPadsActive = {};
let keyboardHoldTimers = {};

const padNames = ['KICK', 'SNARE', 'CLHAT', 'OPHAT', 'CLAP', 'PERC', 'RIM', 'TOM'];

// Initialize
document.addEventListener('DOMContentLoaded', () => {
    initWebSocket();
    createPads();
    createSequencer();
    setupControls();
    initVisualizers();
    setupKeyboardControls();
});

// WebSocket Connection
function initWebSocket() {
    const wsUrl = `ws://${window.location.hostname}/ws`;
    ws = new WebSocket(wsUrl);
    
    ws.onopen = () => {
        console.log('WebSocket Connected');
        isConnected = true;
        updateStatus(true);
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
            update
        case 'audioData':
            // Audio visualization data
            if (data.spectrum) spectrumData = data.spectrum;
            if (data.waveform) waveformData = data.waveform;
            break;PadLoopVisual(data.track);
            break;
        case 'state':
            updateSequencerState(data);
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
        case 'kitChanged':
            showNotification(`Kit: ${data.name}`);
            break;
    }
}

function loadPatternData(data) {
    // Limpiar sequencer
    document.querySelectorAll('.seq-step').forEach(el => {
        el.classList.remove('active');
    });
    
    // Cargar datos del pattern
    for (let track = 0; track < 8; track++) {
        if (data[track]) {
            data[track].forEach((active, step) => {
                if (active) {
                    const stepEl = document.querySelector(`[data-track="${track}"][data-step="${step}"]`);
                    if (stepEl) {
                        stepEl.classList.add('active');
                    }
                }
            });
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

// Create Pads
function createPads() {
    const grid = document.getElementById('padsGrid');
    
    for (let i = 0; i < 8; i++) {
        const padContainer = document.createElement('div');
        padContainer.className = 'pad-container';
        
        const pad = document.createElement('div');
        pad.className = 'pad';
        pad.dataset.pad = i;
        
        pad.innerHTML = `
            <div class="pad-number">${(i + 1).toString().padStart(2, '0')}</div>
            <div class="pad-name">${padNames[i]}</div>
        `;
        
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
        
        // Botón de loop
        const loopBtn = document.createElement('button');
        loopBtn.className = 'loop-btn';
        loopBtn.innerHTML = '🔁';
        loopBtn.dataset.pad = i;
        loopBtn.addEventListener('click', (e) => {
            e.stopPropagation();
            toggleLoop(i);
        });
        
        padContainer.appendChild(pad);
        padContainer.appendChild(loopBtn);
        grid.appendChild(padContainer);
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
    
    // Timer para pulsación larga (3 segundos) -> activa loop
    const timer = setTimeout(() => {
        // Activar loop después de 3 segundos
        toggleLoop(padIndex);
        timer._longPressTriggered = true;
    }, 3000);
    padHoldTimers[padIndex] = timer;
    
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
    // Cancelar timer de pulsación larga si se suelta antes de 3 segundos
    if (padHoldTimers[padIndex]) {
        const wasHolding = padHoldTimers[padIndex];
        clearTimeout(padHoldTimers[padIndex]);
        delete padHoldTimers[padIndex];
        
        // Si había loop activo y se soltó rápido, pausar/reanudar
        if (padLoopState[padIndex] && padLoopState[padIndex].active && !wasHolding._longPressTriggered) {
            pauseLoop(padIndex);
            // El estado se actualizará via WebSocket callback
            return;
        }
    }
    
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

function toggleLoop(track) {
    if (ws && ws.readyState === WebSocket.OPEN) {
        const msg = JSON.stringify({
            cmd: 'toggleLoop',
            track: track
        });
        ws.send(msg);
    }
}

function pauseLoop(track) {
    if (ws && ws.readyState === WebSocket.OPEN) {
        const msg = JSON.stringify({
            cmd: 'pauseLoop',
            track: track
        });
        ws.send(msg);
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
}

// Create Sequencer
function createSequencer() {
    const grid = document.getElementById('sequencerGrid');
    const indicator = document.getElementById('stepIndicator');
    const trackNames = ['KICK', 'SNARE', 'CH', 'OH', 'CLAP', 'COW', 'TOM', 'PERC'];
    const trackColors = ['#e74c3c', '#3498db', '#f39c12', '#2ecc71', '#9b59b6', '#e67e22', '#1abc9c', '#95a5a6'];
    
    // 8 tracks x 16 steps (con labels)
    for (let track = 0; track < 8; track++) {
        // Track label con botón mute
        const label = document.createElement('div');
        label.className = 'track-label';
        label.dataset.track = track;
        
        const muteBtn = document.createElement('button');
        muteBtn.className = 'mute-btn';
        muteBtn.textContent = 'M';
        muteBtn.dataset.track = track;
        muteBtn.addEventListener('click', (e) => {
            e.stopPropagation();
            muteBtn.classList.toggle('muted');
            const isMuted = muteBtn.classList.contains('muted');
            
            // Atenuar visualmente los steps de esta pista
            document.querySelectorAll(`.seq-step[data-track="${track}"]`).forEach(step => {
                step.style.opacity = isMuted ? '0.3' : '1';
            });
            
            // Enviar comando de mute al ESP32
            sendWebSocket({
                cmd: 'mute',
                track: track,
                value: isMuted
            });
        });
        
        const name = document.createElement('span');
        name.textContent = trackNames[track];
        name.style.color = trackColors[track];
        
        label.appendChild(muteBtn);
        label.appendChild(name);
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
    
    // Loop Record button
    const loopBtn = document.createElement('button');
    loopBtn.id = 'loopBtn';
    loopBtn.className = 'btn btn-loop';
    loopBtn.textContent = '● REC LOOP';
    loopBtn.addEventListener('click', toggleLoopRecording);
    document.querySelector('.sequencer-controls').appendChild(loopBtn);
    
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
    });
    
    tempoSlider.addEventListener('change', (e) => {
        sendWebSocket({
            cmd: 'tempo',
            value: parseFloat(e.target.value)
        });
    });
    
    // Volume slider
    const volumeSlider = document.getElementById('volumeSlider');
    const volumeValue = document.getElementById('volumeValue');
    
    volumeSlider.addEventListener('input', (e) => {
        const volume = e.target.value;
        volumeValue.textContent = volume;
    });
    
    volumeSlider.addEventListener('change', (e) => {
        const volume = parseInt(e.target.value);
        sendWebSocket({
            cmd: 'setVolume',
            value: volume
        });
        console.log(`Volume set to ${volume}%`);
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
    
    // Kit buttons
    document.querySelectorAll('.btn-kit').forEach(btn => {
        btn.addEventListener('click', (e) => {
            document.querySelectorAll('.btn-kit').forEach(b => b.classList.remove('active'));
            btn.classList.add('active');
            
            const kit = parseInt(btn.dataset.kit);
            
            // Cambiar kit por WebSocket
            sendWebSocket({
                cmd: 'loadKit',
                index: kit
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

function toggleLoopRecording() {
    const loopBtn = document.getElementById('loopBtn');
    
    if (!isRecording) {
        // Iniciar grabación
        isRecording = true;
        recordedSteps = [];
        recordStartTime = Date.now();
        loopBtn.classList.add('recording');
        loopBtn.textContent = '■ STOP REC';
        console.log('Loop recording started');
    } else {
        // Detener y procesar
        isRecording = false;
        loopBtn.classList.remove('recording');
        loopBtn.textContent = '● REC LOOP';
        
        if (recordedSteps.length > 0) {
            processRecordedLoop();
        }
    }
}

function processRecordedLoop() {
    console.log('Processing recorded loop:', recordedSteps);
    
    // Encontrar duración total
    const totalDuration = Math.max(...recordedSteps.map(s => s.time));
    const stepDuration = totalDuration / 16;
    
    // Mapear a steps del sequencer
    recordedSteps.forEach(record => {
        const stepIndex = Math.floor(record.time / stepDuration);
        if (stepIndex < 16) {
            const track = record.pad;
            
            // Activar step en sequencer
            const stepEl = document.querySelector(`[data-track="${track}"][data-step="${stepIndex}"]`);
            if (stepEl && !stepEl.classList.contains('active')) {
                stepEl.classList.add('active');
                sendWebSocket({
                    cmd: 'setStep',
                    track: track,
                    step: stepIndex,
                    active: true
                });
            }
        }
    });
    
    alert(`Loop grabado: ${recordedSteps.length} hits mapeados a 16 steps`);
}

function updateSequencerState(data) {
    document.getElementById('tempoSlider').value = data.tempo;
    document.getElementById('tempoValue').textContent = data.tempo;
    
    // Update playing state
    isPlaying = data.playing || false;
    
    // Update pattern button
    document.querySelectorAll('.btn-pattern').forEach(btn => {
        btn.classList.toggle('active', parseInt(btn.dataset.pattern) === data.pattern);
    });
    
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

function initVisualizers() {
    const spectrumCanvas = document.getElementById('spectrumCanvas');
    const waveformCanvas = document.getElementById('waveformCanvas');
    
    if (!spectrumCanvas || !waveformCanvas) return;
    
    const spectrumCtx = spectrumCanvas.getContext('2d');
    const waveformCtx = waveformCanvas.getContext('2d');
    
    // Set actual canvas size for crisp rendering
    spectrumCanvas.width = 600;
    spectrumCanvas.height = 200;
    waveformCanvas.width = 600;
    waveformCanvas.height = 200;
    
    function drawSpectrum() {
        const width = spectrumCanvas.width;
        const height = spectrumCanvas.height;
        
        // Clear canvas
        spectrumCtx.fillStyle = 'rgba(0, 0, 0, 0.3)';
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
        
        // Draw spectrum bars
        const barWidth = width / spectrumData.length;
        
        for (let i = 0; i < spectrumData.length; i++) {
            const value = spectrumData[i];
            const barHeight = (value / 255) * height;
            
            const x = i * barWidth;
            const y = height - barHeight;
            
            // Create gradient
            const gradient = spectrumCtx.createLinearGradient(x, y, x, height);
            gradient.addColorStop(0, '#FF0000');
            gradient.addColorStop(0.5, '#FF4444');
            gradient.addColorStop(1, '#880000');
            
            spectrumCtx.fillStyle = gradient;
            spectrumCtx.fillRect(x, y, barWidth - 1, barHeight);
            
            // Glow effect on peaks
            if (value > 200) {
                spectrumCtx.shadowBlur = 10;
                spectrumCtx.shadowColor = '#FF0000';
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
    
    function drawWaveform() {
        const width = waveformCanvas.width;
        const height = waveformCanvas.height;
        const centerY = height / 2;
        
        // Clear canvas
        waveformCtx.fillStyle = 'rgba(0, 0, 0, 0.3)';
        waveformCtx.fillRect(0, 0, width, height);
        
        // Draw center line
        waveformCtx.strokeStyle = 'rgba(255, 255, 255, 0.1)';
        waveformCtx.lineWidth = 1;
        waveformCtx.beginPath();
        waveformCtx.moveTo(0, centerY);
        waveformCtx.lineTo(width, centerY);
        waveformCtx.stroke();
        
        // Draw waveform
        waveformCtx.strokeStyle = '#FF0000';
        waveformCtx.lineWidth = 2;
        waveformCtx.shadowBlur = 5;
        waveformCtx.shadowColor = '#FF0000';
        waveformCtx.beginPath();
        
        const sliceWidth = width / waveformData.length;
        
        for (let i = 0; i < waveformData.length; i++) {
            const value = waveformData[i];
            const v = (value / 255.0) * height;
            const y = centerY + (v - centerY);
            const x = i * sliceWidth;
            
            if (i === 0) {
                waveformCtx.moveTo(x, y);
            } else {
                waveformCtx.lineTo(x, y);
            }
        }
        
        waveformCtx.stroke();
        waveformCtx.shadowBlur = 0;
        
        // Draw scale marks
        waveformCtx.fillStyle = '#666';
        waveformCtx.font = '10px Roboto Mono';
        waveformCtx.fillText('+1.0', 5, 15);
        waveformCtx.fillText('0.0', 5, centerY + 5);
        waveformCtx.fillText('-1.0', 5, height - 5);
    }
    
    // Animation loop
    function animate() {
        if (isVisualizerActive) {
            drawSpectrum();
            drawWaveform();
        }
        requestAnimationFrame(animate);
    }
    
    animate();
    console.log('✓ Audio visualizers initialized');
}

// ============= KEYBOARD CONTROLS =============

let isPlaying = false;

function setupKeyboardControls() {
    document.addEventListener('keydown', (e) => {
        // Evitar repetición si ya está presionada
        if (e.repeat) return;
        
        const key = e.key.toUpperCase();
        
        // Números 1-8: Pads con tremolo
        if (key >= '1' && key <= '8') {
            e.preventDefault();
            const padIndex = parseInt(key) - 1;
            
            if (!keyboardPadsActive[padIndex]) {
                keyboardPadsActive[padIndex] = true;
                const padElement = document.querySelector(`.pad[data-pad="${padIndex}"]`);
                if (padElement) {
                    startTremolo(padIndex, padElement);
                }
            }
        }
        
        // SPACE: Toggle Play/Pause
        else if (key === ' ') {
            e.preventDefault();
            togglePlayPause();
        }
        
        // Q: Subir BPM
        else if (key === 'Q') {
            e.preventDefault();
            adjustBPM(5);
        }
        
        // A: Bajar BPM
        else if (key === 'A') {
            e.preventDefault();
            adjustBPM(-5);
        }
        
        // W: Subir Volumen
        else if (key === 'W') {
            e.preventDefault();
            adjustVolume(5);
        }
        
        // S: Bajar Volumen
        else if (key === 'S') {
            e.preventDefault();
            adjustVolume(-5);
        }
    });
    
    document.addEventListener('keyup', (e) => {
        const key = e.key.toUpperCase();
        
        // Números 1-8: Soltar pads
        if (key >= '1' && key <= '8') {
            e.preventDefault();
            const padIndex = parseInt(key) - 1;
            
            if (keyboardPadsActive[padIndex]) {
                keyboardPadsActive[padIndex] = false;
                const padElement = document.querySelector(`.pad[data-pad="${padIndex}"]`);
                if (padElement) {
                    stopTremolo(padIndex, padElement);
                }
            }
        }
    });
    
    console.log('✓ Keyboard controls initialized');
    console.log('  Keys: 1-8=Pads, SPACE=Play/Pause, Q/A=BPM, W/S=Volume');
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
    const volumeSlider = document.getElementById('volumeSlider');
    const volumeValue = document.getElementById('volumeValue');
    
    if (volumeSlider && volumeValue) {
        let currentVolume = parseInt(volumeSlider.value);
        let newVolume = currentVolume + change;
        
        // Limitar entre 0 y 100
        newVolume = Math.max(0, Math.min(100, newVolume));
        
        volumeSlider.value = newVolume;
        volumeValue.textContent = newVolume;
        
        // Enviar al ESP32
        sendWebSocket({
            cmd: 'setVolume',
            value: newVolume
        });
        
        console.log(`Volume: ${newVolume}%`);
    }
}

