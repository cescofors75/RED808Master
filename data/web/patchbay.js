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

const FX_DEFS = {
  lowpass:    { label:'LOW PASS',    color:'cyan',   icon:'⏣', params:[ {id:'cutoff', label:'Cutoff', min:20, max:20000, def:1000, unit:'Hz', log:true}, {id:'resonance', label:'Res', min:0.1, max:20, def:0.707, step:0.01} ] },
  highpass:   { label:'HI PASS',     color:'cyan',   icon:'⏣', params:[ {id:'cutoff', label:'Cutoff', min:20, max:20000, def:1000, unit:'Hz', log:true}, {id:'resonance', label:'Res', min:0.1, max:20, def:0.707, step:0.01} ] },
  bandpass:   { label:'BAND PASS',   color:'blue',   icon:'⏣', params:[ {id:'cutoff', label:'Cutoff', min:20, max:20000, def:1000, unit:'Hz', log:true}, {id:'resonance', label:'Q', min:0.1, max:20, def:1, step:0.01} ] },
  echo:       { label:'REVERB',      color:'orange', icon:'◎', params:[ {id:'time', label:'Time', min:10, max:750, def:200, unit:'ms'}, {id:'feedback', label:'Feedback', min:0, max:95, def:40, unit:'%'}, {id:'mix', label:'Mix', min:0, max:100, def:50, unit:'%'} ] },
  delay:      { label:'DELAY',       color:'yellow', icon:'◉', params:[ {id:'time', label:'Time', min:10, max:750, def:300, unit:'ms'}, {id:'feedback', label:'Feedback', min:0, max:95, def:50, unit:'%'}, {id:'mix', label:'Mix', min:0, max:100, def:50, unit:'%'} ] },
  bitcrusher: { label:'BITCRUSHER',  color:'purple', icon:'▦', params:[ {id:'bits', label:'Bit Depth', min:1, max:16, def:8, step:1} ] },
  distortion: { label:'DISTORTION',  color:'pink',   icon:'⚡', params:[ {id:'amount', label:'Amount', min:0, max:100, def:50, unit:'%'}, {id:'mode', label:'Mode', type:'select', options:['SOFT','HARD','TUBE','FUZZ'], def:0} ] },
  compressor: { label:'COMPRESSOR',  color:'green',  icon:'▬', params:[ {id:'threshold', label:'Threshold', min:0, max:100, def:60, unit:'%'}, {id:'ratio', label:'Ratio', min:1, max:20, def:4, step:0.5} ] },
  flanger:    { label:'FLANGER',     color:'teal',   icon:'≋', params:[ {id:'rate', label:'Rate', min:0, max:100, def:30, unit:'%'}, {id:'depth', label:'Depth', min:0, max:100, def:50, unit:'%'}, {id:'feedback', label:'Feedback', min:0, max:90, def:40, unit:'%'} ] },
  phaser:     { label:'PHASER',      color:'violet', icon:'◐', params:[ {id:'rate', label:'Rate', min:0, max:100, def:30, unit:'%'}, {id:'depth', label:'Depth', min:0, max:100, def:50, unit:'%'}, {id:'feedback', label:'Feedback', min:0, max:90, def:40, unit:'%'} ] }
};

const FILTER_TYPE_MAP = { lowpass:1, highpass:2, bandpass:3 };

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

/* Drag state */
let drag = null;   // { type:'node'|'cable', nodeId, startX, startY, offsetX, offsetY, fromId, fromType }
let previewPath = null;
let selectedCable = null;
let editingNode = null;

/* DOM refs */
let svgEl, nodesEl, canvasEl;

/* ──────────── INIT ──────────── */
function init() {
  svgEl   = document.getElementById('pbSVG');
  nodesEl = document.getElementById('pbNodes');
  canvasEl= document.getElementById('pbCanvas');

  /* Set canvas size */
  nodesEl.style.width  = CANVAS_W + 'px';
  nodesEl.style.height = CANVAS_H + 'px';
  svgEl.setAttribute('width',  CANVAS_W);
  svgEl.setAttribute('height', CANVAS_H);
  svgEl.style.width  = CANVAS_W + 'px';
  svgEl.style.height = CANVAS_H + 'px';

  /* Create initial pad nodes */
  for (let i = 0; i < 16; i++) {
    createPadNode(i, 60, 60 + i * 120);
  }
  /* Create Master Out */
  createMasterNode(CANVAS_W - 260, 500);

  /* Load saved state */
  loadState();

  /* Events */
  canvasEl.addEventListener('pointerdown', onPointerDown);
  canvasEl.addEventListener('pointermove', onPointerMove);
  canvasEl.addEventListener('pointerup', onPointerUp);
  canvasEl.addEventListener('pointercancel', onPointerUp);
  document.addEventListener('keydown', onKeyDown);
  document.addEventListener('click', onDocClick);

  /* WebSocket */
  connectWS();

  updateStatus();
}

