/**
 * synth-editor.js
 * Synth Parameter Editor for RED808 Drum Machine
 * Provides waveform visualization on pads and a full parameter modal
 * for TR-808, TR-909, TR-505, and TB-303 synth engines.
 */

// ============================================================
//  PAD → INSTRUMENT MAPPING (mirrors Daisy main.cpp padTo*)
// ============================================================
const PAD_TO_808 = [0,1,3,4,15,2,13,14,5,6,7,12,11,10,9,8];
const PAD_TO_909 = [0,1,3,4,9,2,10,8,5,6,7,2,10,5,6,7];
const PAD_TO_505 = [0,1,3,4,9,2,10,8,5,6,7,8,2,5,6,7];

function padToInstrument(engine, padIndex) {
    if (engine === 0) return PAD_TO_808[padIndex] ?? padIndex;
    if (engine === 1) return PAD_TO_909[padIndex] ?? padIndex;
    if (engine === 2) return PAD_TO_505[padIndex] ?? padIndex;
    return padIndex; // 303 not instrument-based
}

// ============================================================
//  INSTRUMENT NAMES PER ENGINE
// ============================================================
const INST_NAMES_808 = {
    0:'Kick', 1:'Snare', 2:'Clap', 3:'HiHat Closed', 4:'HiHat Open',
    5:'Low Tom', 6:'Mid Tom', 7:'Hi Tom', 8:'Low Conga', 9:'Mid Conga',
    10:'Hi Conga', 11:'Claves', 12:'Maracas', 13:'Rimshot', 14:'Cowbell', 15:'Cymbal'
};
const INST_NAMES_909 = {
    0:'Kick', 1:'Snare', 2:'Clap', 3:'HiHat Closed', 4:'HiHat Open',
    5:'Low Tom', 6:'Mid Tom', 7:'Hi Tom', 8:'Ride', 9:'Crash', 10:'Rimshot'
};
const INST_NAMES_505 = {
    0:'Kick', 1:'Snare', 2:'Clap', 3:'HiHat Closed', 4:'HiHat Open',
    5:'Low Tom', 6:'Mid Tom', 7:'Hi Tom', 8:'Cowbell', 9:'Cymbal', 10:'Rimshot'
};

function getInstrumentName(engine, instrument) {
    if (engine === 0) return INST_NAMES_808[instrument] || `Inst ${instrument}`;
    if (engine === 1) return INST_NAMES_909[instrument] || `Inst ${instrument}`;
    if (engine === 2) return INST_NAMES_505[instrument] || `Inst ${instrument}`;
    if (engine === 3) return 'TB-303 Bass';
    return `Inst ${instrument}`;
}

// ============================================================
//  PARAMETER DEFINITIONS PER ENGINE+INSTRUMENT
//  {id, name, min, max, step, default, unit, paramId}
//  paramId = SYNTH_PARAM_* constant sent to Daisy
// ============================================================
const PARAM_DECAY  = 0;
const PARAM_PITCH  = 1;
const PARAM_TONE   = 2;
const PARAM_VOLUME = 3;
const PARAM_SNAPPY = 4;

// Helper to make param definition
function P(paramId, name, min, max, step, def, unit='') {
    return { paramId, name, min, max, step, default: def, unit };
}

