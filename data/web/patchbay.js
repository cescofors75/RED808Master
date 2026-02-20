/* ============================================================
   RED808 PATCHBAY — Signal Routing Engine v1.0
   Visual modular cable patching for the RED808 drum machine
   ============================================================ */
(function(){
'use strict';

/* ──────────── CONFIG ──────────── */
const TRACK_NAMES = [
  'BD','SD','CH','OH','CP','CB','CY','HC',
  'HT','MT','LT','LC','MC','RS','MA','CL'
];
const TRACK_LABELS = [
  'KICK','SNARE','CL.HAT','OP.HAT','CLAP','COWBELL','CYMBAL','HI CONGA',
  'HI TOM','MID TOM','LO TOM','LO CONGA','MID CONGA','RIMSHOT','MARACAS','CLAVES'
];
const PAD_COLORS = [
  '#ff2d2d','#ff6b35','#ffa726','#ffd600',
  '#c6ff00','#69f0ae','#26c6da','#448aff',
  '#7c4dff','#ea80fc','#ff4081','#f50057',
  '#ff1744','#d500f9','#651fff','#00e676'
];
const INITIAL_SOURCE_COUNT = 0;

const FX_DEFS = {
  // ── FILTERS ── (cutoff clamped 100–16000 Hz en firmware; resonance clamped 0.5–20; gain clamped ±12 dB)
  lowpass:    { label:'LOW PASS',    color:'cyan',   icon:'⏣', params:[ {id:'cutoff', label:'Cutoff', min:100, max:16000, def:1000, unit:'Hz', log:true}, {id:'resonance', label:'Res', min:0.5, max:20, def:0.707, step:0.01} ] },
  highpass:   { label:'HI PASS',     color:'cyan',   icon:'⏣', params:[ {id:'cutoff', label:'Cutoff', min:100, max:16000, def:1000, unit:'Hz', log:true}, {id:'resonance', label:'Res', min:0.5, max:20, def:0.707, step:0.01} ] },
  bandpass:   { label:'BAND PASS',   color:'blue',   icon:'⏣', params:[ {id:'cutoff', label:'Cutoff', min:100, max:16000, def:1000, unit:'Hz', log:true}, {id:'resonance', label:'Q', min:0.5, max:20, def:1, step:0.01} ] },
  notch:     { label:'NOTCH',     color:'silver',  icon:'⏣', params:[ {id:'cutoff', label:'Cutoff', min:100, max:16000, def:800,  unit:'Hz', log:true}, {id:'resonance', label:'Q',   min:0.5, max:20, def:1,     step:0.01} ] },
  allpass:   { label:'ALL PASS',  color:'slate',   icon:'⏣', params:[ {id:'cutoff', label:'Freq',   min:100, max:16000, def:1000, unit:'Hz', log:true}, {id:'resonance', label:'Q',   min:0.5, max:20, def:0.707, step:0.01} ] },
  peaking:   { label:'PEAKING',   color:'amber',   icon:'⏣', params:[ {id:'cutoff', label:'Freq',   min:100, max:16000, def:1000, unit:'Hz', log:true}, {id:'resonance', label:'Q',   min:0.5, max:20, def:1,     step:0.01}, {id:'gain', label:'Gain', min:-12, max:12, def:6, unit:'dB', step:0.5} ] },
  lowshelf:  { label:'LO SHELF',  color:'lime',    icon:'⏣', params:[ {id:'cutoff', label:'Freq',   min:100, max:5000,  def:200,  unit:'Hz', log:true}, {id:'gain', label:'Gain', min:-12, max:12, def:6, unit:'dB', step:0.5} ] },
  highshelf: { label:'HI SHELF',  color:'lavender',icon:'⏣', params:[ {id:'cutoff', label:'Freq',   min:200,max:16000, def:8000, unit:'Hz', log:true}, {id:'gain', label:'Gain', min:-12, max:12, def:6, unit:'dB', step:0.5} ] },
  resonant:  { label:'RESONANT',  color:'hotpink', icon:'⏣', params:[ {id:'cutoff', label:'Cutoff', min:100, max:16000, def:800,  unit:'Hz', log:true}, {id:'resonance', label:'Res', min:0.5, max:20, def:5,     step:0.1}  ] },
  // ── FX ──
  echo:       { label:'REVERB',      color:'orange', icon:'◎', params:[ {id:'time', label:'Time', min:10, max:750, def:200, unit:'ms'}, {id:'feedback', label:'Feedback', min:0, max:95, def:40, unit:'%'}, {id:'mix', label:'Mix', min:0, max:100, def:50, unit:'%'} ] },
  delay:      { label:'DELAY',       color:'yellow', icon:'◉', params:[ {id:'time', label:'Time', min:10, max:750, def:300, unit:'ms'}, {id:'feedback', label:'Feedback', min:0, max:95, def:50, unit:'%'}, {id:'mix', label:'Mix', min:0, max:100, def:50, unit:'%'} ] },
  bitcrusher: { label:'BITCRUSHER',  color:'purple', icon:'▦', params:[ {id:'bits', label:'Bit Depth', min:1, max:16, def:8, step:1} ] },
  distortion: { label:'DISTORTION',  color:'pink',   icon:'⚡', params:[ {id:'amount', label:'Amount', min:0, max:100, def:50, unit:'%'}, {id:'mode', label:'Mode', type:'select', options:['SOFT','HARD','TUBE','FUZZ'], def:0} ] },
  compressor: { label:'COMPRESSOR',  color:'green',  icon:'▬', params:[ {id:'threshold', label:'Threshold', min:0, max:100, def:60, unit:'%'}, {id:'ratio', label:'Ratio', min:1, max:20, def:4, step:0.5} ] },
  flanger:    { label:'FLANGER',     color:'teal',   icon:'≋', params:[ {id:'rate', label:'Rate', min:0, max:100, def:30, unit:'%'}, {id:'depth', label:'Depth', min:0, max:100, def:50, unit:'%'}, {id:'feedback', label:'Feedback', min:0, max:90, def:40, unit:'%'} ] },
  phaser:     { label:'PHASER',      color:'violet', icon:'◐', params:[ {id:'rate', label:'Rate', min:0, max:100, def:30, unit:'%'}, {id:'depth', label:'Depth', min:0, max:100, def:50, unit:'%'}, {id:'feedback', label:'Feedback', min:0, max:90, def:40, unit:'%'} ] }
};

const FILTER_TYPE_MAP = { lowpass:1, highpass:2, bandpass:3, notch:4, allpass:5, peaking:6, lowshelf:7, highshelf:8, resonant:9 };

const FACTORY_PRESETS = [
  {
    id: 'tight-bus',
    name: 'TIGHT BUS',
    description: 'HPF + COMP para pegada limpia',
    chain: [
      { key: 'hpf', fxType: 'highpass', x: 760, y: 540, params: { cutoff: 55, resonance: 0.82 } },
      { key: 'comp', fxType: 'compressor', x: 1060, y: 540, params: { threshold: 62, ratio: 5 } }
    ]
  },
  {
    id: 'lofi-crunch',
    name: 'LOFI CRUNCH',
    description: 'Bitcrusher + Distortion + Delay',
    chain: [
      { key: 'crush', fxType: 'bitcrusher', x: 760, y: 820, params: { bits: 7 } },
      { key: 'dist', fxType: 'distortion', x: 1060, y: 820, params: { amount: 58, mode: 2 } },
      { key: 'dly', fxType: 'delay', x: 1360, y: 820, params: { time: 280, feedback: 44, mix: 36 } }
    ]
  },
  {
    id: 'space-wide',
    name: 'SPACE WIDE',
    description: 'BandPass + Phaser + Reverb + Comp',
    chain: [
      { key: 'bp', fxType: 'bandpass', x: 760, y: 1100, params: { cutoff: 1450, resonance: 1.1 } },
      { key: 'ph', fxType: 'phaser', x: 1060, y: 1100, params: { rate: 34, depth: 62, feedback: 28 } },
      { key: 'rv', fxType: 'echo', x: 1360, y: 1100, params: { time: 240, feedback: 26, mix: 40 } },
      { key: 'cp', fxType: 'compressor', x: 1660, y: 1100, params: { threshold: 56, ratio: 3.5 } }
    ]
  }
];

const CANVAS_W = 2800;
const CANVAS_H = 2200;
const SNAP = 20;

/* ──────────── STATE ──────────── */
let nodes = [];
let cables = [];
let nextId = 1;
let ws = null;
let wsConnected = false;
let isPlaying = false;
let currentTempo = 120;
let currentStep = -1;
let stepPulseTimer = null;
let serverPlaying = null;
let stepDrivenPlaying = false;
let stepDrivenTimer = null;
let signalDemoMode = false;
let viewZoom = 1;
let gridVisible = true;
let heatMode = 'on'; // 'off' | 'on' | 'auto'
let sceneQuantizeEnabled = true;
let pendingSceneState = null;
let pendingSceneLabel = '';
let trackVolumes = new Array(16).fill(100);
let trackPeaks = new Array(16).fill(0);
const throttledCmdTimers = new Map();
const queuedWsPayloads = [];
let wsFlushTimer = null;
let lastWsSendTs = 0;
const WS_MIN_SEND_GAP_MS = 12;
const WS_MAX_QUEUE = 240;
const WS_BOOT_SYNC_DELAY_MS = 34;
const WS_BOOT_SYNC_MAX_CABLES = 48;
let activeMacroScene = 'A';
let macroScenes = {
  A: [45, 35, 55, 40],
  B: [70, 20, 68, 55],
  C: [25, 62, 34, 22],
  D: [55, 48, 72, 68]
};

/* Drag state */
let drag = null;   // { type:'node'|'cable', nodeId, startX, startY, offsetX, offsetY, fromId, fromType }
let previewPath = null;
let selectedCable = null;
let editingNode = null;
let activeTool = 'patch'; // 'patch' | 'hand' | 'select'
const selectedNodeIds = new Set();

/* DOM refs */
let svgEl, nodesEl, canvasEl, worldEl, selectionBoxEl;

function setPBMenuOpen(open) {
  const menu = document.getElementById('pbMenu');
  const backdrop = document.getElementById('pbMenuBackdrop');
  if (!menu || !backdrop) return;
  menu.classList.toggle('hidden', !open);
  backdrop.classList.toggle('hidden', !open);
}

function updateToolButtons() {
  const handBtn = document.getElementById('pbToolHandBtn');
  const selectBtn = document.getElementById('pbToolSelectBtn');
  if (handBtn) handBtn.classList.toggle('is-active', activeTool === 'hand');
  if (selectBtn) selectBtn.classList.toggle('is-active', activeTool === 'select');

  if (canvasEl) {
    canvasEl.classList.toggle('pb-tool-hand', activeTool === 'hand');
    canvasEl.classList.toggle('pb-tool-select', activeTool === 'select');
  }
}

function clearNodeSelection() {
  selectedNodeIds.clear();
  renderNodeSelection();
}

function renderNodeSelection() {
  if (!nodesEl) return;
  nodesEl.querySelectorAll('.pb-node').forEach(el => {
    const nodeId = el.dataset.nodeId;
    el.classList.toggle('is-selected', selectedNodeIds.has(nodeId));
  });
}

function selectNodesInWorldRect(x1, y1, x2, y2, additive = false) {
  if (!additive) selectedNodeIds.clear();

  const left = Math.min(x1, x2);
  const right = Math.max(x1, x2);
  const top = Math.min(y1, y2);
  const bottom = Math.max(y1, y2);

  nodes.forEach(node => {
    const width = node.type === 'pad' || node.type === 'fx' || node.type === 'master' || node.type === 'bus' ? 170 : 170;
    const height = node.type === 'pad' ? 72 : 80;
    const intersects = !(node.x + width < left || node.x > right || node.y + height < top || node.y > bottom);
    if (intersects) selectedNodeIds.add(node.id);
  });

  renderNodeSelection();
}

function showSelectionBoxWorldRect(x1, y1, x2, y2) {
  if (!selectionBoxEl) return;
  const left = Math.min(x1, x2);
  const right = Math.max(x1, x2);
  const top = Math.min(y1, y2);
  const bottom = Math.max(y1, y2);

  const vx = left * viewZoom - canvasEl.scrollLeft;
  const vy = top * viewZoom - canvasEl.scrollTop;
  const vw = Math.max(1, (right - left) * viewZoom);
  const vh = Math.max(1, (bottom - top) * viewZoom);

  selectionBoxEl.style.display = 'block';
  selectionBoxEl.style.left = `${vx}px`;
  selectionBoxEl.style.top = `${vy}px`;
  selectionBoxEl.style.width = `${vw}px`;
  selectionBoxEl.style.height = `${vh}px`;
}

function hideSelectionBox() {
  if (!selectionBoxEl) return;
  selectionBoxEl.style.display = 'none';
}

function setActiveTool(nextTool) {
  const target = (nextTool === 'hand' || nextTool === 'select') ? nextTool : 'patch';
  activeTool = (activeTool === target) ? 'patch' : target;

  if (activeTool !== 'select') {
    clearNodeSelection();
    hideSelectionBox();
  }
  updateToolButtons();
}

/* ──────────── INIT ──────────── */
function init() {
  svgEl   = document.getElementById('pbSVG');
  nodesEl = document.getElementById('pbNodes');
  canvasEl= document.getElementById('pbCanvas');
  worldEl = document.getElementById('pbWorld');
  selectionBoxEl = document.getElementById('pbSelectionBox');
  updateToolButtons();
  renderNodeSelection();

  /* Set canvas size */
  nodesEl.style.width  = CANVAS_W + 'px';
  nodesEl.style.height = CANVAS_H + 'px';
  svgEl.setAttribute('width',  CANVAS_W);
  svgEl.setAttribute('height', CANVAS_H);
  svgEl.style.width  = CANVAS_W + 'px';
  svgEl.style.height = CANVAS_H + 'px';

  loadViewPrefs();
  applyZoom();
  applyGridVisibility();
  updateHeatModeButton();
  updateQuantizeButton();
  loadMacroScenes();
  initMacroPanel();

  /* Create initial sampler sources */
  for (let i = 0; i < Math.min(INITIAL_SOURCE_COUNT, TRACK_NAMES.length); i++) {
    createPadNode(i, 60, 60 + i * 120);
  }
  /* Create Master Out */
  createMasterNode(CANVAS_W - 260, 500);
  createBusNode('bus-a', 'BUS A', CANVAS_W - 560, 380, 'teal');
  createBusNode('bus-b', 'BUS B', CANVAS_W - 560, 640, 'violet');

  /* Do not autoload patch topology: start from a clean PATH canvas */
  refreshSourcePicker();

  /* Events */
  canvasEl.addEventListener('pointerdown', onPointerDown);
  canvasEl.addEventListener('pointermove', onPointerMove);
  canvasEl.addEventListener('pointerup', onPointerUp);
  canvasEl.addEventListener('pointercancel', onPointerUp);
  document.addEventListener('keydown', onKeyDown);
  document.addEventListener('click', onDocClick);

  /* WebSocket */
  connectWS();

  applyTempoVisuals();

  updateStatus();
}

function parsePadTrackId(nodeId) {
  if (typeof nodeId !== 'string' || !nodeId.startsWith('pad-')) return -1;
  const track = parseInt(nodeId.slice(4), 10);
  if (!Number.isInteger(track) || track < 0 || track >= TRACK_NAMES.length) return -1;
  return track;
}

function getMissingPadTracks() {
  const used = new Set(nodes.filter(n => n.type === 'pad').map(n => n.track));
  const missing = [];
  for (let track = 0; track < TRACK_NAMES.length; track++) {
    if (!used.has(track)) missing.push(track);
  }
  return missing;
}

function getActivePadTracks() {
  return nodes
    .filter(n => n.type === 'pad')
    .map(n => n.track)
    .filter(track => Number.isInteger(track) && track >= 0 && track < TRACK_NAMES.length)
    .sort((a, b) => a - b);
}

function getNextPadSlotY() {
  const padNodes = nodes.filter(n => n.type === 'pad');
  if (!padNodes.length) return 60;
  const maxY = padNodes.reduce((acc, node) => Math.max(acc, node.y), 60);
  return snap(maxY + 120);
}

function refreshSourcePicker() {
  const picker = document.getElementById('pbSourcePicker');
  const activePicker = document.getElementById('pbActiveSourcePicker');
  if (!picker) return;

  const addBtn = document.getElementById('pbAddSourceBtn');
  const removeBtn = document.getElementById('pbRemoveSourceBtn');
  const addAllBtn = document.getElementById('pbAddAllSourcesBtn');
  const countEl = document.getElementById('pbSourceCount');
  const missing = getMissingPadTracks();
  const activeTracks = getActivePadTracks();
  const activeCount = activeTracks.length;

  picker.innerHTML = '';
  if (missing.length) {
    missing.forEach(track => {
      const opt = document.createElement('option');
      opt.value = String(track);
      opt.textContent = `PAD ${track + 1} · ${TRACK_LABELS[track] || TRACK_NAMES[track]}`;
      picker.appendChild(opt);
    });
  } else {
    const opt = document.createElement('option');
    opt.value = '';
    opt.textContent = 'All sampler sources added';
    picker.appendChild(opt);
  }

  const disabled = !missing.length;
  picker.disabled = disabled;
  if (addBtn) addBtn.disabled = disabled;
  if (addAllBtn) addAllBtn.disabled = disabled;

  if (activePicker) {
    activePicker.innerHTML = '';
    if (activeTracks.length) {
      activeTracks.forEach(track => {
        const opt = document.createElement('option');
        opt.value = String(track);
        opt.textContent = `PAD ${track + 1} · ${TRACK_LABELS[track] || TRACK_NAMES[track]}`;
        activePicker.appendChild(opt);
      });
      activePicker.disabled = false;
    } else {
      const opt = document.createElement('option');
      opt.value = '';
      opt.textContent = 'No active sampler sources';
      activePicker.appendChild(opt);
      activePicker.disabled = true;
    }
  }

  if (removeBtn) removeBtn.disabled = !activeTracks.length;
  if (countEl) countEl.textContent = `${activeCount}/${TRACK_NAMES.length}`;
}

function addPadSource(track) {
  const parsedTrack = Number(track);
  if (!Number.isInteger(parsedTrack) || parsedTrack < 0 || parsedTrack >= TRACK_NAMES.length) return false;
  if (nodes.find(n => n.type === 'pad' && n.track === parsedTrack)) return false;

  createPadNode(parsedTrack, 60, getNextPadSlotY());
  updateStatus();
  saveState();
  refreshSourcePicker();
  return true;
}

function removePadSource(track) {
  const parsedTrack = Number(track);
  if (!Number.isInteger(parsedTrack) || parsedTrack < 0 || parsedTrack >= TRACK_NAMES.length) return false;

  const padNode = nodes.find(n => n.type === 'pad' && n.track === parsedTrack);
  if (!padNode) return false;

  const relatedCables = cables.filter(c => c.from === padNode.id || c.to === padNode.id);
  relatedCables.forEach(cable => clearConnection(cable));
  cables = cables.filter(c => c.from !== padNode.id && c.to !== padNode.id);

  const nodeEl = document.getElementById('node-' + padNode.id);
  if (nodeEl) nodeEl.remove();
  nodes = nodes.filter(n => n.id !== padNode.id);
  selectedNodeIds.delete(padNode.id);
  renderNodeSelection();

  if (selectedCable && !cables.find(c => c.id === selectedCable)) {
    selectedCable = null;
  }

  renderCables();
  updateStatus();
  saveState();
  refreshSourcePicker();
  return true;
}

function ensurePadNodesForState(data) {
  const requiredTracks = new Set();

  if (Array.isArray(data?.padPositions)) {
    data.padPositions.forEach(pp => {
      const track = parsePadTrackId(pp?.id);
      if (track >= 0) requiredTracks.add(track);
    });
  }

  if (Array.isArray(data?.cables)) {
    data.cables.forEach(cd => {
      const fromTrack = parsePadTrackId(cd?.from);
      const toTrack = parsePadTrackId(cd?.to);
      if (fromTrack >= 0) requiredTracks.add(fromTrack);
      if (toTrack >= 0) requiredTracks.add(toTrack);
    });
  }

  requiredTracks.forEach(track => {
    if (!nodes.find(n => n.type === 'pad' && n.track === track)) {
      createPadNode(track, 60, 60 + track * 120);
    }
  });
}

/* ──────────── WEBSOCKET ──────────── */
function connectWS() {
  const proto = location.protocol === 'https:' ? 'wss' : 'ws';
  ws = new WebSocket(proto + '://' + location.host + '/ws');
  ws.onopen = () => {
    wsConnected = true;
    document.getElementById('pbWsStatus').classList.add('connected');
    flushWsQueue();
    syncConnectionsAfterReconnect();
    sendCmd('getTrackVolumes', {});
  };
  ws.onclose = () => {
    wsConnected = false;
    document.getElementById('pbWsStatus').classList.remove('connected');
    queuedWsPayloads.length = 0;
    if (wsFlushTimer) {
      clearTimeout(wsFlushTimer);
      wsFlushTimer = null;
    }
    setTimeout(connectWS, 3000);
  };
  ws.onerror = () => ws.close();
  ws.onmessage = (e) => {
    if (e.data instanceof ArrayBuffer) {
      ingestAudioLevelBuffer(new Uint8Array(e.data));
      return;
    }
    if (typeof e.data !== 'string') return;
    try { handleWSMessage(JSON.parse(e.data)); } catch(ex) {}
  };
}

/* Reset all per-track FX in firmware to match an empty patchbay canvas.
   Sent staggered to avoid WS burst on reconnect. */
function resetFirmwareFX() {
  for (let t = 0; t < 16; t++) {
    const track = t;
    const delay = t * WS_BOOT_SYNC_DELAY_MS;
    setTimeout(() => {
      sendCmd('clearTrackFilter',   { track });
      sendCmd('clearTrackFX',       { track });
      sendCmd('setTrackEcho',       { track, active: false, time: 200, feedback: 40, mix: 50 });
      sendCmd('setTrackFlanger',    { track, active: false, rate: 30, depth: 50, feedback: 40 });
      sendCmd('setTrackCompressor', { track, active: false, threshold: 60, ratio: 4 });
    }, delay);
  }
  /* Also reset master-level FX that might be stale */
  setTimeout(() => sendCmd('setPhaserActive', { value: false }), 16 * WS_BOOT_SYNC_DELAY_MS);
  console.log('[PATCH] Firmware FX reset enviado (canvas vacío).');
}

function syncConnectionsAfterReconnect() {
  if (!Array.isArray(cables) || cables.length === 0) {
    /* Canvas vacío — resetea estado FX del firmware para que coincida */
    resetFirmwareFX();
    return;
  }
  const total = cables.length;
  const count = Math.min(total, WS_BOOT_SYNC_MAX_CABLES);
  for (let i = 0; i < count; i++) {
    setTimeout(() => applyConnection(cables[i]), i * WS_BOOT_SYNC_DELAY_MS);
  }
  if (total > count) {
    console.warn(`[PATCH] Sync limitado: ${count}/${total} cables para evitar sobrecarga del ESP32`);
  }
}

function flushWsQueue() {
  wsFlushTimer = null;
  if (!ws || ws.readyState !== 1) return;
  if (!queuedWsPayloads.length) return;

  const now = performance.now();
  const elapsed = now - lastWsSendTs;
  if (elapsed < WS_MIN_SEND_GAP_MS) {
    wsFlushTimer = setTimeout(flushWsQueue, WS_MIN_SEND_GAP_MS - elapsed);
    return;
  }

  const payload = queuedWsPayloads.shift();
  ws.send(payload);
  lastWsSendTs = performance.now();

  if (queuedWsPayloads.length) {
    wsFlushTimer = setTimeout(flushWsQueue, WS_MIN_SEND_GAP_MS);
  }
}

function sendCmd(cmd, data) {
  if (!ws || ws.readyState !== 1) return;
  const payload = JSON.stringify(Object.assign({ cmd }, data));

  if (queuedWsPayloads.length >= WS_MAX_QUEUE) {
    queuedWsPayloads.shift();
  }
  queuedWsPayloads.push(payload);

  if (!wsFlushTimer) {
    flushWsQueue();
  }
}

function sendCmdThrottled(key, cmd, data, delay = 70) {
  const prev = throttledCmdTimers.get(key);
  if (prev) clearTimeout(prev);
  const tid = setTimeout(() => {
    throttledCmdTimers.delete(key);
    sendCmd(cmd, data);
  }, Math.max(0, delay));
  throttledCmdTimers.set(key, tid);
}

function getMacroSliderValues() {
  const values = [];
  for (let i = 1; i <= 4; i++) {
    const el = document.getElementById(`pbMacro${i}`);
    values.push(el ? parseInt(el.value || '0', 10) : 0);
  }
  return values.map(v => Number.isNaN(v) ? 0 : Math.max(0, Math.min(100, v)));
}

function setMacroSliderValues(values) {
  if (!Array.isArray(values)) return;
  for (let i = 1; i <= 4; i++) {
    const el = document.getElementById(`pbMacro${i}`);
    if (!el) continue;
    const v = Math.max(0, Math.min(100, parseInt(values[i - 1] ?? 0, 10) || 0));
    el.value = String(v);
  }
}

function updateMacroSceneButtons() {
  ['A','B','C','D'].forEach(scene => {
    const btn = document.getElementById(`pbMacroScene${scene}`);
    if (!btn) return;
    btn.classList.toggle('is-active', scene === activeMacroScene);
  });
}

function saveMacroScenes() {
  try {
    localStorage.setItem('pb_macro_scenes', JSON.stringify({ active: activeMacroScene, scenes: macroScenes }));
  } catch(ex) {}
}

function loadMacroScenes() {
  try {
    const raw = localStorage.getItem('pb_macro_scenes');
    if (!raw) return;
    const parsed = JSON.parse(raw);
    if (parsed && parsed.scenes) {
      ['A','B','C','D'].forEach(scene => {
        if (Array.isArray(parsed.scenes[scene]) && parsed.scenes[scene].length >= 4) {
          macroScenes[scene] = parsed.scenes[scene].slice(0, 4);
        }
      });
    }
    if (parsed && ['A','B','C','D'].includes(parsed.active)) {
      activeMacroScene = parsed.active;
    }
  } catch(ex) {}
}

function applyMacroValue(index, value) {
  const v = Math.max(0, Math.min(100, value));
  if (index === 1) {
    const cutoff = Math.round(200 + (v / 100) * 11800);
    sendCmdThrottled('macro:m1', 'setFilterCutoff', { value: cutoff }, 60);
  } else if (index === 2) {
    sendCmdThrottled('macro:m2:active', 'setDelayActive', { value: v > 0 }, 80);
    sendCmdThrottled('macro:m2:mix', 'setDelayMix', { value: v }, 80);
  } else if (index === 3) {
    const threshold = -50 + (v / 100) * 44;
    sendCmdThrottled('macro:m3:active', 'setCompressorActive', { value: v > 0 }, 90);
    sendCmdThrottled('macro:m3:thr', 'setCompressorThreshold', { value: threshold }, 90);
  } else if (index === 4) {
    sendCmdThrottled('macro:m4:sidechain', 'setSidechainPro', {
      active: v > 0,
      source: 0,
      destinations: [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15],
      amount: v,
      attack: 6,
      release: 180,
      knee: 0.45
    }, 130);
  }
}

function applyMacroSceneValues(values) {
  const safe = (Array.isArray(values) ? values : [0,0,0,0]).slice(0, 4);
  setMacroSliderValues(safe);
  for (let i = 0; i < 4; i++) {
    applyMacroValue(i + 1, parseInt(safe[i], 10) || 0);
  }
}

function initMacroPanel() {
  // Only initialise UI — do NOT send WS commands on page load.
  // applyMacroSceneValues() will be called explicitly by the user via pbSelectMacroScene/pbLoadMacroScene.
  setMacroSliderValues(macroScenes[activeMacroScene]);
  updateMacroSceneButtons();

  for (let i = 1; i <= 4; i++) {
    const el = document.getElementById(`pbMacro${i}`);
    if (!el) continue;
    el.addEventListener('input', () => {
      const values = getMacroSliderValues();
      macroScenes[activeMacroScene] = values;
      applyMacroValue(i, values[i - 1]);
      saveMacroScenes();
    });
  }
  // Do NOT call applyMacroSceneValues here — avoid sending sidechain/FX commands on every page load.
}

window.pbSelectMacroScene = function(scene) {
  if (!['A','B','C','D'].includes(scene)) return;
  activeMacroScene = scene;
  updateMacroSceneButtons();
  applyMacroSceneValues(macroScenes[scene]);
  saveMacroScenes();
};

window.pbSaveMacroScene = function() {
  macroScenes[activeMacroScene] = getMacroSliderValues();
  saveMacroScenes();
};

window.pbLoadMacroScene = function() {
  applyMacroSceneValues(macroScenes[activeMacroScene]);
};

function handleWSMessage(msg) {
  if ((msg.type === 'playState' || msg.type === 'sequencerState' || msg.type === 'status' || msg.type === 'state') && typeof msg.playing !== 'undefined') {
    isPlaying = !!msg.playing;
    serverPlaying = !!msg.playing;
    if (!serverPlaying) {
      stepDrivenPlaying = false;
      if (stepDrivenTimer) {
        clearTimeout(stepDrivenTimer);
        stepDrivenTimer = null;
      }
    }
    updatePlayingVisualState();
  }

  if (typeof msg.tempo !== 'undefined') {
    const parsedTempo = parseFloat(msg.tempo);
    if (!Number.isNaN(parsedTempo)) {
      currentTempo = parsedTempo;
      applyTempoVisuals();
    }
  }

  if (typeof msg.step !== 'undefined') {
    const parsedStep = parseInt(msg.step, 10);
    if (!Number.isNaN(parsedStep) && parsedStep !== currentStep) {
      currentStep = parsedStep;
      triggerStepPulse();
      if (pendingSceneState && parsedStep % 4 === 0) {
        applyQueuedSceneState();
      }
      if (serverPlaying !== true) {
        stepDrivenPlaying = true;
        updatePlayingVisualState();
        if (stepDrivenTimer) clearTimeout(stepDrivenTimer);
        stepDrivenTimer = setTimeout(() => {
          stepDrivenPlaying = false;
          updatePlayingVisualState();
        }, 850);
      }
    }
  }

  if (Array.isArray(msg.trackVolumes)) {
    ingestTrackVolumes(msg.trackVolumes);
  }

  if (msg.type === 'trackVolumes' && Array.isArray(msg.volumes)) {
    ingestTrackVolumes(msg.volumes);
  }

  if ((msg.type === 'trackVolumeSet' || msg.type === 'trackVolume') && typeof msg.track !== 'undefined' && typeof msg.volume !== 'undefined') {
    setTrackVolume(msg.track, msg.volume);
  }

  /* Update sample names on pads if available */
  if (msg.type === 'sampleLoaded' || msg.type === 'kitLoaded') {
    // Could update pad labels here
  }

  /* Per-track filter feedback — warn if firmware rejected (max 8 active) */
  if (msg.type === 'trackFilterSet' && msg.success === false) {
    const statusEl = document.getElementById('pbStatus');
    if (statusEl) {
      statusEl.textContent = '⚠ Límite: máx 8 filtros de track activos simultáneos';
      statusEl.style.color = '#ff8a50';
      setTimeout(() => { statusEl.style.color = ''; updateStatus(); }, 4000);
    }
    console.warn('[PATCH] setTrackFilter rechazado por firmware (límite 8 activos)');
  }
}

function applyTempoVisuals() {
  const bpm = Math.max(40, Math.min(300, Number(currentTempo) || 120));
  const beat = 60 / bpm;
  const flowDuration = Math.max(0.22, Math.min(1.4, beat / 2));
  const pulseDuration = Math.max(0.18, Math.min(0.95, beat / 4));
  const root = document.getElementById('patchbay');
  if (!root) return;
  root.style.setProperty('--pb-flow-duration', `${flowDuration.toFixed(3)}s`);
  root.style.setProperty('--pb-pulse-duration', `${pulseDuration.toFixed(3)}s`);
}

function updatePlayingVisualState() {
  const root = document.getElementById('patchbay');
  if (!root) return;
  const active = !!(signalDemoMode || serverPlaying || stepDrivenPlaying || isPlaying);
  root.classList.toggle('pb-playing', active);
}

function triggerStepPulse() {
  const root = document.getElementById('patchbay');
  if (!root) return;
  root.classList.remove('pb-step-hit');
  void root.offsetWidth;
  root.classList.add('pb-step-hit');
  if (stepPulseTimer) clearTimeout(stepPulseTimer);
  stepPulseTimer = setTimeout(() => root.classList.remove('pb-step-hit'), 110);
}

function clampTrackVolume(v) {
  const n = Number(v);
  if (Number.isNaN(n)) return 100;
  return Math.max(0, Math.min(127, n));
}

function setTrackVolume(track, volume) {
  const idx = parseInt(track, 10);
  if (Number.isNaN(idx) || idx < 0 || idx >= 16) return;
  trackVolumes[idx] = clampTrackVolume(volume);
  renderCables();
}

function ingestTrackVolumes(volumes) {
  if (!Array.isArray(volumes)) return;
  for (let i = 0; i < 16; i++) {
    if (typeof volumes[i] !== 'undefined') {
      trackVolumes[i] = clampTrackVolume(volumes[i]);
    }
  }
  renderCables();
}

function ingestAudioLevelBuffer(buf) {
  if (!buf || buf.length < 18) return;
  if (buf[0] !== 0xAA) return;
  for (let i = 0; i < 16; i++) {
    trackPeaks[i] = Math.max(0, Math.min(1, (buf[i + 1] || 0) / 255));
  }
  renderCables();
}

/* ──────────── NODE CREATION ──────────── */
function createPadNode(track, x, y) {
  const id = 'pad-' + track;
  if (nodes.find(n => n.id === id)) return;
  const node = {
    id, type: 'pad', track,
    x: snap(x), y: snap(y),
    label: 'PAD ' + (track + 1),
    sub: TRACK_LABELS[track] || TRACK_NAMES[track],
    color: PAD_COLORS[track]
  };
  nodes.push(node);
  renderNode(node);
  return node;
}

function createFxNode(fxType, x, y) {
  const def = FX_DEFS[fxType];
  if (!def) return null;
  const id = 'fx-' + nextId++;
  const params = {};
  def.params.forEach(p => { params[p.id] = p.def; });
  const node = {
    id, type: 'fx', fxType,
    x: snap(x), y: snap(y),
    label: def.label,
    color: def.color,
    params
  };
  nodes.push(node);
  renderNode(node);
  updateStatus();
  saveState();
  return node;
}

function createMasterNode(x, y) {
  const id = 'master';
  if (nodes.find(n => n.id === id)) return;
  const node = {
    id, type: 'master',
    x: snap(x), y: snap(y),
    label: 'MASTER OUT',
    sub: '🔊 STEREO',
    color: 'gold'
  };
  nodes.push(node);
  renderNode(node);
}

function createBusNode(id, label, x, y, color) {
  if (nodes.find(n => n.id === id)) return;
  const node = {
    id,
    type: 'bus',
    x: snap(x),
    y: snap(y),
    label,
    sub: 'SUB MIX',
    color: color || 'teal'
  };
  nodes.push(node);
  renderNode(node);
}

/* ──────────── NODE RENDERING ──────────── */
function renderNode(node) {
  let el = document.getElementById('node-' + node.id);
  if (el) el.remove();

  el = document.createElement('div');
  el.id = 'node-' + node.id;
  el.className = 'pb-node';
  el.dataset.nodeId = node.id;
  el.style.left = node.x + 'px';
  el.style.top  = node.y + 'px';
  if (selectedNodeIds.has(node.id)) {
    el.classList.add('is-selected');
  }

  if (node.type === 'pad') {
    el.classList.add('pb-pad');
    el.style.borderColor = node.color + '66';
    el.innerHTML = `
      <div class="pb-node-header" style="color:${node.color}">${node.label}</div>
      <div class="pb-node-sub">${node.sub}</div>
      <div class="pb-connector out" data-node="${node.id}" data-dir="out" style="background:${node.color};border-color:${node.color};box-shadow:0 0 8px ${node.color}80"></div>
    `;
  } else if (node.type === 'fx') {
    const def = FX_DEFS[node.fxType];
    el.classList.add('pb-fx');
    el.setAttribute('data-color', def.color);
    const paramText = def.params.map(p => {
      const v = node.params[p.id];
      const disp = p.type === 'select' ? p.options[v] : (p.unit ? v + p.unit : v);
      return p.label + ': ' + disp;
    }).join('  ·  ');
    el.innerHTML = `
      <button class="pb-node-delete" data-node="${node.id}" onclick="pbDeleteNode('${node.id}')">✕</button>
      <button class="pb-node-edit" data-node="${node.id}" onclick="pbEditNode('${node.id}')">⚙</button>
      <div class="pb-node-header">${def.icon} ${node.label}</div>
      <div class="pb-node-params">${paramText}</div>
      <div class="pb-connector in" data-node="${node.id}" data-dir="in"></div>
      <div class="pb-connector out" data-node="${node.id}" data-dir="out"></div>
    `;
  } else if (node.type === 'master') {
    el.classList.add('pb-master');
    el.setAttribute('data-color', 'gold');
    el.innerHTML = `
      <div class="pb-node-header">${node.label}</div>
      <div class="pb-node-sub">${node.sub}</div>
      <div class="pb-connector in" data-node="${node.id}" data-dir="in"></div>
    `;
  } else if (node.type === 'bus') {
    el.classList.add('pb-fx');
    el.setAttribute('data-color', node.color || 'teal');
    el.innerHTML = `
      <div class="pb-node-header">⎍ ${node.label}</div>
      <div class="pb-node-sub">${node.sub || 'SUB MIX'}</div>
      <div class="pb-connector in" data-node="${node.id}" data-dir="in"></div>
      <div class="pb-connector out" data-node="${node.id}" data-dir="out"></div>
    `;
  }

  nodesEl.appendChild(el);
}

function updateNodeDisplay(node) {
  if (node.type !== 'fx') return;
  const el = document.getElementById('node-' + node.id);
  if (!el) return;
  const def = FX_DEFS[node.fxType];
  const paramEl = el.querySelector('.pb-node-params');
  if (paramEl) {
    paramEl.textContent = def.params.map(p => {
      const v = node.params[p.id];
      const disp = p.type === 'select' ? p.options[v] : (p.unit ? Math.round(v) + p.unit : Math.round(v*100)/100);
      return p.label + ': ' + disp;
    }).join('  ·  ');
  }
}

/* ──────────── CABLE RENDERING ──────────── */
function getConnectorPos(nodeId, dir) {
  const el = document.querySelector(`#node-${CSS.escape(nodeId)} .pb-connector.${dir}`);
  if (!el) return null;
  const nodeEl = el.parentElement;
  const nx = parseFloat(nodeEl.style.left);
  const ny = parseFloat(nodeEl.style.top);
  const cr = 8;
  if (dir === 'out') {
    return { x: nx + nodeEl.offsetWidth + cr - 1, y: ny + nodeEl.offsetHeight / 2 };
  } else {
    return { x: nx - cr + 1, y: ny + nodeEl.offsetHeight / 2 };
  }
}

function getCableBezier(x1, y1, x2, y2) {
  const dx = Math.abs(x2 - x1);
  const cp = Math.max(80, dx * 0.45);
  return `M${x1},${y1} C${x1+cp},${y1} ${x2-cp},${y2} ${x2},${y2}`;
}

function renderCables() {
  /* Remove old cable paths */
  svgEl.querySelectorAll('path:not(.cable-preview)').forEach(p => p.remove());

  cables.forEach((cable, index) => {
    const fromPos = getConnectorPos(cable.from, 'out');
    const toPos   = getConnectorPos(cable.to, 'in');
    if (!fromPos || !toPos) return;

    const path = document.createElementNS('http://www.w3.org/2000/svg', 'path');
    const baseWidth = getCableStrokeWidth(cable);
    const levelNorm = getCableLevelNorm(cable);
    const cableStroke = isHeatActive() ? getCableHeatColor(cable.color, levelNorm) : cable.color;
    path.setAttribute('d', getCableBezier(fromPos.x, fromPos.y, toPos.x, toPos.y));
    path.setAttribute('stroke', cableStroke);
    path.setAttribute('stroke-width', String(baseWidth.toFixed(2)));
    path.setAttribute('fill', 'none');
    path.setAttribute('stroke-linecap', 'round');
    path.setAttribute('filter', 'url(#cableGlow)');
    path.dataset.cableId = cable.id;
    path.classList.add('pb-cable');
    path.style.setProperty('--flow-delay', `${(index % 8) * 0.09}s`);
    path.style.pointerEvents = 'stroke';
    path.style.cursor = 'pointer';
    if (selectedCable === cable.id) {
      path.classList.add('is-selected');
      path.setAttribute('stroke-width', String((baseWidth + 1.8).toFixed(2)));
      path.setAttribute('stroke-dasharray', '8 4');
    }
    path.addEventListener('click', (e) => {
      e.stopPropagation();
      onCableClick(cable.id);
    });
    svgEl.appendChild(path);
  });
}

function getCableSourceTracks(cable) {
  const fromNode = nodes.find(n => n.id === cable.from);
  if (!fromNode) return [];
  if (fromNode.type === 'pad') return [fromNode.track];
  if (fromNode.type === 'fx') return getTracksForNode(fromNode.id);
  if (fromNode.type === 'bus') return getTracksForNode(fromNode.id);
  return [];
}

function getCableStrokeWidth(cable) {
  const avg = getCableAverageVolume(cable);
  if (avg < 0) return 2.4;
  return 1.6 + (avg / 127) * 4.4;
}

function getCableAverageVolume(cable) {
  const tracks = getCableSourceTracks(cable);
  if (!tracks.length) return -1;
  const sum = tracks.reduce((acc, track) => acc + clampTrackVolume(trackVolumes[track]), 0);
  return sum / tracks.length;
}

function getCableAveragePeak(cable) {
  const tracks = getCableSourceTracks(cable);
  if (!tracks.length) return -1;
  const sum = tracks.reduce((acc, track) => acc + (trackPeaks[track] || 0), 0);
  return sum / tracks.length;
}

function getCableLevelNorm(cable) {
  const avg = getCableAverageVolume(cable);
  const p = getCableAveragePeak(cable);
  if (avg < 0 && p < 0) return 0.5;
  const volNorm = avg < 0 ? 0.5 : Math.max(0, Math.min(1, avg / 127));
  const peakNorm = p < 0 ? 0.0 : Math.max(0, Math.min(1, p));
  return Math.max(0, Math.min(1, volNorm * 0.35 + peakNorm * 0.65));
}

function getCableHeatColor(baseHex, levelNorm) {
  const c = hexToRgb(baseHex);
  if (!c) return baseHex;

  const cool = { r: 90, g: 170, b: 255 };
  const hot = { r: 255, g: 62, b: 72 };

  const warmMix = Math.max(0, Math.min(1, levelNorm));
  const target = {
    r: Math.round(cool.r + (hot.r - cool.r) * warmMix),
    g: Math.round(cool.g + (hot.g - cool.g) * warmMix),
    b: Math.round(cool.b + (hot.b - cool.b) * warmMix)
  };

  const blendBase = 0.45;
  const out = {
    r: Math.round(c.r * (1 - blendBase) + target.r * blendBase),
    g: Math.round(c.g * (1 - blendBase) + target.g * blendBase),
    b: Math.round(c.b * (1 - blendBase) + target.b * blendBase)
  };
  return rgbToHex(out.r, out.g, out.b);
}

function hexToRgb(hex) {
  if (typeof hex !== 'string') return null;
  const clean = hex.trim().replace('#', '');
  if (!/^[0-9a-fA-F]{6}$/.test(clean)) return null;
  const num = parseInt(clean, 16);
  return {
    r: (num >> 16) & 255,
    g: (num >> 8) & 255,
    b: num & 255
  };
}

function rgbToHex(r, g, b) {
  const rr = Math.max(0, Math.min(255, r)) | 0;
  const gg = Math.max(0, Math.min(255, g)) | 0;
  const bb = Math.max(0, Math.min(255, b)) | 0;
  const v = (rr << 16) | (gg << 8) | bb;
  return '#' + v.toString(16).padStart(6, '0');
}

function renderPreviewCable(x1, y1, x2, y2, color) {
  let pv = svgEl.querySelector('.cable-preview');
  if (!pv) {
    pv = document.createElementNS('http://www.w3.org/2000/svg', 'path');
    pv.classList.add('cable-preview');
    pv.setAttribute('fill', 'none');
    pv.setAttribute('stroke-width', '2.5');
    pv.setAttribute('stroke-linecap', 'round');
    svgEl.appendChild(pv);
  }
  pv.setAttribute('d', getCableBezier(x1, y1, x2, y2));
  pv.setAttribute('stroke', color);
}

function clearPreviewCable() {
  const pv = svgEl.querySelector('.cable-preview');
  if (pv) pv.remove();
}

/* ──────────── CABLE LOGIC ──────────── */
function addCable(fromId, toId) {
  if (!fromId || !toId) return null;
  if (fromId === toId) return null;

  const fromNode = nodes.find(n => n.id === fromId);
  const toNode = nodes.find(n => n.id === toId);
  if (!fromNode || !toNode) return null;

  if (fromNode.type === 'master') return null;
  if (toNode.type === 'pad') return null;

  /* Validate: no duplicate */
  if (cables.find(c => c.from === fromId && c.to === toId)) return null;
  if (wouldCreateCycle(fromId, toId)) {
    alert('Conexión no válida: crearía un bucle infinito en el routing');
    return null;
  }

  /* Get color from source node */
  let color = '#ffffff';
  if (fromNode && fromNode.type === 'pad') {
    color = fromNode.color;
  } else if (fromNode && fromNode.type === 'fx') {
    const def = FX_DEFS[fromNode.fxType];
    const cmap = {cyan:'#00e5ff',orange:'#ff9100',yellow:'#ffd600',purple:'#b388ff',pink:'#ff4081',green:'#69f0ae',teal:'#26c6da',violet:'#ea80fc',blue:'#448aff',gold:'#ffd700'};
    color = cmap[def.color] || '#ffffff';
  } else if (fromNode && fromNode.type === 'bus') {
    color = getColorHex(fromNode.color || 'teal');
  }

  const cable = { id: 'cable-' + nextId++, from: fromId, to: toId, color };
  cables.push(cable);
  renderCables();
  updateStatus();
  applyConnection(cable);
  saveState();
  return cable;
}

function removeCable(id) {
  const idx = cables.findIndex(c => c.id === id);
  if (idx < 0) return;
  const cable = cables[idx];
  clearConnection(cable);
  cables.splice(idx, 1);
  renderCables();
  updateStatus();
  saveState();
}

function onCableClick(id) {
  selectedCable = (selectedCable === id) ? null : id;
  renderCables();
}

/* ──────────── EFFECT APPLICATION ──────────── */
function getTracksForNode(nodeId) {
  /* Find all pads connected upstream to this node */
  const tracks = [];
  const visited = new Set();
  function walkUp(nid) {
    if (visited.has(nid)) return;
    visited.add(nid);
    cables.filter(c => c.to === nid).forEach(c => {
      const fromNode = nodes.find(n => n.id === c.from);
      if (fromNode && fromNode.type === 'pad') {
        if (!tracks.includes(fromNode.track)) tracks.push(fromNode.track);
      } else if (fromNode) {
        walkUp(fromNode.id);
      }
    });
  }
  walkUp(nodeId);
  return tracks;
}

function hasPath(startId, targetId, visited = new Set()) {
  if (startId === targetId) return true;
  if (visited.has(startId)) return false;
  visited.add(startId);
  const nextNodes = cables.filter(c => c.from === startId).map(c => c.to);
  for (const nid of nextNodes) {
    if (hasPath(nid, targetId, visited)) return true;
  }
  return false;
}

function wouldCreateCycle(fromId, toId) {
  return hasPath(toId, fromId);
}

function applyConnection(cable) {
  const toNode = nodes.find(n => n.id === cable.to);
  if (!toNode || toNode.type !== 'fx') return;

  /* Find which tracks feed into this FX node */
  const tracks = getTracksForNode(cable.to);
  tracks.forEach(track => applyFxToTrack(track, toNode));
}

function clearConnection(cable) {
  const toNode = nodes.find(n => n.id === cable.to);
  if (!toNode || toNode.type !== 'fx') return;

  const fromNode = nodes.find(n => n.id === cable.from);
  const tracksToRemove = [];

  if (fromNode && fromNode.type === 'pad') {
    /* Direct pad → fx cable */
    tracksToRemove.push(fromNode.track);
  } else if (fromNode) {
    /* fx → fx (or bus → fx) cable: propagate upstream to find all pad tracks */
    tracksToRemove.push(...getTracksForNode(cable.from));
  }

  tracksToRemove.forEach(track => clearFxFromTrack(track, toNode));
}

function applyFxToTrack(track, fxNode) {
  const p = fxNode.params;
  const ft = fxNode.fxType;

  switch(ft) {
    case 'lowpass':
    case 'highpass':
    case 'bandpass':
    case 'notch':
    case 'allpass':
    case 'resonant':
      sendCmd('setTrackFilter', { track, filterType: FILTER_TYPE_MAP[ft], cutoff: p.cutoff, resonance: p.resonance || p['Q'] || 0.707 });
      break;
    case 'peaking':
    case 'lowshelf':
    case 'highshelf':
      sendCmd('setTrackFilter', { track, filterType: FILTER_TYPE_MAP[ft], cutoff: p.cutoff, resonance: p.resonance || 1, gain: p.gain || 0 });
      break;
    case 'bitcrusher':
      sendCmd('setTrackBitCrush', { track, value: Math.round(p.bits) });
      break;
    case 'distortion':
      sendCmd('setTrackDistortion', { track, amount: p.amount / 100, mode: p.mode || 0 });
      break;
    case 'echo':
    case 'delay':
      sendCmd('setTrackEcho', { track, active: true, time: p.time, feedback: p.feedback, mix: p.mix });
      break;
    case 'flanger':
      sendCmd('setTrackFlanger', { track, active: true, rate: p.rate, depth: p.depth, feedback: p.feedback });
      break;
    case 'compressor':
      sendCmd('setTrackCompressor', { track, active: true, threshold: p.threshold, ratio: p.ratio });
      break;
    case 'phaser':
      /* Phaser is master-only in firmware, use master commands */
      sendCmd('setPhaserActive', { value: true });
      sendCmd('setPhaserRate', { value: p.rate });
      sendCmd('setPhaserDepth', { value: p.depth });
      sendCmd('setPhaserFeedback', { value: p.feedback });
      break;
  }
}

function clearFxFromTrack(track, fxNode) {
  const ft = fxNode.fxType;
  switch(ft) {
    case 'lowpass':
    case 'highpass':
    case 'bandpass':
    case 'notch':
    case 'allpass':
    case 'peaking':
    case 'lowshelf':
    case 'highshelf':
    case 'resonant':
      sendCmd('clearTrackFilter', { track });
      break;
    case 'bitcrusher':
    case 'distortion':
      sendCmd('clearTrackFX', { track });
      break;
    case 'echo':
    case 'delay':
      sendCmd('setTrackEcho', { track, active: false });
      break;
    case 'flanger':
      sendCmd('setTrackFlanger', { track, active: false });
      break;
    case 'compressor':
      sendCmd('setTrackCompressor', { track, active: false });
      break;
    case 'phaser':
      sendCmd('setPhaserActive', { value: false });
      break;
  }
}

/* ──────────── PARAMETER EDITOR ──────────── */
function showParamEditor(nodeId, anchorX, anchorY) {
  const node = nodes.find(n => n.id === nodeId);
  if (!node || node.type !== 'fx') return;
  editingNode = nodeId;
  let _realtimeFxTimer = null; // shared debounce timer for all sliders in this editor

  const def = FX_DEFS[node.fxType];
  const editor = document.getElementById('pbParamEditor');
  const title = document.getElementById('pbParamTitle');
  const body  = document.getElementById('pbParamBody');

  title.textContent = def.label;
  title.style.color = getColorHex(def.color);
  body.innerHTML = '';

  def.params.forEach(p => {
    const row = document.createElement('div');
    row.className = 'pb-param-row';
    const val = node.params[p.id];

    if (p.type === 'select') {
      row.innerHTML = `
        <div class="pb-param-label"><span>${p.label}</span></div>
        <select class="pb-param-select" data-param="${p.id}">
          ${p.options.map((o,i) => `<option value="${i}" ${i===val?'selected':''}>${o}</option>`).join('')}
        </select>
      `;
      row.querySelector('select').addEventListener('change', (e) => {
        node.params[p.id] = parseInt(e.target.value);
        updateNodeDisplay(node);
        resendFxNode(node);
        saveState();
      });
    } else {
      const min = p.min || 0;
      const max = p.max || 100;
      const step = p.step || (max - min > 100 ? 1 : 0.1);
      const unit = p.unit || '';
      const dispVal = p.log ? Math.round(val) : (Number.isInteger(step) ? Math.round(val) : (Math.round(val*100)/100));
      row.innerHTML = `
        <div class="pb-param-label"><span>${p.label}</span><span class="pb-param-val" id="pv-${p.id}">${dispVal}${unit}</span></div>
        <input type="range" class="pb-param-range" data-param="${p.id}" min="${min}" max="${max}" step="${step}" value="${val}">
      `;
      const range = row.querySelector('input');
      range.addEventListener('input', (e) => {
        const v = parseFloat(e.target.value);
        node.params[p.id] = v;
        const dv = p.log ? Math.round(v) : (Number.isInteger(step) ? Math.round(v) : (Math.round(v*100)/100));
        document.getElementById('pv-' + p.id).textContent = dv + unit;
        updateNodeDisplay(node);
        /* Realtime audio update — throttled 35 ms to avoid WS flood */
        if (_realtimeFxTimer) clearTimeout(_realtimeFxTimer);
        _realtimeFxTimer = setTimeout(() => { _realtimeFxTimer = null; resendFxNode(node); }, 35);
      });
      range.addEventListener('change', () => {
        if (_realtimeFxTimer) { clearTimeout(_realtimeFxTimer); _realtimeFxTimer = null; }
        resendFxNode(node);
        saveState();
      });
    }
    body.appendChild(row);
  });

  /* Position editor near the node */
  editor.classList.remove('hidden');
  const rect = canvasEl.getBoundingClientRect();
  let ex = anchorX - rect.left + 10;
  let ey = anchorY - rect.top - 20;
  /* Keep on screen */
  const ew = 280, eh = 200;
  if (ex + ew > window.innerWidth) ex = anchorX - rect.left - ew - 10;
  if (ey + eh > window.innerHeight) ey = window.innerHeight - eh - 20;
  if (ey < 0) ey = 10;
  editor.style.left = (anchorX - 30) + 'px';
  editor.style.top  = (anchorY + 20) + 'px';
}

function closeParamEditor() {
  document.getElementById('pbParamEditor').classList.add('hidden');
  editingNode = null;
}

function resendFxNode(fxNode) {
  const tracks = getTracksForNode(fxNode.id);
  tracks.forEach(t => applyFxToTrack(t, fxNode));
}

function getColorHex(name) {
  const m = {cyan:'#00e5ff',orange:'#ff9100',yellow:'#ffd600',purple:'#b388ff',pink:'#ff4081',green:'#69f0ae',teal:'#26c6da',violet:'#ea80fc',blue:'#448aff',gold:'#ffd700',silver:'#aab0c0',slate:'#78909c',amber:'#ffab40',lime:'#c6ff00',lavender:'#ce93d8',hotpink:'#f06292'};
  return m[name] || '#fff';
}

/* ──────────── POINTER EVENTS ──────────── */
function getCanvasPos(e) {
  const r = canvasEl.getBoundingClientRect();
  return {
    x: (e.clientX - r.left + canvasEl.scrollLeft) / viewZoom,
    y: (e.clientY - r.top  + canvasEl.scrollTop) / viewZoom
  };
}

function onPointerDown(e) {
  const t = e.target;
  closeParamEditor();

  /* Deselect cable */
  if (selectedCable && !t.closest('[data-cable-id]')) {
    selectedCable = null;
    renderCables();
  }

  if (activeTool === 'hand') {
    if (t.closest('#pbViewControls')) return;
    e.preventDefault();
    drag = {
      type: 'pan',
      startClientX: e.clientX,
      startClientY: e.clientY,
      startScrollLeft: canvasEl.scrollLeft,
      startScrollTop: canvasEl.scrollTop
    };
    canvasEl.setPointerCapture(e.pointerId);
    return;
  }

  if (activeTool === 'select') {
    if (t.closest('#pbViewControls')) return;
    if (t.classList.contains('pb-connector') || t.classList.contains('pb-node-delete') || t.classList.contains('pb-node-edit')) return;

    const nodeEl = t.closest('.pb-node');
    const isNodeTarget = nodeEl && !t.classList.contains('pb-connector') && !t.classList.contains('pb-node-delete') && !t.classList.contains('pb-node-edit');
    if (isNodeTarget) {
      e.preventDefault();
      const nodeId = nodeEl.dataset.nodeId;
      if (e.shiftKey) {
        if (selectedNodeIds.has(nodeId)) selectedNodeIds.delete(nodeId);
        else selectedNodeIds.add(nodeId);
      } else if (!selectedNodeIds.has(nodeId)) {
        selectedNodeIds.clear();
        selectedNodeIds.add(nodeId);
      }
      renderNodeSelection();

      const pos = getCanvasPos(e);
      const selectedNodes = Array.from(selectedNodeIds)
        .map(id => nodes.find(n => n.id === id))
        .filter(Boolean);
      selectedNodes.forEach(node => {
        const el = document.getElementById('node-' + node.id);
        if (el) el.classList.add('dragging');
      });
      drag = {
        type: 'multi-node',
        startX: pos.x,
        startY: pos.y,
        moved: false,
        origins: selectedNodes.map(node => ({ id: node.id, x: node.x, y: node.y }))
      };
      canvasEl.setPointerCapture(e.pointerId);
      return;
    }

    e.preventDefault();
    const pos = getCanvasPos(e);
    drag = {
      type: 'select-box',
      startX: pos.x,
      startY: pos.y,
      currentX: pos.x,
      currentY: pos.y,
      additive: !!e.shiftKey
    };
    if (!drag.additive) clearNodeSelection();
    showSelectionBoxWorldRect(pos.x, pos.y, pos.x, pos.y);
    canvasEl.setPointerCapture(e.pointerId);
    return;
  }

  /* Connector drag → cable creation */
  if (t.classList.contains('pb-connector')) {
    e.preventDefault();
    e.stopPropagation();
    const nodeId = t.dataset.node;
    const dir = t.dataset.dir;
    const pos = getCanvasPos(e);
    drag = { type: 'cable', fromId: nodeId, fromDir: dir, startX: pos.x, startY: pos.y };
    canvasEl.setPointerCapture(e.pointerId);
    return;
  }

  /* Node drag */
  const nodeEl = t.closest('.pb-node');
  if (nodeEl && !t.classList.contains('pb-node-delete')) {
    e.preventDefault();
    const nodeId = nodeEl.dataset.nodeId;
    const pos = getCanvasPos(e);
    const nx = parseFloat(nodeEl.style.left);
    const ny = parseFloat(nodeEl.style.top);
    drag = { type: 'node', nodeId, offsetX: pos.x - nx, offsetY: pos.y - ny, moved: false };
    nodeEl.classList.add('dragging');
    canvasEl.setPointerCapture(e.pointerId);
  }
}

function onPointerMove(e) {
  if (!drag) return;
  e.preventDefault();

  if (drag.type === 'pan') {
    const dx = e.clientX - drag.startClientX;
    const dy = e.clientY - drag.startClientY;
    canvasEl.scrollLeft = drag.startScrollLeft - dx;
    canvasEl.scrollTop = drag.startScrollTop - dy;
    return;
  }

  if (drag.type === 'select-box') {
    const pos = getCanvasPos(e);
    drag.currentX = pos.x;
    drag.currentY = pos.y;
    showSelectionBoxWorldRect(drag.startX, drag.startY, pos.x, pos.y);
    selectNodesInWorldRect(drag.startX, drag.startY, pos.x, pos.y, drag.additive);
    return;
  }

  if (drag.type === 'multi-node') {
    const pos = getCanvasPos(e);
    const dx = pos.x - drag.startX;
    const dy = pos.y - drag.startY;
    drag.moved = true;

    drag.origins.forEach(origin => {
      const node = nodes.find(n => n.id === origin.id);
      if (!node) return;
      node.x = snap(Math.max(0, Math.min(CANVAS_W - 170, origin.x + dx)));
      node.y = snap(Math.max(0, Math.min(CANVAS_H - 80, origin.y + dy)));
      const el = document.getElementById('node-' + node.id);
      if (el) {
        el.style.left = node.x + 'px';
        el.style.top = node.y + 'px';
      }
    });
    renderCables();
    return;
  }

  const pos = getCanvasPos(e);

  if (drag.type === 'node') {
    drag.moved = true;
    const node = nodes.find(n => n.id === drag.nodeId);
    if (!node) return;
    node.x = snap(Math.max(0, Math.min(CANVAS_W - 170, pos.x - drag.offsetX)));
    node.y = snap(Math.max(0, Math.min(CANVAS_H - 80,  pos.y - drag.offsetY)));
    const el = document.getElementById('node-' + node.id);
    if (el) {
      el.style.left = node.x + 'px';
      el.style.top  = node.y + 'px';
    }
    renderCables();
  }

  else if (drag.type === 'cable') {
    const fromNode = nodes.find(n => n.id === drag.fromId);
    let color = '#888';
    if (fromNode) {
      if (fromNode.type === 'pad') color = fromNode.color;
      else if (fromNode.type === 'fx') color = getColorHex(FX_DEFS[fromNode.fxType]?.color);
      else if (fromNode.type === 'bus') color = getColorHex(fromNode.color || 'teal');
      else if (fromNode.type === 'master') color = '#ffd700';
    }

    if (drag.fromDir === 'out') {
      const fp = getConnectorPos(drag.fromId, 'out');
      if (fp) renderPreviewCable(fp.x, fp.y, pos.x, pos.y, color);
    } else {
      const fp = getConnectorPos(drag.fromId, 'in');
      if (fp) renderPreviewCable(pos.x, pos.y, fp.x, fp.y, color);
    }

    /* Highlight valid targets */
    highlightTargets(drag.fromId, drag.fromDir, pos);
  }
}

function onPointerUp(e) {
  if (!drag) return;

  if (drag.type === 'node') {
    const el = document.getElementById('node-' + drag.nodeId);
    if (el) el.classList.remove('dragging');

    /* If not moved, treat as double-click → param editor */
    if (!drag.moved) {
      const node = nodes.find(n => n.id === drag.nodeId);
      if (node && node.type === 'fx') {
        showParamEditor(node.id, e.clientX, e.clientY);
      }
    } else {
      saveState();
    }
  }

  else if (drag.type === 'multi-node') {
    drag.origins.forEach(origin => {
      const el = document.getElementById('node-' + origin.id);
      if (el) el.classList.remove('dragging');
    });
    if (drag.moved) saveState();
  }

  else if (drag.type === 'select-box') {
    hideSelectionBox();
  }

  else if (drag.type === 'pan') {
    /* no-op */
  }

  else if (drag.type === 'cable') {
    clearPreviewCable();
    clearHighlights();

    /* Find target connector under pointer */
    const pos = getCanvasPos(e);
    const target = findConnectorAt(pos.x, pos.y, drag.fromId, drag.fromDir);
    if (target) {
      if (drag.fromDir === 'out') {
        addCable(drag.fromId, target);
      } else {
        addCable(target, drag.fromId);
      }
    }
  }

  drag = null;
  try { canvasEl.releasePointerCapture(e.pointerId); } catch(ex){}
}

function findConnectorAt(x, y, excludeId, fromDir) {
  const targetDir = fromDir === 'out' ? 'in' : 'out';
  const connectors = document.querySelectorAll(`.pb-connector.${targetDir}`);
  let best = null, bestDist = 40; // snap radius

  connectors.forEach(c => {
    const nodeId = c.dataset.node;
    if (nodeId === excludeId) return;
    const cp = getConnectorPos(nodeId, targetDir);
    if (!cp) return;
    const d = Math.hypot(cp.x - x, cp.y - y);
    if (d < bestDist) { bestDist = d; best = nodeId; }
  });
  return best;
}

function highlightTargets(fromId, fromDir, pos) {
  const targetDir = fromDir === 'out' ? 'in' : 'out';
  document.querySelectorAll('.pb-connector').forEach(c => c.classList.remove('active'));
  document.querySelectorAll(`.pb-connector.${targetDir}`).forEach(c => {
    if (c.dataset.node !== fromId) {
      const cp = getConnectorPos(c.dataset.node, targetDir);
      if (cp && Math.hypot(cp.x - pos.x, cp.y - pos.y) < 60) {
        c.classList.add('active');
      }
    }
  });
}

function clearHighlights() {
  document.querySelectorAll('.pb-connector.active').forEach(c => c.classList.remove('active'));
}

/* ──────────── KEYBOARD ──────────── */
function onKeyDown(e) {
  const tag = (e.target?.tagName || '').toUpperCase();
  const isFormField = tag === 'INPUT' || tag === 'TEXTAREA' || tag === 'SELECT';
  if (isFormField) return;

  if (e.key === 'Delete' || e.key === 'Backspace') {
    if (selectedCable) {
      e.preventDefault();
      removeCable(selectedCable);
      selectedCable = null;
      renderCables();
    }
  }
  if (e.key === 'Escape') {
    closeParamEditor();
    selectedCable = null;
    clearNodeSelection();
    hideSelectionBox();
    renderCables();
  }
}

function onDocClick(e) {
  if (!e.target.closest('#pbMenu') && !e.target.closest('#pbMenuBtn')) {
    setPBMenuOpen(false);
  }
  if (!e.target.closest('#pbPresetPanel') && !e.target.closest('#pbMenu')) {
    const panel = document.getElementById('pbPresetPanel');
    if (panel && !panel.classList.contains('hidden') && !e.target.closest('[onclick="pbOpenPresetPanel()"]')) {
      panel.classList.add('hidden');
    }
  }
}

/* ──────────── PUBLIC API (window) ──────────── */
window.goBack = function() {
  window.location.href = '/';
};

window.togglePBMenu = function() {
  const menu = document.getElementById('pbMenu');
  if (!menu) return;
  const willOpen = menu.classList.contains('hidden');
  if (willOpen) {
    const preset = document.getElementById('pbPresetPanel');
    if (preset) preset.classList.add('hidden');
  }
  setPBMenuOpen(willOpen);
};

window.pbAddEffect = function(fxType) {
  setPBMenuOpen(false);
  /* Place new node at center of visible viewport */
  const r = canvasEl.getBoundingClientRect();
  const cx = canvasEl.scrollLeft + r.width / 2 - 85 + Math.random() * 80 - 40;
  const cy = canvasEl.scrollTop + r.height / 2 - 40 + Math.random() * 80 - 40;
  const node = createFxNode(fxType, cx, cy);
  if (node) {
    /* Scroll to make node visible */
    const el = document.getElementById('node-' + node.id);
    if (el) el.scrollIntoView({ behavior: 'smooth', block: 'center', inline: 'center' });
  }
};

window.pbAddSelectedSource = function() {
  const picker = document.getElementById('pbSourcePicker');
  if (!picker || picker.disabled) return;
  const track = parseInt(picker.value, 10);
  if (!Number.isInteger(track)) return;
  addPadSource(track);
};

window.pbRemoveSelectedSource = function() {
  const picker = document.getElementById('pbActiveSourcePicker');
  if (!picker || picker.disabled) return;
  const track = parseInt(picker.value, 10);
  if (!Number.isInteger(track)) return;
  removePadSource(track);
};

window.pbAddAllSources = function() {
  const missing = getMissingPadTracks();
  if (!missing.length) return;
  missing.forEach(track => {
    if (!nodes.find(n => n.type === 'pad' && n.track === track)) {
      createPadNode(track, 60, getNextPadSlotY());
    }
  });
  updateStatus();
  saveState();
  refreshSourcePicker();
};

window.pbZoomIn = function() {
  setZoom(viewZoom + 0.12);
};

window.pbToggleHandTool = function() {
  setActiveTool('hand');
};

window.pbToggleSelectTool = function() {
  setActiveTool('select');
};

window.pbZoomOut = function() {
  setZoom(viewZoom - 0.12);
};

window.pbZoomReset = function() {
  setZoom(1);
};

window.pbToggleGrid = function() {
  gridVisible = !gridVisible;
  applyGridVisibility();
  saveViewPrefs();
};

window.pbToggleHeatMode = function() {
  if (heatMode === 'off') heatMode = 'on';
  else if (heatMode === 'on') heatMode = 'auto';
  else heatMode = 'off';
  updateHeatModeButton();
  renderCables();
  saveViewPrefs();
};

window.pbToggleSceneQuantize = function() {
  sceneQuantizeEnabled = !sceneQuantizeEnabled;
  if (!sceneQuantizeEnabled && pendingSceneState) {
    applyQueuedSceneState();
  }
  updateQuantizeButton();
  saveViewPrefs();
};

window.pbSaveSnapshot = function(slot) {
  const key = String(slot || 'A').toUpperCase() === 'B' ? 'B' : 'A';
  localStorage.setItem(`pb_snapshot_${key}`, JSON.stringify(exportState()));
};

window.pbLoadSnapshot = function(slot) {
  const key = String(slot || 'A').toUpperCase() === 'B' ? 'B' : 'A';
  const raw = localStorage.getItem(`pb_snapshot_${key}`);
  if (!raw) return;
  try {
    const state = JSON.parse(raw);
    scheduleSceneStateApply(state, `Snapshot ${key}`);
  } catch(ex) {}
};

window.pbDeleteNode = function(nodeId) {
  const node = nodes.find(n => n.id === nodeId);
  if (!node || node.type === 'pad' || node.type === 'master' || node.type === 'bus') return;

  /* Remove all cables connected to this node */
  const connectedCables = cables.filter(c => c.from === nodeId || c.to === nodeId);
  connectedCables.forEach(c => {
    clearConnection(c);
  });
  cables = cables.filter(c => c.from !== nodeId && c.to !== nodeId);

  /* Remove node */
  nodes = nodes.filter(n => n.id !== nodeId);
  selectedNodeIds.delete(nodeId);
  const el = document.getElementById('node-' + nodeId);
  if (el) el.remove();

  renderNodeSelection();
  renderCables();
  updateStatus();
  saveState();
};

window.pbClearAll = function() {
  setPBMenuOpen(false);
  /* Clear all cables */
  cables.forEach(c => clearConnection(c));
  cables = [];
  /* Remove PAD + FX nodes, keep master/buses */
  nodes.filter(n => n.type === 'fx' || n.type === 'pad').forEach(n => {
    const el = document.getElementById('node-' + n.id);
    if (el) el.remove();
  });
  nodes = nodes.filter(n => n.type !== 'fx' && n.type !== 'pad');
  clearNodeSelection();
  renderCables();
  updateStatus();
  refreshSourcePicker();
  saveState();
  /* Reset firmware FX state to match the now-empty canvas */
  resetFirmwareFX();
};

window.pbResetLayout = function() {
  setPBMenuOpen(false);
  /* Reset pad positions */
  nodes.filter(n => n.type === 'pad').forEach((n, i) => {
    n.x = 60; n.y = 60 + i * 120;
    const el = document.getElementById('node-' + n.id);
    if (el) { el.style.left = n.x + 'px'; el.style.top = n.y + 'px'; }
  });
  /* Reset master position */
  const master = nodes.find(n => n.type === 'master');
  if (master) {
    master.x = CANVAS_W - 260; master.y = 500;
    const el = document.getElementById('node-' + master.id);
    if (el) { el.style.left = master.x + 'px'; el.style.top = master.y + 'px'; }
  }
  const busA = nodes.find(n => n.id === 'bus-a');
  if (busA) {
    busA.x = CANVAS_W - 560; busA.y = 380;
    const el = document.getElementById('node-' + busA.id);
    if (el) { el.style.left = busA.x + 'px'; el.style.top = busA.y + 'px'; }
  }
  const busB = nodes.find(n => n.id === 'bus-b');
  if (busB) {
    busB.x = CANVAS_W - 560; busB.y = 640;
    const el = document.getElementById('node-' + busB.id);
    if (el) { el.style.left = busB.x + 'px'; el.style.top = busB.y + 'px'; }
  }
  renderCables();
  saveState();
};

window.pbAutoRoute = function() {
  setPBMenuOpen(false);
  /* Auto: each pad → master out */
  nodes.filter(n => n.type === 'pad').forEach(pad => {
    if (!cables.find(c => c.from === pad.id && c.to === 'master')) {
      addCable(pad.id, 'master');
    }
  });
};

window.pbBuildChain = function() {
  setPBMenuOpen(false);
  const fxNodes = nodes.filter(n => n.type === 'fx').sort((a, b) => (a.x - b.x) || (a.y - b.y));
  if (fxNodes.length === 0) return;

  cables.forEach(c => clearConnection(c));
  cables = [];

  const firstFx = fxNodes[0];
  nodes.filter(n => n.type === 'pad').forEach(pad => addCable(pad.id, firstFx.id));
  for (let i = 0; i < fxNodes.length - 1; i++) {
    addCable(fxNodes[i].id, fxNodes[i + 1].id);
  }
  addCable(fxNodes[fxNodes.length - 1].id, 'master');

  renderCables();
  updateStatus();
  saveState();
};

window.pbOrganizeNodes = function() {
  setPBMenuOpen(false);
  const fxNodes = nodes.filter(n => n.type === 'fx').sort((a, b) => (a.x - b.x) || (a.y - b.y));
  fxNodes.forEach((node, idx) => {
    const col = idx % 4;
    const row = Math.floor(idx / 4);
    node.x = snap(760 + col * 280);
    node.y = snap(280 + row * 220);
    const el = document.getElementById('node-' + node.id);
    if (el) {
      el.style.left = node.x + 'px';
      el.style.top = node.y + 'px';
    }
  });

  const master = nodes.find(n => n.type === 'master');
  if (master) {
    master.x = snap(CANVAS_W - 260);
    master.y = snap(520);
    const el = document.getElementById('node-' + master.id);
    if (el) {
      el.style.left = master.x + 'px';
      el.style.top = master.y + 'px';
    }
  }

  renderCables();
  saveState();
};

window.pbToggleSignalDemo = function() {
  setPBMenuOpen(false);
  signalDemoMode = !signalDemoMode;
  updatePlayingVisualState();
};

window.pbSavePreset = function() {
  window.pbOpenPresetPanel();
  const input = document.getElementById('pbPresetNameInput');
  if (input) {
    input.focus();
    input.select();
  }
};

window.pbFactoryPresets = function() {
  window.pbOpenPresetPanel();
};

window.pbLoadPreset = function() {
  window.pbOpenPresetPanel();
};

window.pbOpenPresetPanel = function() {
  setPBMenuOpen(false);
  const panel = document.getElementById('pbPresetPanel');
  if (!panel) return;
  renderPresetPanel();
  panel.classList.remove('hidden');
};

window.pbClosePresetPanel = function() {
  const panel = document.getElementById('pbPresetPanel');
  if (panel) panel.classList.add('hidden');
};

window.pbSavePresetFromPanel = function() {
  const input = document.getElementById('pbPresetNameInput');
  const baseName = input?.value?.trim();
  const name = baseName || ('Patchbay ' + new Date().toLocaleTimeString());
  const presets = getUserPresets();
  presets[name] = exportState();
  setUserPresets(presets);
  if (input) input.value = '';
  renderPresetPanel();
};

window.pbEditNode = function(nodeId) {
  const node = nodes.find(n => n.id === nodeId);
  if (!node || node.type !== 'fx') return;
  const el = document.getElementById('node-' + nodeId);
  if (!el) return;
  const rect = el.getBoundingClientRect();
  showParamEditor(nodeId, rect.right - 10, rect.top + 8);
};

window.closeParamEditor = closeParamEditor;

/* ──────────── PERSISTENCE ──────────── */
function exportState() {
  return {
    nodes: nodes.filter(n => n.type === 'fx').map(n => ({
      id: n.id, fxType: n.fxType, x: n.x, y: n.y, params: {...n.params}
    })),
    padPositions: nodes.filter(n => n.type === 'pad').map(n => ({ id: n.id, x: n.x, y: n.y })),
    busPositions: nodes.filter(n => n.type === 'bus').map(n => ({ id: n.id, x: n.x, y: n.y })),
    masterPos: (() => { const m = nodes.find(n => n.type === 'master'); return m ? {x: m.x, y: m.y} : null; })(),
    cables: cables.map(c => ({ from: c.from, to: c.to })),
    nextId
  };
}

function importState(data) {
  clearNodeSelection();

  /* Clear everything */
  cables.forEach(c => clearConnection(c));
  cables = [];
  nodes.filter(n => n.type === 'fx').forEach(n => {
    const el = document.getElementById('node-' + n.id);
    if (el) el.remove();
  });
  nodes = nodes.filter(n => n.type !== 'fx');

  ensurePadNodesForState(data);

  /* Restore pad positions */
  if (data.padPositions) {
    data.padPositions.forEach(pp => {
      const node = nodes.find(n => n.id === pp.id);
      if (node) {
        node.x = pp.x; node.y = pp.y;
        const el = document.getElementById('node-' + node.id);
        if (el) { el.style.left = pp.x + 'px'; el.style.top = pp.y + 'px'; }
      }
    });
  }

  if (data.busPositions) {
    data.busPositions.forEach(bp => {
      const node = nodes.find(n => n.id === bp.id && n.type === 'bus');
      if (node) {
        node.x = bp.x;
        node.y = bp.y;
        const el = document.getElementById('node-' + node.id);
        if (el) { el.style.left = bp.x + 'px'; el.style.top = bp.y + 'px'; }
      }
    });
  }

  /* Restore master position */
  if (data.masterPos) {
    const m = nodes.find(n => n.type === 'master');
    if (m) {
      m.x = data.masterPos.x; m.y = data.masterPos.y;
      const el = document.getElementById('node-' + m.id);
      if (el) { el.style.left = m.x + 'px'; el.style.top = m.y + 'px'; }
    }
  }

  /* Recreate FX nodes */
  if (data.nodes) {
    data.nodes.forEach(nd => {
      const node = createFxNode(nd.fxType, nd.x, nd.y);
      if (node && nd.params) {
        Object.assign(node.params, nd.params);
        updateNodeDisplay(node);
      }
      /* Remap ID */
      if (node) {
        const oldId = node.id;
        node.id = nd.id;
        const el = document.getElementById('node-' + oldId);
        if (el) { el.id = 'node-' + nd.id; el.dataset.nodeId = nd.id; }
        /* Update connector data-node attrs */
        el?.querySelectorAll('.pb-connector').forEach(c => c.dataset.node = nd.id);
        el?.querySelector('.pb-node-delete')?.setAttribute('onclick', `pbDeleteNode('${nd.id}')`);
        el?.querySelector('.pb-node-delete')?.setAttribute('data-node', nd.id);
        el?.querySelector('.pb-node-edit')?.setAttribute('onclick', `pbEditNode('${nd.id}')`);
        el?.querySelector('.pb-node-edit')?.setAttribute('data-node', nd.id);
      }
    });
  }

  nextId = data.nextId || nextId;

  /* Recreate cables */
  if (data.cables) {
    data.cables.forEach(cd => addCable(cd.from, cd.to));
  }

  renderCables();
  updateStatus();
  refreshSourcePicker();
}

function saveState() {
  try {
    localStorage.setItem('pb_state', JSON.stringify(exportState()));
  } catch(ex) {}
}

function loadState() {
  try {
    const raw = localStorage.getItem('pb_state');
    if (raw) {
      const data = JSON.parse(raw);
      importState(data);
    }
  } catch(ex) {
    console.warn('Patchbay: could not load state', ex);
  }
}

function setZoom(nextZoom) {
  const clamped = Math.max(0.5, Math.min(2.5, nextZoom));
  if (Math.abs(clamped - viewZoom) < 0.001) return;

  const oldZoom = viewZoom;
  const rect = canvasEl.getBoundingClientRect();
  const centerX = canvasEl.scrollLeft + rect.width / 2;
  const centerY = canvasEl.scrollTop + rect.height / 2;
  const worldX = centerX / oldZoom;
  const worldY = centerY / oldZoom;

  viewZoom = clamped;
  applyZoom();

  canvasEl.scrollLeft = worldX * viewZoom - rect.width / 2;
  canvasEl.scrollTop  = worldY * viewZoom - rect.height / 2;

  saveViewPrefs();
}

function applyZoom() {
  if (!worldEl || !svgEl || !nodesEl || !canvasEl) return;
  worldEl.style.width = (CANVAS_W * viewZoom) + 'px';
  worldEl.style.height = (CANVAS_H * viewZoom) + 'px';
  svgEl.style.transform = `scale(${viewZoom})`;
  nodesEl.style.transform = `scale(${viewZoom})`;
  canvasEl.style.setProperty('--pb-grid-zoom', String(viewZoom));

  const zoomLabel = document.getElementById('pbZoomValue');
  if (zoomLabel) zoomLabel.textContent = `${Math.round(viewZoom * 100)}%`;

  renderCables();
}

function applyGridVisibility() {
  if (!canvasEl) return;
  canvasEl.classList.toggle('pb-grid-off', !gridVisible);
  const btn = document.getElementById('pbGridToggleBtn');
  if (btn) {
    btn.classList.toggle('is-off', !gridVisible);
    btn.textContent = gridVisible ? 'GRID' : 'NO GRID';
  }
}

function updateHeatModeButton() {
  const btn = document.getElementById('pbHeatToggleBtn');
  if (!btn) return;
  const labels = {
    off: '🌡 Heat OFF',
    on: '🌡 Heat ON',
    auto: '🌡 Heat AUTO'
  };
  btn.textContent = labels[heatMode] || '🌡 Heat ON';
}

function updateQuantizeButton() {
  const btn = document.getElementById('pbQuantizeBtn');
  if (!btn) return;
  if (!sceneQuantizeEnabled) {
    btn.textContent = '⏱ Quantize OFF';
    return;
  }
  btn.textContent = pendingSceneState
    ? `⏱ Quantize ON • ${pendingSceneLabel || 'Queued'}`
    : '⏱ Quantize ON';
}

function cloneSceneState(state) {
  try {
    return JSON.parse(JSON.stringify(state));
  } catch(ex) {
    return null;
  }
}

function applySceneStateNow(state) {
  const cloned = cloneSceneState(state);
  if (!cloned) return;
  importState(cloned);
  saveState();
}

function applyQueuedSceneState() {
  if (!pendingSceneState) return;
  const queued = pendingSceneState;
  pendingSceneState = null;
  pendingSceneLabel = '';
  applySceneStateNow(queued);
  updateQuantizeButton();
}

function scheduleSceneStateApply(state, label = '') {
  const cloned = cloneSceneState(state);
  if (!cloned) return;

  const running = !!(serverPlaying || stepDrivenPlaying || isPlaying);
  if (!sceneQuantizeEnabled || !running) {
    pendingSceneState = null;
    pendingSceneLabel = '';
    applySceneStateNow(cloned);
    updateQuantizeButton();
    return;
  }

  pendingSceneState = cloned;
  pendingSceneLabel = label || 'Queued';
  updateQuantizeButton();
}

function isHeatActive() {
  if (heatMode === 'on') return true;
  if (heatMode === 'off') return false;
  const isRunning = !!(signalDemoMode || serverPlaying || stepDrivenPlaying || isPlaying);
  return isRunning;
}

function loadViewPrefs() {
  try {
    const raw = localStorage.getItem('pb_view');
    if (!raw) return;
    const cfg = JSON.parse(raw);
    if (typeof cfg.zoom === 'number') viewZoom = Math.max(0.5, Math.min(2.5, cfg.zoom));
    if (typeof cfg.gridVisible === 'boolean') gridVisible = cfg.gridVisible;
    if (typeof cfg.heatMode === 'string' && ['off', 'on', 'auto'].includes(cfg.heatMode)) {
      heatMode = cfg.heatMode;
    } else if (typeof cfg.heatModeEnabled === 'boolean') {
      heatMode = cfg.heatModeEnabled ? 'on' : 'off';
    }
    if (typeof cfg.sceneQuantizeEnabled === 'boolean') sceneQuantizeEnabled = cfg.sceneQuantizeEnabled;
  } catch(ex) {}
}

function saveViewPrefs() {
  try {
    localStorage.setItem('pb_view', JSON.stringify({ zoom: viewZoom, gridVisible, heatMode, sceneQuantizeEnabled }));
  } catch(ex) {}
}

function createFactoryPresetState(preset) {
  if (!preset || !Array.isArray(preset.chain) || preset.chain.length === 0) return null;

  const current = exportState();
  let idCounter = nextId;
  const keyToId = {};

  const fxNodes = preset.chain.map(def => {
    const id = `fx-${idCounter++}`;
    keyToId[def.key] = id;
    return {
      id,
      fxType: def.fxType,
      x: def.x,
      y: def.y,
      params: {...(def.params || {})}
    };
  });

  const chainCables = [];
  const firstFxId = keyToId[preset.chain[0].key];
  const lastFxId = keyToId[preset.chain[preset.chain.length - 1].key];

  if (firstFxId) {
    current.padPositions.forEach(pad => {
      chainCables.push({ from: pad.id, to: firstFxId });
    });
  }

  for (let i = 0; i < preset.chain.length - 1; i++) {
    const fromId = keyToId[preset.chain[i].key];
    const toId = keyToId[preset.chain[i + 1].key];
    if (fromId && toId) chainCables.push({ from: fromId, to: toId });
  }

  if (lastFxId) chainCables.push({ from: lastFxId, to: 'master' });

  return {
    nodes: fxNodes,
    padPositions: current.padPositions,
    busPositions: current.busPositions,
    masterPos: current.masterPos,
    cables: chainCables,
    nextId: idCounter
  };
}

function applyFactoryPreset(preset) {
  const state = createFactoryPresetState(preset);
  if (!state) return;
  scheduleSceneStateApply(state, preset.name || 'Factory');
}

function getUserPresets() {
  try {
    const parsed = JSON.parse(localStorage.getItem('pb_presets') || '{}');
    if (parsed && typeof parsed === 'object') return parsed;
  } catch(ex) {}
  return {};
}

function getPresetMeta() {
  try {
    const parsed = JSON.parse(localStorage.getItem('pb_preset_meta') || '{}');
    if (parsed && typeof parsed === 'object') return parsed;
  } catch(ex) {}
  return {};
}

function setPresetMeta(meta) {
  try {
    localStorage.setItem('pb_preset_meta', JSON.stringify(meta || {}));
  } catch(ex) {}
}

function getPresetSearchTerm() {
  const input = document.getElementById('pbPresetSearchInput');
  return (input?.value || '').trim().toLowerCase();
}

function isFavOnlyEnabled() {
  const cb = document.getElementById('pbPresetFavOnly');
  return !!cb?.checked;
}

function matchesPresetFilter(name, description, tags, favorite) {
  if (isFavOnlyEnabled() && !favorite) return false;
  const term = getPresetSearchTerm();
  if (!term) return true;
  const hay = `${name || ''} ${description || ''} ${tags || ''}`.toLowerCase();
  return hay.includes(term);
}

window.pbRefreshPresetBrowser = function() {
  renderPresetPanel();
};

function setUserPresets(presets) {
  localStorage.setItem('pb_presets', JSON.stringify(presets || {}));
}

function renderPresetPanel() {
  const factoryList = document.getElementById('pbFactoryPresetList');
  const userList = document.getElementById('pbUserPresetList');
  if (!factoryList || !userList) return;
  const meta = getPresetMeta();

  factoryList.innerHTML = '';
  FACTORY_PRESETS.forEach((preset) => {
    const pmeta = meta[`factory:${preset.id}`] || {};
    const tags = pmeta.tags || '';
    const favorite = !!pmeta.favorite;
    if (!matchesPresetFilter(preset.name, preset.description, tags, favorite)) return;
    const card = document.createElement('article');
    card.className = 'pb-preset-card';
    const stars = '★'.repeat(Math.max(0, Math.min(5, parseInt(pmeta.rating || 0, 10)))) || '☆☆☆☆☆';
    card.innerHTML = `
      <div class="pb-preset-card-head">
        <span class="pb-preset-name">${preset.name}</span>
        <div class="pb-preset-actions">
          <button class="pb-load">Cargar</button>
        </div>
      </div>
      <div class="pb-preset-desc">${preset.description}</div>
      <div class="pb-preset-desc">${favorite ? '⭐ ' : ''}${stars} ${tags ? '· ' + tags : ''}</div>
      <div class="pb-preset-meta">
        <button class="pb-fav">${favorite ? '★' : '☆'}</button>
        <select class="pb-rating">
          <option value="0">0★</option><option value="1">1★</option><option value="2">2★</option>
          <option value="3">3★</option><option value="4">4★</option><option value="5">5★</option>
        </select>
        <input class="pb-tags" type="text" maxlength="42" placeholder="tags" value="${tags}">
      </div>
    `;
    const key = `factory:${preset.id}`;
    const ratingSel = card.querySelector('.pb-rating');
    if (ratingSel) ratingSel.value = String(parseInt(pmeta.rating || 0, 10));
    card.querySelector('.pb-load')?.addEventListener('click', () => {
      applyFactoryPreset(preset);
    });
    card.querySelector('.pb-fav')?.addEventListener('click', () => {
      const m = getPresetMeta();
      const cur = m[key] || {};
      cur.favorite = !cur.favorite;
      m[key] = cur;
      setPresetMeta(m);
      renderPresetPanel();
    });
    card.querySelector('.pb-rating')?.addEventListener('change', (ev) => {
      const m = getPresetMeta();
      const cur = m[key] || {};
      cur.rating = Math.max(0, Math.min(5, parseInt(ev.target.value || '0', 10) || 0));
      m[key] = cur;
      setPresetMeta(m);
      renderPresetPanel();
    });
    card.querySelector('.pb-tags')?.addEventListener('change', (ev) => {
      const m = getPresetMeta();
      const cur = m[key] || {};
      cur.tags = String(ev.target.value || '').trim();
      m[key] = cur;
      setPresetMeta(m);
      renderPresetPanel();
    });
    factoryList.appendChild(card);
  });

  userList.innerHTML = '';
  const userPresets = getUserPresets();
  const names = Object.keys(userPresets)
    .filter((name) => {
      const pmeta = meta[`user:${name}`] || {};
      return matchesPresetFilter(name, 'Preset usuario', pmeta.tags || '', !!pmeta.favorite);
    })
    .sort((a, b) => a.localeCompare(b));
  if (names.length === 0) {
    const empty = document.createElement('article');
    empty.className = 'pb-preset-card';
    empty.innerHTML = '<div class="pb-preset-desc">Sin presets guardados todavía.</div>';
    userList.appendChild(empty);
    return;
  }

  names.forEach((name) => {
    const pmeta = meta[`user:${name}`] || {};
    const tags = pmeta.tags || '';
    const favorite = !!pmeta.favorite;
    const stars = '★'.repeat(Math.max(0, Math.min(5, parseInt(pmeta.rating || 0, 10)))) || '☆☆☆☆☆';
    const card = document.createElement('article');
    card.className = 'pb-preset-card';
    card.innerHTML = `
      <div class="pb-preset-card-head">
        <span class="pb-preset-name">${name}</span>
        <div class="pb-preset-actions">
          <button class="pb-load">Cargar</button>
          <button class="pb-delete">Borrar</button>
        </div>
      </div>
      <div class="pb-preset-desc">Preset usuario ${favorite ? '⭐' : ''} ${stars}</div>
      <div class="pb-preset-desc">${tags ? 'tags: ' + tags : ''}</div>
      <div class="pb-preset-meta">
        <button class="pb-fav">${favorite ? '★' : '☆'}</button>
        <select class="pb-rating">
          <option value="0">0★</option><option value="1">1★</option><option value="2">2★</option>
          <option value="3">3★</option><option value="4">4★</option><option value="5">5★</option>
        </select>
        <input class="pb-tags" type="text" maxlength="42" placeholder="tags" value="${tags}">
      </div>
    `;
    const key = `user:${name}`;
    const ratingSel = card.querySelector('.pb-rating');
    if (ratingSel) ratingSel.value = String(parseInt(pmeta.rating || 0, 10));
    card.querySelector('.pb-load')?.addEventListener('click', () => {
      scheduleSceneStateApply(userPresets[name], name);
    });
    card.querySelector('.pb-fav')?.addEventListener('click', () => {
      const m = getPresetMeta();
      const cur = m[key] || {};
      cur.favorite = !cur.favorite;
      m[key] = cur;
      setPresetMeta(m);
      renderPresetPanel();
    });
    card.querySelector('.pb-rating')?.addEventListener('change', (ev) => {
      const m = getPresetMeta();
      const cur = m[key] || {};
      cur.rating = Math.max(0, Math.min(5, parseInt(ev.target.value || '0', 10) || 0));
      m[key] = cur;
      setPresetMeta(m);
      renderPresetPanel();
    });
    card.querySelector('.pb-tags')?.addEventListener('change', (ev) => {
      const m = getPresetMeta();
      const cur = m[key] || {};
      cur.tags = String(ev.target.value || '').trim();
      m[key] = cur;
      setPresetMeta(m);
      renderPresetPanel();
    });
    card.querySelector('.pb-delete')?.addEventListener('click', () => {
      const presets = getUserPresets();
      delete presets[name];
      setUserPresets(presets);
      const m = getPresetMeta();
      delete m[key];
      setPresetMeta(m);
      renderPresetPanel();
    });
    userList.appendChild(card);
  });
}

/* ──────────── UTILS ──────────── */
function snap(v) { return Math.round(v / SNAP) * SNAP; }

function updateStatus() {
  document.getElementById('pbNodeCount').textContent = nodes.length;
  document.getElementById('pbCableCount').textContent = cables.length;
}

/* ──────────── BOOT ──────────── */
document.addEventListener('DOMContentLoaded', init);

})();