/* ──────────── WEBSOCKET ──────────── */
function connectWS() {
  const proto = location.protocol === 'https:' ? 'wss' : 'ws';
  ws = new WebSocket(proto + '://' + location.host + '/ws');
  ws.onopen = () => {
    wsConnected = true;
    document.getElementById('pbWsStatus').classList.add('connected');
    /* Resend all active effects */
    cables.forEach(c => applyConnection(c));
  };
  ws.onclose = () => {
    wsConnected = false;
    document.getElementById('pbWsStatus').classList.remove('connected');
    setTimeout(connectWS, 3000);
  };
  ws.onerror = () => ws.close();
  ws.onmessage = (e) => {
    try { handleWSMessage(JSON.parse(e.data)); } catch(ex) {}
  };
}

function sendCmd(cmd, data) {
  if (!ws || ws.readyState !== 1) return;
  ws.send(JSON.stringify(Object.assign({ cmd }, data)));
}

function handleWSMessage(msg) {
  if ((msg.type === 'playState' || msg.type === 'sequencerState' || msg.type === 'status') && typeof msg.playing !== 'undefined') {
    isPlaying = !!msg.playing;
    const root = document.getElementById('patchbay');
    if (root) root.classList.toggle('pb-playing', isPlaying);
    return;
  }

  /* Update sample names on pads if available */
  if (msg.type === 'sampleLoaded' || msg.type === 'kitLoaded') {
    // Could update pad labels here
  }
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
    path.setAttribute('d', getCableBezier(fromPos.x, fromPos.y, toPos.x, toPos.y));
    path.setAttribute('stroke', cable.color);
    path.setAttribute('stroke-width', '3');
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
      path.setAttribute('stroke-width', '5');
      path.setAttribute('stroke-dasharray', '8 4');
    }
    path.addEventListener('click', (e) => {
      e.stopPropagation();
      onCableClick(cable.id);
    });
    svgEl.appendChild(path);
  });
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
  if (selectedCable === id) {
    removeCable(id);
    selectedCable = null;
  } else {
    selectedCable = id;
    renderCables();
  }
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
  const fromNode = nodes.find(n => n.id === cable.from);

  if (toNode && toNode.type === 'fx' && fromNode && fromNode.type === 'pad') {
    clearFxFromTrack(fromNode.track, toNode);
  }

  /* If an FX node loses all input connections, clear its effect on all previously connected tracks */
  if (toNode && toNode.type === 'fx') {
    /* Check if any other cables still connect to this FX */
    const remainingInputs = cables.filter(c => c.to === toNode.id && c.id !== cable.id);
    if (remainingInputs.length === 0) {
      // All tracks that were connected need to be cleared
      // But since we removed the cable already, use fromNode info
    }
  }
}