// TR-808 params per instrument index
const PARAMS_808 = {
    0:  [ P(0,'Decay',0,1,0.01,0.45), P(1,'Pitch',20,200,1,55,'Hz'), P(2,'Saturation',0,1,0.01,0.3), P(3,'Volume',0,1,0.01,0.8) ],
    1:  [ P(0,'Decay',0,1,0.01,0.2), P(2,'Tone',0,1,0.01,0.5), P(4,'Snappy',0,1,0.01,0.5), P(3,'Volume',0,1,0.01,0.8) ],
    2:  [ P(0,'Decay',0,1,0.01,0.3), P(3,'Volume',0,1,0.01,0.8) ],
    3:  [ P(0,'Decay',0,0.5,0.005,0.04), P(3,'Volume',0,1,0.01,0.8) ],
    4:  [ P(0,'Decay',0,1,0.01,0.25), P(3,'Volume',0,1,0.01,0.8) ],
    5:  [ P(0,'Decay',0,1,0.01,0.3), P(1,'Pitch',40,200,1,80,'Hz'), P(3,'Volume',0,1,0.01,0.8) ],
    6:  [ P(0,'Decay',0,1,0.01,0.3), P(1,'Pitch',60,300,1,120,'Hz'), P(3,'Volume',0,1,0.01,0.8) ],
    7:  [ P(0,'Decay',0,1,0.01,0.3), P(1,'Pitch',80,400,1,180,'Hz'), P(3,'Volume',0,1,0.01,0.8) ],
    8:  [ P(0,'Decay',0,1,0.01,0.3), P(1,'Pitch',60,300,1,170,'Hz'), P(3,'Volume',0,1,0.01,0.8) ],
    9:  [ P(0,'Decay',0,1,0.01,0.3), P(1,'Pitch',80,400,1,250,'Hz'), P(3,'Volume',0,1,0.01,0.8) ],
    10: [ P(0,'Decay',0,1,0.01,0.3), P(1,'Pitch',100,500,1,370,'Hz'), P(3,'Volume',0,1,0.01,0.8) ],
    11: [ P(3,'Volume',0,1,0.01,0.8) ],
    12: [ P(3,'Volume',0,1,0.01,0.8) ],
    13: [ P(3,'Volume',0,1,0.01,0.8) ],
    14: [ P(0,'Decay',0,0.5,0.005,0.08), P(3,'Volume',0,1,0.01,0.8) ],
    15: [ P(0,'Decay',0,2,0.01,0.8), P(3,'Volume',0,1,0.01,0.8) ],
};

// TR-909 params per instrument index
const PARAMS_909 = {
    0:  [ P(0,'Decay',0,1,0.01,0.4), P(1,'Pitch',20,200,1,50,'Hz'), P(3,'Volume',0,1,0.01,0.8) ],
    1:  [ P(0,'Decay',0,1,0.01,0.25), P(2,'Tone',0,1,0.01,0.5), P(4,'Snappy',0,1,0.01,0.5), P(3,'Volume',0,1,0.01,0.8) ],
    2:  [ P(0,'Decay',0,1,0.01,0.3), P(3,'Volume',0,1,0.01,0.8) ],
    3:  [ P(0,'Decay',0,0.5,0.005,0.04), P(3,'Volume',0,1,0.01,0.8) ],
    4:  [ P(0,'Decay',0,1,0.01,0.3), P(3,'Volume',0,1,0.01,0.8) ],
    5:  [ P(0,'Decay',0,1,0.01,0.3), P(1,'Pitch',40,200,1,80,'Hz'), P(3,'Volume',0,1,0.01,0.8) ],
    6:  [ P(0,'Decay',0,1,0.01,0.3), P(1,'Pitch',60,300,1,120,'Hz'), P(3,'Volume',0,1,0.01,0.8) ],
    7:  [ P(0,'Decay',0,1,0.01,0.3), P(1,'Pitch',80,400,1,180,'Hz'), P(3,'Volume',0,1,0.01,0.8) ],
    8:  [ P(0,'Decay',0,2,0.01,0.5), P(3,'Volume',0,1,0.01,0.8) ],
    9:  [ P(0,'Decay',0,2,0.01,0.8), P(3,'Volume',0,1,0.01,0.8) ],
    10: [ P(3,'Volume',0,1,0.01,0.8) ],
};

// TR-505 params per instrument index
const PARAMS_505 = {
    0:  [ P(0,'Decay',0,1,0.01,0.4), P(1,'Pitch',20,200,1,55,'Hz'), P(3,'Volume',0,1,0.01,0.8) ],
    1:  [ P(0,'Decay',0,1,0.01,0.25), P(2,'Tone',0,1,0.01,0.5), P(3,'Volume',0,1,0.01,0.8) ],
    2:  [ P(0,'Decay',0,1,0.01,0.3), P(3,'Volume',0,1,0.01,0.8) ],
    3:  [ P(0,'Decay',0,0.5,0.005,0.04), P(3,'Volume',0,1,0.01,0.8) ],
    4:  [ P(0,'Decay',0,1,0.01,0.3), P(3,'Volume',0,1,0.01,0.8) ],
    5:  [ P(0,'Decay',0,1,0.01,0.3), P(1,'Pitch',40,200,1,80,'Hz'), P(3,'Volume',0,1,0.01,0.8) ],
    6:  [ P(0,'Decay',0,1,0.01,0.3), P(1,'Pitch',60,300,1,120,'Hz'), P(3,'Volume',0,1,0.01,0.8) ],
    7:  [ P(0,'Decay',0,1,0.01,0.3), P(1,'Pitch',80,400,1,180,'Hz'), P(3,'Volume',0,1,0.01,0.8) ],
    8:  [ P(0,'Decay',0,0.5,0.005,0.1), P(3,'Volume',0,1,0.01,0.8) ],
    9:  [ P(0,'Decay',0,2,0.01,0.8), P(3,'Volume',0,1,0.01,0.8) ],
    10: [ P(3,'Volume',0,1,0.01,0.8) ],
};

