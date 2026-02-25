/* =============================================================
   RED808 MULTIVIEW — Logic Engine
   ============================================================= */
(function () {
'use strict';

/* ── LAYOUTS ── */
const LAYOUTS = [
  {
    id: '2x1',
    label: '2×1',
    icon: '▬',
    count: 2,
    title: '2 paneles horizontales'
  },
  {
    id: '2x2',
    label: '2×2',
    icon: '⊞',
    count: 4,
    title: '4 paneles en cuadrícula'
  },
  {
    id: '1+2',
    label: '1+2',
    icon: '⊡',
    count: 3,
    title: '1 panel grande + 2 paneles'
  },
  {
    id: '3x2',
    label: '3×2',
    icon: '⊟',
    count: 6,
    title: '6 paneles (3 columnas)'
  },
  {
    id: '4x4',
    label: '4×4',
    icon: '⊞⊞',
    count: 16,
    title: '16 micro-paneles'
  }
];

/* ── PANELS ── */
const PANELS = [
  { id: 'empty',  label: 'VACÍO',      sub: 'Sin contenido', icon: '○',  url: null },
  { id: 'pads',   label: 'LIVE PADS',  sub: 'Pads en vivo',  icon: '🎹', url: '/?embed=1&tab=performance&solopads=1' },
  { id: 'xtra',   label: 'XTRA PADS',  sub: 'Pads extra',    icon: '🎲', url: '/?embed=1&tab=xtra-pads' },
  { id: 'seq',    label: 'SEQUENCER',  sub: 'Secuenciador',  icon: '🎵', url: '/?embed=1&tab=sequencer' },
  { id: 'vol',    label: 'VOLUMES',    sub: 'Mezclador',     icon: '🎚', url: '/?embed=1&tab=volumes' },
  { id: 'fx',     label: 'FX',         sub: 'Efectos',       icon: '🔊', url: '/?embed=1&tab=fx' },
  { id: 'path',   label: 'PATH',       sub: 'Signal routing',icon: '⬡',  url: '/patchbay?embed=1' },
  { id: 'midi',   label: 'MIDI',       sub: 'Control MIDI',  icon: '🎼', url: '/?embed=1&tab=midi' },
  { id: 'info',   label: 'INFO',       sub: 'Sistema',       icon: '📊', url: '/?embed=1&tab=info' }
];

const PANEL_MAP = Object.fromEntries(PANELS.map(p => [p.id, p]));

/* ── DEFAULT STATE ── */
const DEFAULT_LAYOUT = '2x2';
const DEFAULT_CELLS_BY_LAYOUT = {
  '2x1': [{ panelId: 'pads' },  { panelId: 'seq' }],
  '2x2': [{ panelId: 'pads' },  { panelId: 'seq' }, { panelId: 'vol' }, { panelId: 'path' }],
  '1+2': [{ panelId: 'seq' },   { panelId: 'pads' }, { panelId: 'vol' }],
  '3x2': [{ panelId: 'pads' },  { panelId: 'seq' },  { panelId: 'vol' }, { panelId: 'fx' }, { panelId: 'path' }, { panelId: 'empty' }],
  '4x4': Array(16).fill(null).map((_, i) => ({ panelId: 'empty' }))
};

/* ── APP STATE ── */
let state = {
  layout: DEFAULT_LAYOUT,
  editMode: false,
  cells: []           // [{ panelId: 'pads' }, ...]
};
let pickerTargetCell = -1; // cell index being configured

/* ── DOM REFS ── */
const gridEl    = () => document.getElementById('mvGrid');
const layoutsEl = () => document.getElementById('mvLayouts');
const pickerEl  = () => document.getElementById('mvPicker');
const editBtn   = () => document.getElementById('mvEditModeBtn');

/* ── PERSISTENCE ── */
function saveState() {
  try {
    localStorage.setItem('mv_state', JSON.stringify({
      layout: state.layout,
      cells: state.cells
    }));
  } catch (e) {}
}

function loadState() {
  try {
    const raw = localStorage.getItem('mv_state');
    if (!raw) return;
    const saved = JSON.parse(raw);
    if (saved.layout) state.layout = saved.layout;
    if (Array.isArray(saved.cells) && saved.cells.length) state.cells = saved.cells;
  } catch (e) {}
}

/* ── CELL ARRAY MANAGEMENT ── */
function buildCellsForLayout(layoutId, existingCells) {
  const layout = LAYOUTS.find(l => l.id === layoutId);
  if (!layout) return [];
  const count = layout.count;
  const base  = (existingCells || []).slice(0, count);
  while (base.length < count) {
    const def = (DEFAULT_CELLS_BY_LAYOUT[layoutId] || [])[base.length];
    base.push(def ? { panelId: def.panelId } : { panelId: 'empty' });
  }
  return base;
}

/* ── LAYOUT TOOLBAR ── */
function renderLayoutToolbar() {
  const el = layoutsEl();
  if (!el) return;
  el.innerHTML = '';
  LAYOUTS.forEach(layout => {
    const btn = document.createElement('button');
    btn.className = 'mv-layout-btn' + (state.layout === layout.id ? ' is-active' : '');
    btn.dataset.layout = layout.id;
    btn.title = layout.title;
    btn.innerHTML = `<span class="mv-layout-icon">${layout.icon}</span>${layout.label}`;
    btn.addEventListener('click', () => setLayout(layout.id));
    el.appendChild(btn);
  });
}

/* ── GRID RENDERING ── */
function renderGrid() {
  const el = gridEl();
  if (!el) return;
  el.setAttribute('data-layout', state.layout);
  if (state.editMode) el.classList.add('mv-edit-mode');
  else el.classList.remove('mv-edit-mode');

  /* Reuse / recreate cells */
  const existingCells = Array.from(el.querySelectorAll('.mv-cell'));
  const needed = state.cells.length;

  /* Remove extra */
  for (let i = existingCells.length - 1; i >= needed; i--) {
    existingCells[i].remove();
  }
  /* Add missing */
  for (let i = existingCells.length; i < needed; i++) {
    const cell = document.createElement('div');
    cell.className = 'mv-cell';
    el.appendChild(cell);
  }

  /* Update each cell */
  const cells = Array.from(el.querySelectorAll('.mv-cell'));
  cells.forEach((cell, idx) => updateCell(cell, idx));
}

function updateCell(cellEl, idx) {
  const cfg   = state.cells[idx] || { panelId: 'empty' };
  const panel = PANEL_MAP[cfg.panelId] || PANEL_MAP['empty'];
  const isEmpty = panel.id === 'empty';

  cellEl.classList.toggle('has-panel', !isEmpty);
  cellEl.innerHTML = '';

  /* ── Overlay (always present, visible only in edit mode) ── */
  const overlay = document.createElement('div');
  overlay.className = 'mv-cell-overlay';
  overlay.innerHTML = `
    <span class="mv-cell-overlay-icon">✎</span>
    <span class="mv-cell-overlay-panel-name">${panel.label}</span>
    <span class="mv-cell-overlay-label">Cambiar panel</span>
  `;
  overlay.addEventListener('click', () => openPicker(idx));
  cellEl.appendChild(overlay);

  if (isEmpty) {
    /* Empty state — full click area */
    const empty = document.createElement('div');
    empty.className = 'mv-cell-empty';
    empty.innerHTML = `
      <span class="mv-cell-empty-icon">${panel.icon}</span>
      <span class="mv-cell-empty-label">+ Agregar panel</span>
    `;
    empty.addEventListener('click', () => openPicker(idx));
    cellEl.appendChild(empty);
  } else {
    /* iframe */
    const iframe = document.createElement('iframe');
    iframe.src = panel.url;
    iframe.allow = 'autoplay';
    iframe.setAttribute('loading', 'lazy');
    iframe.setAttribute('sandbox', 'allow-scripts allow-same-origin allow-forms allow-popups allow-presentation');
    cellEl.appendChild(iframe);
  }
}

/* ── LAYOUT CHANGE ── */
function setLayout(layoutId) {
  const layout = LAYOUTS.find(l => l.id === layoutId);
  if (!layout) return;
  state.layout = layoutId;
  state.cells  = buildCellsForLayout(layoutId, state.cells);
  renderLayoutToolbar();
  renderGrid();
  saveState();
}

/* ── EDIT MODE ── */
window.mvToggleEditMode = function () {
  state.editMode = !state.editMode;
  const el = gridEl();
  if (el) el.classList.toggle('mv-edit-mode', state.editMode);
  const btn = editBtn();
  if (btn) btn.classList.toggle('is-active', state.editMode);
};

/* ── PANEL PICKER ── */
function openPicker(cellIdx) {
  pickerTargetCell = cellIdx;
  const picker   = pickerEl();
  const grid     = document.getElementById('mvPickerGrid');
  if (!picker || !grid) return;
  const cur = (state.cells[cellIdx] || {}).panelId || 'empty';
  grid.innerHTML = '';
  PANELS.forEach(panel => {
    const card = document.createElement('div');
    card.className = 'mv-panel-card'
      + (panel.id === 'empty' ? ' is-empty' : '')
      + (panel.id === cur ? ' is-current' : '');
    card.innerHTML = `
      <span class="mv-panel-card-icon">${panel.icon}</span>
      <span class="mv-panel-card-label">${panel.label}</span>
      <span class="mv-panel-card-sub">${panel.sub}</span>
    `;
    card.addEventListener('click', () => assignPanel(cellIdx, panel.id));
    grid.appendChild(card);
  });
  picker.classList.remove('hidden');
  picker.addEventListener('click', onPickerBackdrop);
}

function onPickerBackdrop(e) {
  if (e.target === pickerEl()) mvClosePicker();
}

window.mvClosePicker = function () {
  const picker = pickerEl();
  if (picker) {
    picker.classList.add('hidden');
    picker.removeEventListener('click', onPickerBackdrop);
  }
  pickerTargetCell = -1;
};

function assignPanel(cellIdx, panelId) {
  if (cellIdx < 0 || cellIdx >= state.cells.length) return;
  state.cells[cellIdx] = { panelId };
  mvClosePicker();
  /* Update single cell without full re-render to preserve other iframes */
  const cells = gridEl()?.querySelectorAll('.mv-cell');
  if (cells && cells[cellIdx]) updateCell(cells[cellIdx], cellIdx);
  saveState();
}

/* ── FULLSCREEN ── */
window.mvToggleFullscreen = function () {
  if (!document.fullscreenElement) {
    document.documentElement.requestFullscreen?.();
  } else {
    document.exitFullscreen?.();
  }
};

/* ── KEYBOARD ── */
document.addEventListener('keydown', (e) => {
  if (e.key === 'Escape') mvClosePicker();
  if (e.key === 'e' && !e.ctrlKey && !e.metaKey && !e.altKey) {
    const active = document.activeElement;
    if (!active || active === document.body) mvToggleEditMode();
  }
});

/* ── INIT ── */
function init() {
  loadState();

  /* Ensure cell count matches layout */
  state.cells = buildCellsForLayout(state.layout, state.cells);

  renderLayoutToolbar();
  renderGrid();

  /* Embed mode (if loaded inside an iframe) */
  if (new URLSearchParams(location.search).get('embed') === '1') {
    document.body.classList.add('embed-mode');
  }
}

document.addEventListener('DOMContentLoaded', init);

})();