function applyFxToTrack(track, fxNode) {
  const p = fxNode.params;
  const ft = fxNode.fxType;

  switch(ft) {
    case 'lowpass':
    case 'highpass':
    case 'bandpass':
      sendCmd('setTrackFilter', { track, filterType: FILTER_TYPE_MAP[ft], cutoff: p.cutoff, resonance: p.resonance || p['Q'] || 0.707 });
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
      });
      range.addEventListener('change', () => {
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
  const m = {cyan:'#00e5ff',orange:'#ff9100',yellow:'#ffd600',purple:'#b388ff',pink:'#ff4081',green:'#69f0ae',teal:'#26c6da',violet:'#ea80fc',blue:'#448aff',gold:'#ffd700'};
  return m[name] || '#fff';
}

/* ──────────── POINTER EVENTS ──────────── */
function getCanvasPos(e) {
  const r = canvasEl.getBoundingClientRect();
  return {
    x: e.clientX - r.left + canvasEl.scrollLeft,
    y: e.clientY - r.top  + canvasEl.scrollTop
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
  if (e.key === 'Delete' || e.key === 'Backspace') {
    if (selectedCable) {
      removeCable(selectedCable);
      selectedCable = null;
    }
  }
  if (e.key === 'Escape') {
    closeParamEditor();
    selectedCable = null;
    renderCables();
  }
}

function onDocClick(e) {
  if (!e.target.closest('#pbMenu') && !e.target.closest('#pbMenuBtn')) {
    document.getElementById('pbMenu').classList.add('hidden');
  }
}

/* ──────────── PUBLIC API (window) ──────────── */
window.goBack = function() {
  window.location.href = '/';
};

window.togglePBMenu = function() {
  document.getElementById('pbMenu').classList.toggle('hidden');
};

window.pbAddEffect = function(fxType) {
  document.getElementById('pbMenu').classList.add('hidden');
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

window.pbDeleteNode = function(nodeId) {
  const node = nodes.find(n => n.id === nodeId);
  if (!node || node.type === 'pad' || node.type === 'master') return;

  /* Remove all cables connected to this node */
  const connectedCables = cables.filter(c => c.from === nodeId || c.to === nodeId);
  connectedCables.forEach(c => {
    clearConnection(c);
  });
  cables = cables.filter(c => c.from !== nodeId && c.to !== nodeId);

  /* Remove node */
  nodes = nodes.filter(n => n.id !== nodeId);
  const el = document.getElementById('node-' + nodeId);
  if (el) el.remove();

  renderCables();
  updateStatus();
  saveState();
};

window.pbClearAll = function() {
  document.getElementById('pbMenu').classList.add('hidden');
  /* Clear all cables */
  cables.forEach(c => clearConnection(c));
  cables = [];
  /* Remove FX nodes */
  nodes.filter(n => n.type === 'fx').forEach(n => {
    const el = document.getElementById('node-' + n.id);
    if (el) el.remove();
  });
  nodes = nodes.filter(n => n.type !== 'fx');
  renderCables();
  updateStatus();
  saveState();
};

window.pbResetLayout = function() {
  document.getElementById('pbMenu').classList.add('hidden');
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
  renderCables();
  saveState();
};

window.pbAutoRoute = function() {
  document.getElementById('pbMenu').classList.add('hidden');
  /* Auto: each pad → master out */
  nodes.filter(n => n.type === 'pad').forEach(pad => {
    if (!cables.find(c => c.from === pad.id && c.to === 'master')) {
      addCable(pad.id, 'master');
    }
  });
};

window.pbSavePreset = function() {
  document.getElementById('pbMenu').classList.add('hidden');
  const name = prompt('Nombre del preset:', 'Patchbay ' + new Date().toLocaleTimeString());
  if (!name) return;
  const presets = JSON.parse(localStorage.getItem('pb_presets') || '{}');
  presets[name] = exportState();
  localStorage.setItem('pb_presets', JSON.stringify(presets));
  alert('Preset guardado: ' + name);
};

window.pbFactoryPresets = function() {
  document.getElementById('pbMenu').classList.add('hidden');
  const list = FACTORY_PRESETS.map((p, i) => `${i + 1}. ${p.name} — ${p.description}`).join('\n');
  const input = prompt('PRESETS DE FÁBRICA:\n\n' + list + '\n\nEscribe número o nombre:');
  if (!input) return;
  const key = input.trim().toLowerCase();
  let preset = FACTORY_PRESETS.find((p, i) => String(i + 1) === key);
  if (!preset) preset = FACTORY_PRESETS.find(p => p.name.toLowerCase() === key || p.id.toLowerCase() === key);
  if (!preset) {
    alert('Preset no encontrado');
    return;
  }
  applyFactoryPreset(preset);
  alert('Preset de fábrica cargado: ' + preset.name);
};

window.pbLoadPreset = function() {
  document.getElementById('pbMenu').classList.add('hidden');
  const presets = JSON.parse(localStorage.getItem('pb_presets') || '{}');
  const names = Object.keys(presets);
  if (names.length === 0) { alert('No hay presets guardados'); return; }
  const name = prompt('Presets disponibles:\n' + names.map((n,i) => (i+1) + '. ' + n).join('\n') + '\n\nEscribe el nombre:');
  if (!name || !presets[name]) return;
  importState(presets[name]);
  alert('Preset cargado: ' + name);
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
    masterPos: (() => { const m = nodes.find(n => n.type === 'master'); return m ? {x: m.x, y: m.y} : null; })(),
    cables: cables.map(c => ({ from: c.from, to: c.to })),
    nextId
  };
}

function importState(data) {
  /* Clear everything */
  cables.forEach(c => clearConnection(c));
  cables = [];
  nodes.filter(n => n.type === 'fx').forEach(n => {
    const el = document.getElementById('node-' + n.id);
    if (el) el.remove();
  });
  nodes = nodes.filter(n => n.type !== 'fx');

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

function applyFactoryPreset(preset) {
  if (!preset || !Array.isArray(preset.chain) || preset.chain.length === 0) return;

  cables.forEach(c => clearConnection(c));
  cables = [];

  nodes.filter(n => n.type === 'fx').forEach(n => {
    const el = document.getElementById('node-' + n.id);
    if (el) el.remove();
  });
  nodes = nodes.filter(n => n.type !== 'fx');

  const fxMap = {};
  preset.chain.forEach(def => {
    const fxNode = createFxNode(def.fxType, def.x, def.y);
    if (fxNode && def.params) {
      Object.assign(fxNode.params, def.params);
      updateNodeDisplay(fxNode);
    }
    if (fxNode) fxMap[def.key] = fxNode.id;
  });

  const firstFxId = preset.chain.length ? fxMap[preset.chain[0].key] : null;
  if (firstFxId) {
    nodes.filter(n => n.type === 'pad').forEach(pad => addCable(pad.id, firstFxId));
  }

  for (let i = 0; i < preset.chain.length - 1; i++) {
    const fromId = fxMap[preset.chain[i].key];
    const toId = fxMap[preset.chain[i + 1].key];
    if (fromId && toId) addCable(fromId, toId);
  }

  const lastFxId = preset.chain.length ? fxMap[preset.chain[preset.chain.length - 1].key] : null;
  if (lastFxId) addCable(lastFxId, 'master');

  renderCables();
  updateStatus();
  saveState();
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