// TB-303 params (shared across all pads when engine=303)
const PARAMS_303 = [
    { paramId:0, name:'Cutoff',    min:20,   max:5000, step:1,    default:800,  unit:'Hz' },
    { paramId:1, name:'Resonance', min:0,    max:1,    step:0.01, default:0.5,  unit:'' },
    { paramId:2, name:'Env Mod',   min:0,    max:1,    step:0.01, default:0.5,  unit:'' },
    { paramId:3, name:'Decay',     min:0.05, max:2,    step:0.01, default:0.3,  unit:'s' },
    { paramId:4, name:'Accent',    min:0,    max:1,    step:0.01, default:0.5,  unit:'' },
    { paramId:5, name:'Slide',     min:0.01, max:0.2,  step:0.005,default:0.06, unit:'s' },
    { paramId:6, name:'Waveform',  min:0,    max:1,    step:1,    default:0,    unit:'', labels:['SAW','SQR'] },
    { paramId:7, name:'Volume',    min:0,    max:1,    step:0.01, default:0.7,  unit:'' },
];

function getParamsForPad(engine, padIndex) {
    if (engine === 3) return PARAMS_303;
    const inst = padToInstrument(engine, padIndex);
    if (engine === 0) return PARAMS_808[inst] || [ P(3,'Volume',0,1,0.01,0.8) ];
    if (engine === 1) return PARAMS_909[inst] || [ P(3,'Volume',0,1,0.01,0.8) ];
    if (engine === 2) return PARAMS_505[inst] || [ P(3,'Volume',0,1,0.01,0.8) ];
    return [];
}

// ============================================================
//  CURRENT PARAM STATE (tracks live values per pad)
// ============================================================
const synthParamValues = new Array(16).fill(null).map(() => ({}));
// synthParamValues[padIdx][paramId] = currentValue

function getSynthParamValue(padIndex, engine, paramId) {
    const key = `${engine}_${paramId}`;
    if (synthParamValues[padIndex] && synthParamValues[padIndex][key] !== undefined) {
        return synthParamValues[padIndex][key];
    }
    // Return default
    const params = getParamsForPad(engine, padIndex);
    const p = params.find(pp => pp.paramId === paramId);
    return p ? p.default : 0;
}

function setSynthParamValue(padIndex, engine, paramId, value) {
    const key = `${engine}_${paramId}`;
    if (!synthParamValues[padIndex]) synthParamValues[padIndex] = {};
    synthParamValues[padIndex][key] = value;
}

// ============================================================
//  SYNTH WAVEFORM DRAWING INSIDE PAD
// ============================================================
const ENGINE_COLORS = {
    0: '#c0392b',  // 808 red
    1: '#0076a8',  // 909 blue
    2: '#1e8449',  // 505 green
    3: '#7d3c98',  // 303 violet
};

function drawSynthWaveformInPad(canvas, engine, padIndex) {
    if (!canvas) return;
    const ctx = canvas.getContext('2d');
    const w = canvas.width;
    const h = canvas.height;
    const color = ENGINE_COLORS[engine] || '#fff';
    const inst = (engine < 3) ? padToInstrument(engine, padIndex) : -1;

    ctx.clearRect(0, 0, w, h);

    // Dark bg with subtle grid
    ctx.fillStyle = 'rgba(0,0,0,0.6)';
    ctx.fillRect(0, 0, w, h);
    ctx.strokeStyle = 'rgba(255,255,255,0.06)';
    ctx.lineWidth = 0.5;
    for (let x = 0; x < w; x += w/8) {
        ctx.beginPath(); ctx.moveTo(x,0); ctx.lineTo(x,h); ctx.stroke();
    }
    // Center line
    ctx.strokeStyle = 'rgba(255,255,255,0.1)';
    ctx.beginPath(); ctx.moveTo(0,h/2); ctx.lineTo(w,h/2); ctx.stroke();

    // Draw waveform
    ctx.beginPath();
    ctx.strokeStyle = color;
    ctx.lineWidth = 2;
    ctx.shadowColor = color;
    ctx.shadowBlur = 4;

    const mid = h / 2;
    const amp = h * 0.38;

    if (engine === 3) {
        // TB-303: saw or square wave
        const wf = getSynthParamValue(padIndex, 3, 6); // waveform param
        const cutoff = getSynthParamValue(padIndex, 3, 0);
        const cutoffNorm = Math.min(1, cutoff / 5000);
        const cycles = 3 + Math.floor(cutoffNorm * 3);
        if (wf < 0.5) {
            // Sawtooth
            for (let x = 0; x < w; x++) {
                const phase = (x / w) * cycles;
                const frac = phase - Math.floor(phase);
                const y = mid - amp * (2 * frac - 1) * Math.exp(-x / w * 0.5);
                x === 0 ? ctx.moveTo(x, y) : ctx.lineTo(x, y);
            }
        } else {
            // Square
            for (let x = 0; x < w; x++) {
                const phase = (x / w) * cycles;
                const frac = phase - Math.floor(phase);
                const y = mid - amp * (frac < 0.5 ? 1 : -1) * Math.exp(-x / w * 0.5);
                x === 0 ? ctx.moveTo(x, y) : ctx.lineTo(x, y);
            }
        }
    } else {
        // Percussion waveform based on instrument type
        const decay = getSynthParamValue(padIndex, engine, 0) || 0.3;
        const isKick = (inst === 0);
        const isSnare = (inst === 1);
        const isHat = (inst === 3 || inst === 4);
        const isTom = (inst >= 5 && inst <= 7);

        if (isKick) {
            // Kick: decaying sine with pitch drop
            const pitch = getSynthParamValue(padIndex, engine, 1) || 55;
            const freq = pitch / 20;
            for (let x = 0; x < w; x++) {
                const t = x / w;
                const env = Math.exp(-t / (decay * 0.5 + 0.05));
                const freqSweep = freq + (freq * 2) * Math.exp(-t * 15);
                const y = mid - amp * Math.sin(t * freqSweep * Math.PI * 2) * env;
                x === 0 ? ctx.moveTo(x, y) : ctx.lineTo(x, y);
            }
        } else if (isSnare) {
            // Snare: sine + noise
            for (let x = 0; x < w; x++) {
                const t = x / w;
                const env = Math.exp(-t / (decay * 0.3 + 0.03));
                const tone = Math.sin(t * 32 * Math.PI) * 0.6;
                const noise = (Math.random() - 0.5) * 0.8;
                const y = mid - amp * (tone + noise) * env;
                x === 0 ? ctx.moveTo(x, y) : ctx.lineTo(x, y);
            }
        } else if (isHat) {
            // HiHat: noise burst
            for (let x = 0; x < w; x++) {
                const t = x / w;
                const env = Math.exp(-t / (decay * 0.3 + 0.01));
                const noise = (Math.random() - 0.5) * 1.4;
                const y = mid - amp * noise * env;
                x === 0 ? ctx.moveTo(x, y) : ctx.lineTo(x, y);
            }
        } else if (isTom) {
            // Tom: decaying sine
            const pitch = getSynthParamValue(padIndex, engine, 1) || 120;
            const freq = pitch / 30;
            for (let x = 0; x < w; x++) {
                const t = x / w;
                const env = Math.exp(-t / (decay * 0.4 + 0.04));
                const y = mid - amp * Math.sin(t * freq * Math.PI * 2) * env;
                x === 0 ? ctx.moveTo(x, y) : ctx.lineTo(x, y);
            }
        } else {
            // Generic percussion: short attack + decay
            for (let x = 0; x < w; x++) {
                const t = x / w;
                const env = Math.exp(-t / (decay * 0.3 + 0.02));
                const sig = Math.sin(t * 20 * Math.PI) * 0.5 + (Math.random()-0.5)*0.5;
                const y = mid - amp * sig * env;
                x === 0 ? ctx.moveTo(x, y) : ctx.lineTo(x, y);
            }
        }
    }
    ctx.stroke();
    ctx.shadowBlur = 0;

    // Glow fill underneath
    ctx.globalAlpha = 0.08;
    ctx.lineTo(w, mid);
    ctx.lineTo(0, mid);
    ctx.closePath();
    ctx.fillStyle = color;
    ctx.fill();
    ctx.globalAlpha = 1.0;
}

// ============================================================
//  PAD SYNTH OVERLAY (waveform + param buttons)
// ============================================================
function createSynthOverlay(padIndex) {
    const pad = document.querySelector(`.pad[data-pad="${padIndex}"]`);
    if (!pad) return;

    // Remove existing overlay
    const existing = pad.querySelector('.synth-overlay');
    if (existing) existing.remove();

    const engine = (typeof padSynthEngine !== 'undefined') ? padSynthEngine[padIndex] : -1;
    if (engine < 0) return; // No synth mode

    const overlay = document.createElement('div');
    overlay.className = 'synth-overlay';

    // Waveform canvas — fill the pad
    const canvas = document.createElement('canvas');
    canvas.className = 'synth-pad-waveform';
    canvas.width = 220;
    canvas.height = 80;
    overlay.appendChild(canvas);
    pad.appendChild(overlay);

    // Params button — positioned at top of pad, between loop btn and upload btn
    const existingBtn = pad.querySelector('.synth-params-btn');
    if (existingBtn) existingBtn.remove();
    const paramsBtn = document.createElement('button');
    paramsBtn.className = 'synth-params-btn';
    paramsBtn.innerHTML = '🎛️';
    paramsBtn.title = 'Edit synth parameters';
    const stopProp = (e) => { e.stopPropagation(); };
    paramsBtn.addEventListener('touchstart', stopProp);
    paramsBtn.addEventListener('mousedown', stopProp);
    paramsBtn.addEventListener('click', (e) => {
        e.preventDefault();
        e.stopPropagation();
        openSynthModal(padIndex);
    });
    paramsBtn.addEventListener('touchend', (e) => {
        e.preventDefault();
        e.stopPropagation();
        openSynthModal(padIndex);
    });
    pad.appendChild(paramsBtn);

    // Draw initial waveform
    drawSynthWaveformInPad(canvas, engine, padIndex);
}

function removeSynthOverlay(padIndex) {
    const pad = document.querySelector(`.pad[data-pad="${padIndex}"]`);
    if (!pad) return;
    const ov = pad.querySelector('.synth-overlay');
    if (ov) ov.remove();
    const btn = pad.querySelector('.synth-params-btn');
    if (btn) btn.remove();
}

function refreshSynthOverlay(padIndex) {
    const engine = (typeof padSynthEngine !== 'undefined') ? padSynthEngine[padIndex] : -1;
    if (engine >= 0) {
        createSynthOverlay(padIndex);
    } else {
        removeSynthOverlay(padIndex);
    }
}

// Refresh all pads
function refreshAllSynthOverlays() {
    for (let i = 0; i < 16; i++) refreshSynthOverlay(i);
}

// ============================================================
//  SYNTH PARAMETER MODAL
// ============================================================
let synthModalPad = -1;
let synthModalEngine = -1;

function openSynthModal(padIndex) {
    const engine = (typeof padSynthEngine !== 'undefined') ? padSynthEngine[padIndex] : -1;
    if (engine < 0) return;

    synthModalPad = padIndex;
    synthModalEngine = engine;

    const inst = (engine < 3) ? padToInstrument(engine, padIndex) : -1;
    const instName = getInstrumentName(engine, inst);
    const engineLabel = ['TR-808','TR-909','TR-505','TB-303'][engine] || '';
    const params = getParamsForPad(engine, padIndex);
    const color = ENGINE_COLORS[engine];
    const padName = (typeof padNames !== 'undefined') ? padNames[padIndex] : `Pad ${padIndex+1}`;

    // Create or reuse modal
    let modal = document.getElementById('synthParamModal');
    if (!modal) {
        modal = document.createElement('div');
        modal.id = 'synthParamModal';
        modal.className = 'synth-modal-overlay';
        document.body.appendChild(modal);
    }

    modal.innerHTML = `
        <div class="synth-modal" data-engine="${engine}">
            <div class="synth-modal-header">
                <div class="synth-modal-title">
                    <span class="synth-modal-engine-badge" style="background:${color}">${engineLabel}</span>
                    <span class="synth-modal-inst-name">${padName} — ${instName}</span>
                </div>
                <div class="synth-modal-actions">
                    <button class="synth-modal-trigger-btn" id="synthModalTrigger" title="Preview sound">▶ TEST</button>
                    <button class="synth-modal-close-btn" id="synthModalClose">✕</button>
                </div>
            </div>
            <div class="synth-modal-waveform-wrap">
                <canvas id="synthModalCanvas" width="600" height="120"></canvas>
            </div>
            <div class="synth-modal-params" id="synthModalParams"></div>
        </div>
    `;

    // Build parameter sliders
    const paramsContainer = modal.querySelector('#synthModalParams');
    params.forEach(p => {
        const currentVal = getSynthParamValue(padIndex, engine, p.paramId);
        const paramRow = document.createElement('div');
        paramRow.className = 'synth-param-row';

        if (p.labels) {
            // Toggle buttons (for waveform: SAW/SQR)
            paramRow.innerHTML = `
                <label class="synth-param-label">${p.name}</label>
                <div class="synth-param-toggle-group" data-param-id="${p.paramId}">
                    ${p.labels.map((lbl, idx) => `
                        <button class="synth-param-toggle-btn ${currentVal == idx ? 'active' : ''}"
                                data-value="${idx}">${lbl}</button>
                    `).join('')}
                </div>
            `;
            paramRow.querySelectorAll('.synth-param-toggle-btn').forEach(btn => {
                btn.addEventListener('click', (e) => {
                    e.preventDefault();
                    const val = parseFloat(btn.dataset.value);
                    paramRow.querySelectorAll('.synth-param-toggle-btn').forEach(b => b.classList.remove('active'));
                    btn.classList.add('active');
                    onSynthParamChange(padIndex, engine, p.paramId, val);
                });
            });
        } else {
            // Slider
            const pct = ((currentVal - p.min) / (p.max - p.min)) * 100;
            paramRow.innerHTML = `
                <label class="synth-param-label">${p.name}</label>
                <input type="range" class="synth-param-slider" data-param-id="${p.paramId}"
                       min="${p.min}" max="${p.max}" step="${p.step}" value="${currentVal}">
                <span class="synth-param-value" data-param-id="${p.paramId}">
                    ${formatParamValue(currentVal, p.unit)}
                </span>
            `;
            const slider = paramRow.querySelector('.synth-param-slider');
            const valSpan = paramRow.querySelector('.synth-param-value');

            let sendTimeout = null;
            slider.addEventListener('input', () => {
                const val = parseFloat(slider.value);
                valSpan.textContent = formatParamValue(val, p.unit);
                // Throttled send
                if (sendTimeout) clearTimeout(sendTimeout);
                sendTimeout = setTimeout(() => {
                    onSynthParamChange(padIndex, engine, p.paramId, val);
                }, 30);
            });
            slider.addEventListener('change', () => {
                if (sendTimeout) clearTimeout(sendTimeout);
                const val = parseFloat(slider.value);
                onSynthParamChange(padIndex, engine, p.paramId, val);
            });
        }

        paramsContainer.appendChild(paramRow);
    });

    // Draw large waveform
    const bigCanvas = modal.querySelector('#synthModalCanvas');
    if (bigCanvas) {
        drawSynthWaveformModal(bigCanvas, engine, padIndex);
    }

    // Event listeners
    modal.querySelector('#synthModalClose').addEventListener('click', closeSynthModal);
    modal.querySelector('#synthModalTrigger').addEventListener('click', () => {
        triggerSynthPreview(padIndex, engine);
    });
    modal.addEventListener('click', (e) => {
        if (e.target === modal) closeSynthModal();
    });

    // Show modal
    requestAnimationFrame(() => {
        modal.classList.add('active');
    });
}

function closeSynthModal() {
    const modal = document.getElementById('synthParamModal');
    if (modal) {
        modal.classList.remove('active');
        setTimeout(() => { modal.innerHTML = ''; }, 300);
    }
    synthModalPad = -1;
    synthModalEngine = -1;
}

function formatParamValue(val, unit) {
    if (unit === 'Hz') return `${Math.round(val)} Hz`;
    if (unit === 's') return `${val.toFixed(2)}s`;
    if (val >= 0 && val <= 1 && !unit) return `${Math.round(val * 100)}%`;
    return val.toFixed(2);
}

function drawSynthWaveformModal(canvas, engine, padIndex) {
    if (!canvas) return;
    const ctx = canvas.getContext('2d');
    const w = canvas.width;
    const h = canvas.height;
    const color = ENGINE_COLORS[engine] || '#fff';
    const inst = (engine < 3) ? padToInstrument(engine, padIndex) : -1;

    ctx.clearRect(0,0,w,h);

    // Panel background
    const bg = ctx.createLinearGradient(0,0,0,h);
    bg.addColorStop(0, 'rgba(18,20,28,0.95)');
    bg.addColorStop(1, 'rgba(6,8,14,0.98)');
    ctx.fillStyle = bg;
    ctx.fillRect(0,0,w,h);

    // Grid
    ctx.strokeStyle = 'rgba(255,255,255,0.05)';
    ctx.lineWidth = 0.5;
    for (let x = 0; x < w; x += 30) {
        ctx.beginPath(); ctx.moveTo(x,0); ctx.lineTo(x,h); ctx.stroke();
    }
    for (let y = 0; y < h; y += 20) {
        ctx.beginPath(); ctx.moveTo(0,y); ctx.lineTo(w,y); ctx.stroke();
    }
    // Center line
    ctx.strokeStyle = 'rgba(255,255,255,0.12)';
    ctx.beginPath(); ctx.moveTo(0,h/2); ctx.lineTo(w,h/2); ctx.stroke();

    // Waveform - same logic as pad but bigger
    ctx.beginPath();
    ctx.strokeStyle = color;
    ctx.lineWidth = 2.5;
    ctx.shadowColor = color;
    ctx.shadowBlur = 8;

    const mid = h / 2;
    const amp = h * 0.4;

    if (engine === 3) {
        const wf = getSynthParamValue(padIndex, 3, 6);
        const cutoff = getSynthParamValue(padIndex, 3, 0);
        const reso = getSynthParamValue(padIndex, 3, 1);
        const cutoffNorm = Math.min(1, cutoff / 5000);
        const cycles = 4 + Math.floor(cutoffNorm * 4);
        for (let x = 0; x < w; x++) {
            const t = x / w;
            const phase = t * cycles;
            const frac = phase - Math.floor(phase);
            let sig;
            if (wf < 0.5) {
                sig = 2 * frac - 1; // Sawtooth
            } else {
                sig = frac < 0.5 ? 1 : -1; // Square
            }
            // Simulate filter effect
            const filterEnv = 1.0 - (1.0 - cutoffNorm) * 0.5;
            const resoBoost = 1 + reso * 0.3;
            const y = mid - amp * sig * filterEnv * resoBoost * Math.exp(-t * 0.3);
            x === 0 ? ctx.moveTo(x, y) : ctx.lineTo(x, y);
        }
    } else {
        const decay = getSynthParamValue(padIndex, engine, 0) || 0.3;
        const isKick = (inst === 0);
        const isSnare = (inst === 1);
        const isHat = (inst === 3 || inst === 4);
        const isTom = (inst >= 5 && inst <= 7);

        if (isKick) {
            const pitch = getSynthParamValue(padIndex, engine, 1) || 55;
            const freq = pitch / 15;
            for (let x = 0; x < w; x++) {
                const t = x / w;
                const env = Math.exp(-t / (decay * 0.5 + 0.05));
                const freqSweep = freq + (freq * 2.5) * Math.exp(-t * 15);
                const y = mid - amp * Math.sin(t * freqSweep * Math.PI * 2) * env;
                x === 0 ? ctx.moveTo(x, y) : ctx.lineTo(x, y);
            }
        } else if (isSnare) {
            for (let x = 0; x < w; x++) {
                const t = x / w;
                const env = Math.exp(-t / (decay * 0.3 + 0.03));
                const tone = Math.sin(t * 40 * Math.PI) * 0.6;
                const noise = (Math.random() - 0.5) * 0.8;
                const y = mid - amp * (tone + noise) * env;
                x === 0 ? ctx.moveTo(x, y) : ctx.lineTo(x, y);
            }
        } else if (isHat) {
            for (let x = 0; x < w; x++) {
                const t = x / w;
                const env = Math.exp(-t / (decay * 0.3 + 0.01));
                const noise = (Math.random() - 0.5) * 1.4;
                const y = mid - amp * noise * env;
                x === 0 ? ctx.moveTo(x, y) : ctx.lineTo(x, y);
            }
        } else if (isTom) {
            const pitch = getSynthParamValue(padIndex, engine, 1) || 120;
            const freq = pitch / 25;
            for (let x = 0; x < w; x++) {
                const t = x / w;
                const env = Math.exp(-t / (decay * 0.4 + 0.04));
                const y = mid - amp * Math.sin(t * freq * Math.PI * 2) * env;
                x === 0 ? ctx.moveTo(x, y) : ctx.lineTo(x, y);
            }
        } else {
            for (let x = 0; x < w; x++) {
                const t = x / w;
                const env = Math.exp(-t / (decay * 0.3 + 0.02));
                const sig = Math.sin(t * 24 * Math.PI)*0.5 + (Math.random()-0.5)*0.5;
                const y = mid - amp * sig * env;
                x === 0 ? ctx.moveTo(x, y) : ctx.lineTo(x, y);
            }
        }
    }
    ctx.stroke();
    ctx.shadowBlur = 0;

    // Glow fill
    ctx.globalAlpha = 0.06;
    ctx.lineTo(w, mid);
    ctx.lineTo(0, mid);
    ctx.closePath();
    ctx.fillStyle = color;
    ctx.fill();
    ctx.globalAlpha = 1.0;
}

// ============================================================
//  WEBSOCKET: SEND PARAM CHANGES
// ============================================================
function onSynthParamChange(padIndex, engine, paramId, value) {
    // Store locally
    setSynthParamValue(padIndex, engine, paramId, value);

    // Send to ESP32 via WebSocket
    if (typeof ws !== 'undefined' && ws && ws.readyState === WebSocket.OPEN) {
        if (engine === 3) {
            // TB-303 global param
            ws.send(JSON.stringify({
                cmd: 'synth303Param',
                paramId: paramId,
                value: value
            }));
        } else {
            // TR-808/909/505 per-instrument param
            const instrument = padToInstrument(engine, padIndex);
            ws.send(JSON.stringify({
                cmd: 'synthParam',
                engine: engine,
                instrument: instrument,
                paramId: paramId,
                value: value
            }));
        }
    }

    // Redraw waveforms
    refreshPadWaveform(padIndex);
    refreshModalWaveform();
}

function refreshPadWaveform(padIndex) {
    const pad = document.querySelector(`.pad[data-pad="${padIndex}"]`);
    if (!pad) return;
    const canvas = pad.querySelector('.synth-pad-waveform');
    const engine = (typeof padSynthEngine !== 'undefined') ? padSynthEngine[padIndex] : -1;
    if (canvas && engine >= 0) {
        drawSynthWaveformInPad(canvas, engine, padIndex);
    }
}

function refreshModalWaveform() {
    if (synthModalPad < 0 || synthModalEngine < 0) return;
    const canvas = document.getElementById('synthModalCanvas');
    if (canvas) {
        drawSynthWaveformModal(canvas, synthModalEngine, synthModalPad);
    }
}

function triggerSynthPreview(padIndex, engine) {
    if (typeof triggerSynthPad === 'function') {
        triggerSynthPad(padIndex);
    }
}

// ============================================================
//  HOOK: called by app.js when synth engine changes on a pad
// ============================================================
function onSynthEngineChanged(padIndex, engine) {
    if (engine >= 0) {
        createSynthOverlay(padIndex);
    } else {
        removeSynthOverlay(padIndex);
    }
}

// ============================================================
//  KEYBOARD SHORTCUT: ESC closes modal
// ============================================================
document.addEventListener('keydown', (e) => {
    if (e.key === 'Escape') {
        const modal = document.getElementById('synthParamModal');
        if (modal && modal.classList.contains('active')) {
            closeSynthModal();
            e.preventDefault();
            e.stopPropagation();
        }
    }
});

// ============================================================
//  INIT: called after pads are created
// ============================================================
function initSynthEditor() {
    // Will be called from app.js after createPads()
    refreshAllSynthOverlays();
    console.log('[SynthEditor] Initialized');
}
