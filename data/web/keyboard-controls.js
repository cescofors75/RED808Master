// RED808 - Keyboard Controls Implementation
// Add to data/web/app.js

// ============= KEYBOARD SHORTCUTS =============

let selectedCell = null; // {track: number, step: number}
let selectedPad = null;  // number (0-15)
let selectedTrack = null; // number (0-15)

// Initialize keyboard system - call from app.js after DOM ready
function initKeyboardControls() {
  // Single keyboard listener (no capture phase to avoid blocking)
  document.addEventListener('keydown', function(e) {
    const isInput = e.target.tagName === 'INPUT' || e.target.tagName === 'TEXTAREA' || e.target.isContentEditable;
    
    // Skip if typing in input field
    if (isInput) return;
    
    // Handle shortcuts - only prevent default if truly handled
    const handled = handleKeyboardShortcut(e);
    if (handled) {
      e.preventDefault();
      // NO stopPropagation - let other handlers see it
    }
  });
}

function handleKeyboardShortcut(e) {
  const key = e.key.toUpperCase();
  
  // ESC: Close velocity editor or track filter panel
  if (e.key === 'Escape') {
    if (selectedCell) {
      hideVelocityEditor();
      showToast('Editor cerrado', TOAST_TYPES.INFO, 1000);
      return true;
    }
    if (selectedTrack !== null) {
      hideTrackFilterPanel();
      showToast('Panel de filtro cerrado', TOAST_TYPES.INFO, 1000);
      return true;
    }
  }
  
  // ============= VELOCITY EDITING (only when cell selected) =============
  if (selectedCell) {
    const {track, step} = selectedCell;
    let velocity = getStepVelocity(track, step);
    let changed = false;
    
    switch(e.key) {      
      // Quick velocity presets (only when cell selected)
      case 'z':
      case 'Z':
        velocity = 40; // Ghost note
        changed = true;
        break;
      case 'x':
      case 'X':
        velocity = 70; // Soft
        changed = true;
        break;
      case 'c':
      case 'C':
        velocity = 100; // Medium
        changed = true;
        break;
      case 'v':
      case 'V':
        velocity = 127; // Accent
        changed = true;
        break;
    }
    
    if (changed) {
      setStepVelocity(track, step, velocity);
      updateStepVelocityUI(track, step, velocity);
      showVelocityFeedback(velocity);
      return true;
    }
  }
  
  // ============= TRANSPORT & GLOBAL CONTROLS =============
  
  // SPACE: Play/Pause
  if (key === ' ') {
    e.preventDefault();
    if (window.togglePlayPause) {
      const isPlaying = window.togglePlayPause();
      showToast(isPlaying ? '▶ Playing' : '⏸ Paused', TOAST_TYPES.INFO, 1500);
    }
    return true;
  }
  
  // N: Next Pattern
  if (key === 'N' && !selectedCell) {
    e.preventDefault();
    if (window.changePattern) {
      window.changePattern(1);
      showToast('⏭ Next Pattern', TOAST_TYPES.INFO, 1500);
    }
    return true;
  }
  
  // B: Previous Pattern  
  if (key === 'B' && !selectedCell) {
    e.preventDefault();
    if (window.changePattern) {
      window.changePattern(-1);
      showToast('⏮ Previous Pattern', TOAST_TYPES.INFO, 1500);
    }
    return true;
  }
  
  // Q-Y: Direct Pattern Selection (1-6)
  if (key === 'Q' && !selectedCell) {
    e.preventDefault();
    if (window.selectPattern) window.selectPattern(0);
    showToast('🎶 HIP HOP', TOAST_TYPES.INFO, 1500);
    return true;
  }
  if (key === 'W' && !selectedCell) {
    e.preventDefault();
    if (window.selectPattern) window.selectPattern(1);
    showToast('🎶 TECHNO', TOAST_TYPES.INFO, 1500);
    return true;
  }
  if (key === 'E' && !selectedCell) {
    e.preventDefault();
    if (window.selectPattern) window.selectPattern(2);
    showToast('🎶 DnB', TOAST_TYPES.INFO, 1500);
    return true;
  }
  if (key === 'R' && !selectedCell) {
    e.preventDefault();
    if (window.selectPattern) window.selectPattern(3);
    showToast('🎶 BREAK', TOAST_TYPES.INFO, 1500);
    return true;
  }
  if (key === 'T' && !selectedCell) {
    e.preventDefault();
    if (window.selectPattern) window.selectPattern(4);
    showToast('🎶 HOUSE', TOAST_TYPES.INFO, 1500);
    return true;
  }
  if (key === 'Y' && !selectedCell) {
    e.preventDefault();
    if (window.selectPattern) window.selectPattern(5);
    showToast('🎶 TRAP', TOAST_TYPES.INFO, 1500);
    return true;
  }
  
  // [: Decrease BPM
  if (key === '[') {
    e.preventDefault();
    if (window.adjustBPM) {
      window.adjustBPM(-5);
      showToast('🎵 BPM -5', TOAST_TYPES.INFO, 1500);
    }
    return true;
  }
  
  // ]: Increase BPM
  if (key === ']') {
    e.preventDefault();
    if (window.adjustBPM) {
      window.adjustBPM(5);
      showToast('🎵 BPM +5', TOAST_TYPES.INFO, 1500);
    }
    return true;
  }
  
  // M: Toggle Color Mode
  if (key === 'M' && !selectedCell) {
    e.preventDefault();
    const colorToggle = document.getElementById('colorToggle');
    if (colorToggle) {
      colorToggle.click();
      const isMono = document.body.classList.contains('mono-mode');
      showToast(isMono ? '🎨 Mono Mode' : '🌈 Color Mode', TOAST_TYPES.INFO, 1500);
    }
    return true;
  }
  
  // H: Toggle Keyboard Sidebar
  if (key === 'H' && !selectedCell) {
    e.preventDefault();
    if (window.toggleKeyboardSidebar) window.toggleKeyboardSidebar();
    return true;
  }
  
  // A: Decrease Sequencer Volume
  if (key === 'A' && !selectedCell) {
    e.preventDefault();
    if (window.adjustSequencerVolume) {
      window.adjustSequencerVolume(-5);
      showToast('🔉 Seq Vol -5', TOAST_TYPES.INFO, 1500);
    }
    return true;
  }
  
  // S: Increase Sequencer Volume
  if (key === 'S' && !selectedCell) {
    e.preventDefault();
    if (window.adjustSequencerVolume) {
      window.adjustSequencerVolume(5);
      showToast('🔊 Seq Vol +5', TOAST_TYPES.INFO, 1500);
    }
    return true;
  }
  
  // -: Decrease Master Volume
  if (e.key === '-' || e.key === '_') {
    if (!selectedCell) {
      e.preventDefault();
      if (window.adjustVolume) {
        window.adjustVolume(-5);
        showToast('🔉 Master Vol -5', TOAST_TYPES.INFO, 1500);
      }
      return true;
    }
  }
  
  // +/=: Increase Master Volume
  if (e.key === '+' || e.key === '=') {
    if (!selectedCell) {
      e.preventDefault();
      if (window.adjustVolume) {
        window.adjustVolume(5);
        showToast('🔊 Master Vol +5', TOAST_TYPES.INFO, 1500);
      }
      return true;
    }
  }
  
  // ============= FILTER SHORTCUTS =============
  // F1-F10: Apply filters to selected track/pad
  if (e.key.startsWith('F') && !e.ctrlKey && !e.altKey) {
    const fKey = parseInt(e.key.substring(1));
    if (fKey >= 1 && fKey <= 10) {
      e.preventDefault();
      applyFilterShortcut(fKey, e.shiftKey);
      return true;
    }
  }
  
  // ============= NAVIGATION (only when cell selected, NO ARROWS) =============
  if (selectedCell) {
    const {track, step} = selectedCell;
    let newTrack = track;
    let newStep = step;
    let navigate = false;
    
    switch(e.key) {
      case ',':
      case '<':
        // Move left (previous step)
        newStep = (step - 1 + 16) % 16;
        navigate = true;
        showToast('← Step ' + (newStep + 1), TOAST_TYPES.INFO, 1000);
        break;
      case '.':
      case '>':
        // Move right (next step)
        newStep = (step + 1) % 16;
        navigate = true;
        showToast('→ Step ' + (newStep + 1), TOAST_TYPES.INFO, 1000);
        break;
      case '-':
      case '_':
        // Move up (previous track) - but only if Shift is pressed to avoid conflict
        if (e.shiftKey) {
          newTrack = (track - 1 + 16) % 16;
          navigate = true;
          showToast('↑ Track ' + (newTrack + 1), TOAST_TYPES.INFO, 1000);
        }
        break;
      case '+':
      case '=':
        // Move down (next track) - but only if Shift is pressed to avoid conflict
        if (e.shiftKey) {
          newTrack = (track + 1) % 16;
          navigate = true;
          showToast('↓ Track ' + (newTrack + 1), TOAST_TYPES.INFO, 1000);
        }
        break;
    }
    
    if (navigate) {
      e.preventDefault();
      selectCell(newTrack, newStep);
      return true;
    }
  }
  
  // ============= PAD TRIGGERS (1-0, Q-Y) =============
  // Trigger pads directly here instead of passing to app.js
  if (!selectedCell) {
    const padIndex = window.getPadIndexFromEvent ? window.getPadIndexFromEvent(e) : null;
    if (padIndex !== null) {
      e.preventDefault();
      
      // Auto-select pad when triggered via keyboard (for filter shortcuts)
      selectPad(padIndex);
      
      // Handle Shift + pad = mute/unmute
      if (e.shiftKey) {
        if (window.setTrackMuted && window.trackMutedState) {
          window.setTrackMuted(padIndex, !window.trackMutedState[padIndex], true);
        }
        return true;
      }
      
      // Trigger pad with tremolo
      if (window.keyboardPadsActive && !window.keyboardPadsActive[padIndex]) {
        window.keyboardPadsActive[padIndex] = true;
        const padElement = document.querySelector(`.pad[data-pad="${padIndex}"]`);
        if (padElement && window.startKeyboardTremolo) {
          window.startKeyboardTremolo(padIndex, padElement);
        }
      }
      return true;
    }
    
    // Check for unassigned keys (when no cell selected and not a pad)
    const unassignedKeys = ['u', 'i', 'o', 'p', 'd', 'f', 'g', 'j', 'k', 'l'];
    const keyLower = e.key.toLowerCase();
    if (unassignedKeys.includes(keyLower) && !e.ctrlKey && !e.altKey && !e.metaKey) {
      showToast(`Key "${e.key.toUpperCase()}" is not assigned`, TOAST_TYPES.UNASSIGNED, 2000);
      return true;
    }
  }
  
  return false; // Not handled
}

// ============= VELOCITY FUNCTIONS =============

function setStepVelocity(track, step, velocity) {
  // Send to ESP32
  if (window.sendWebSocket) {
    window.sendWebSocket({
      cmd: 'setStepVelocity',
      track: track,
      step: step,
      velocity: velocity
    });
  }
  
  // Update local cache
  if (!window.patternVelocities) {
    window.patternVelocities = {};
  }
  if (!window.patternVelocities[track]) {
    window.patternVelocities[track] = {};
  }
  window.patternVelocities[track][step] = velocity;
  
  // Update UI
  updateStepVelocityUI(track, step, velocity);
}

function getStepVelocity(track, step) {
  if (window.patternVelocities && 
      window.patternVelocities[track] && 
      window.patternVelocities[track][step] !== undefined) {
    return window.patternVelocities[track][step];
  }
  return 127; // Default
}

function updateStepVelocityUI(track, step, velocity) {
  const stepElement = document.querySelector(`[data-track="${track}"][data-step="${step}"]`);
  if (!stepElement) return;
  
  // Set data attribute
  stepElement.setAttribute('data-velocity', velocity);
  
  // Update visual styling based on velocity
  if (stepElement.classList.contains('active')) {
    // Calculate opacity (0.3 to 1.0)
    const opacity = Math.max(0.3, velocity / 127);
    stepElement.style.opacity = opacity;
    
    // Calculate color gradient from red (low) to green (high)
    // vel 1-50: red, 51-100: yellow, 101-127: green
    let velocityColor;
    let brightness;
    
    if (velocity <= 50) {
      // Red zone (ghost notes)
      velocityColor = '#ff4444';
      brightness = 0.6 + (velocity / 50) * 0.2; // 0.6 to 0.8
    } else if (velocity <= 100) {
      // Yellow-orange zone (normal)
      const ratio = (velocity - 50) / 50;
      velocityColor = `rgb(255, ${Math.floor(100 + ratio * 155)}, 50)`;
      brightness = 0.8 + ratio * 0.2; // 0.8 to 1.0
    } else {
      // Green zone (accents)
      velocityColor = '#00ff88';
      brightness = 1.0 + ((velocity - 100) / 27) * 0.3; // 1.0 to 1.3
    }
    
    stepElement.style.setProperty('--velocity-color', velocityColor);
    stepElement.style.setProperty('--velocity-brightness', brightness);
    
    // Add glow effect for high velocities (>= 100)
    if (velocity >= 100) {
      stepElement.classList.add('velocity-high');
    } else {
      stepElement.classList.remove('velocity-high');
    }
  }
}

function showVelocityFeedback(velocity) {
  // Show temporary tooltip with velocity value
  let tooltip = document.getElementById('velocity-tooltip');
  if (!tooltip) {
    tooltip = document.createElement('div');
    tooltip.id = 'velocity-tooltip';
    tooltip.style.cssText = `
      position: fixed;
      top: 50%;
      left: 50%;
      transform: translate(-50%, -50%);
      background: rgba(0, 0, 0, 0.9);
      color: white;
      padding: 20px 40px;
      border-radius: 10px;
      font-size: 32px;
      font-weight: bold;
      z-index: 10000;
      pointer-events: none;
      transition: opacity 0.2s;
    `;
    document.body.appendChild(tooltip);
  }
  
  tooltip.textContent = `Velocity: ${velocity}`;
  tooltip.style.opacity = '1';
  
  // Clear existing timeout
  if (tooltip.fadeTimeout) {
    clearTimeout(tooltip.fadeTimeout);
  }
  
  // Fade out after 1 second
  tooltip.fadeTimeout = setTimeout(() => {
    tooltip.style.opacity = '0';
  }, 1000);
}

// ============= FILTER SHORTCUTS =============

const FILTER_SHORTCUTS = {
  1: { type: 1, cutoff: 300, resonance: 5, name: 'Low Pass 300Hz Q5' },
  2: { type: 2, cutoff: 3000, resonance: 5, name: 'High Pass 3kHz Q5' },
  3: { type: 3, cutoff: 800, resonance: 8, name: 'Band Pass 800Hz Q8' },
  4: { type: 9, cutoff: 500, resonance: 15, name: 'Resonant 500Hz Q15' },
  5: { type: 7, cutoff: 200, resonance: 1, gain: 10, name: 'Low Shelf +10dB' },
  6: { type: 8, cutoff: 4000, resonance: 1, gain: 10, name: 'High Shelf +10dB' },
  7: { type: 6, cutoff: 1500, resonance: 5, gain: 10, name: 'Peaking 1.5kHz +10dB' },
  8: { type: 4, cutoff: 800, resonance: 10, name: 'Notch 800Hz Q10' },
  9: { type: 1, cutoff: 150, resonance: 10, name: 'Low Pass 150Hz Q10' },
  10: { type: 0, name: 'Clear Filter' }
};

function applyFilterShortcut(fKey, isShiftPressed) {
  const filter = FILTER_SHORTCUTS[fKey];
  if (!filter) return;
  
  if (isShiftPressed && selectedPad !== null) {
    // Apply to pad
    applyPadFilter(selectedPad, filter);
  } else if (selectedTrack !== null) {
    // Apply to track
    applyTrackFilter(selectedTrack, filter);
  } else {
    showNotification('Select a track or pad first');
  }
}

function applyTrackFilter(track, filter) {
  const cmd = {
    cmd: filter.type === 0 ? 'clearTrackFilter' : 'setTrackFilter',
    track: track
  };
  
  if (filter.type !== 0) {
    cmd.filterType = filter.type;
    cmd.cutoff = filter.cutoff;
    cmd.resonance = filter.resonance;
    if (filter.gain !== undefined) {
      cmd.gain = filter.gain;
    }
  }
  
  if (window.sendWebSocket) {
    window.sendWebSocket(cmd);
  }
  
  // Show toast notification
  const filterName = filter.name || (filter.type === 0 ? 'Filter cleared' : 'Filter applied');
  showToast(`Track ${track + 1}: ${filterName}`, filter.type === 0 ? TOAST_TYPES.INFO : TOAST_TYPES.SUCCESS, 2500);
}

function applyPadFilter(pad, filter) {
  const cmd = {
    cmd: filter.type === 0 ? 'clearPadFilter' : 'setPadFilter',
    pad: pad
  };
  
  if (filter.type !== 0) {
    cmd.filterType = filter.type;
    cmd.cutoff = filter.cutoff;
    cmd.resonance = filter.resonance;
    if (filter.gain !== undefined) {
      cmd.gain = filter.gain;
    }
  }
  
  if (window.sendWebSocket) {
    window.sendWebSocket(cmd);
  }
  
  // Update pad filter state and visual indicator
  if (window.padFilterState) {
    window.padFilterState[pad] = filter.type;
  }
  if (window.updatePadFilterIndicator) {
    window.updatePadFilterIndicator(pad);
  }
  
  // Show toast notification for filter changes
  const filterName = filter.name || (filter.type === 0 ? 'Filter cleared' : 'Filter applied');
  const names = window.padNames || ['BD', 'SD', 'CH', 'OH', 'CP', 'RS', 'CL', 'CY'];
  showToast(`${names[pad]}: ${filterName}`, filter.type === 0 ? TOAST_TYPES.INFO : TOAST_TYPES.SUCCESS, 2500);
}

// ============= UI SELECTION =============

function selectCell(track, step) {
  // Remove previous selection
  document.querySelectorAll('.step.selected').forEach(el => {
    el.classList.remove('selected');
  });
  
  // Add new selection
  const stepElement = document.querySelector(`[data-track="${track}"][data-step="${step}"]`);
  if (stepElement) {
    stepElement.classList.add('selected');
    selectedCell = {track, step};
    
    // Show velocity editor
    showVelocityEditor(track, step);
  }
}

function selectTrack(track) {
  selectedTrack = track;
  
  // Highlight track
  document.querySelectorAll('.track-row').forEach(el => {
    el.classList.remove('selected-track');
  });
  
  const trackElement = document.querySelector(`[data-track="${track}"]`)?.closest('.track-row');
  if (trackElement) {
    trackElement.classList.add('selected-track');
  }
  
  // Show track filter panel
  showTrackFilterPanel(track);
}

function selectPad(pad) {
  selectedPad = pad;
  
  // Highlight pad
  document.querySelectorAll('.pad').forEach(el => {
    el.classList.remove('selected-pad');
  });
  
  const padElement = document.querySelector(`[data-pad="${pad}"]`);
  if (padElement) {
    padElement.classList.add('selected-pad');
  }
}

// Export para app.js
window.selectCell = selectCell;
window.selectTrack = selectTrack;
window.selectPad = selectPad;

// ============= TRACK FILTER UI =============

function showTrackFilterPanel(track) {
  // Always set selectedTrack when panel is shown
  selectedTrack = track;
  
  // Remove existing backdrop
  const existingBackdrop = document.querySelector('.track-filter-backdrop');
  if (existingBackdrop) existingBackdrop.remove();
  
  let panel = document.getElementById('track-filter-panel');
  if (!panel) {
    panel = createTrackFilterPanel();
  }
  
  const trackNames = ['BD', 'SD', 'CH', 'OH', 'CP', 'RS', 'CL', 'CY',
                       'T9', 'T10', 'T11', 'T12', 'T13', 'T14', 'T15', 'T16'];
  panel.querySelector('#track-filter-title').textContent = `🎛️ Track ${track + 1} — ${trackNames[track] || '?'}`;
  
  // Mark active filter
  const currentFilter = (window.trackFilterState && window.trackFilterState[track]) || 0;
  panel.querySelectorAll('.filter-btn').forEach((btn, idx) => {
    btn.classList.toggle('active-filter', idx === currentFilter);
  });
  
  // Create backdrop
  const backdrop = document.createElement('div');
  backdrop.className = 'track-filter-backdrop';
  backdrop.addEventListener('click', () => hideTrackFilterPanel());
  document.body.appendChild(backdrop);
  
  // Show panel centered (CSS handles positioning)
  panel.style.display = 'block';
  panel.style.left = '';
  panel.style.top = '';
  
  // Trigger animation
  requestAnimationFrame(() => {
    panel.classList.add('visible');
  });
}

function hideTrackFilterPanel() {
  const panel = document.getElementById('track-filter-panel');
  if (panel) {
    panel.classList.remove('visible');
    setTimeout(() => { panel.style.display = 'none'; }, 250);
  }
  const backdrop = document.querySelector('.track-filter-backdrop');
  if (backdrop) backdrop.remove();
  selectedTrack = null;
}

function createTrackFilterPanel() {
  const panel = document.createElement('div');
  panel.id = 'track-filter-panel';
  panel.className = 'track-filter-panel';
  panel.innerHTML = `
    <div class="track-filter-header">
      <span id="track-filter-title">Filtro Track</span>
      <button class="filter-close-btn" onclick="window.hideTrackFilterPanel()">×</button>
    </div>
    <div class="track-filter-content">
      <div class="filter-grid">
        ${FILTER_TYPES.map((filter, idx) => `
          <button class="filter-btn" data-filter="${idx}" onclick="window.applyTrackFilterFromPanel(${idx})" title="F${idx + 1}">
            <span class="filter-icon">${filter.icon}</span>
            <span class="filter-name">${filter.name}</span>
          </button>
        `).join('')}
      </div>
    </div>
    <div class="track-filter-footer">
      <small>F1-F10: Aplicar filtro | ESC: Cerrar</small>
    </div>
  `;
  
  document.body.appendChild(panel);
  
  // Stop propagation on clicks inside panel
  panel.addEventListener('click', function(e) {
    e.stopPropagation();
  });
  
  return panel;
}

function applyTrackFilterFromPanel(filterType) {
  if (selectedTrack !== null) {
    // Map filter type index to actual filter shortcuts
    const filterShortcuts = {
      0: { type: 0, name: 'Clear Filter' },
      1: { type: 1, cutoff: 1000, resonance: 1, name: 'Low Pass 1kHz' },
      2: { type: 2, cutoff: 1000, resonance: 1, name: 'High Pass 1kHz' },
      3: { type: 3, cutoff: 1000, resonance: 2, name: 'Band Pass 1kHz' },
      4: { type: 4, cutoff: 1000, resonance: 2, name: 'Notch 1kHz' },
      5: { type: 7, cutoff: 500, resonance: 1, gain: 6, name: 'Low Shelf +6dB' },
      6: { type: 8, cutoff: 4000, resonance: 1, gain: 6, name: 'High Shelf +6dB' },
      7: { type: 6, cutoff: 1000, resonance: 2, gain: 6, name: 'Peak 1kHz' },
      8: { type: 5, cutoff: 1000, resonance: 1, name: 'All Pass 1kHz' },
      9: { type: 9, cutoff: 1000, resonance: 10, name: 'Resonant 1kHz' }
    };
    
    const filter = filterShortcuts[filterType];
    if (filter) {
      applyTrackFilter(selectedTrack, filter);
      
      // Update trackFilterState
      if (window.trackFilterState) {
        window.trackFilterState[selectedTrack] = filterType;
      }
      
      // Sync: also apply filter to corresponding pad if sync enabled
      if (window.padSeqSyncEnabled && window.syncFilterToPad) {
        window.syncFilterToPad(selectedTrack, filterType);
      }
    }
    // Force close panel with slight delay to ensure it closes
    setTimeout(() => {
      hideTrackFilterPanel();
      selectedTrack = null;
    }, 100);
  }
}

window.showTrackFilterPanel = showTrackFilterPanel;
window.hideTrackFilterPanel = hideTrackFilterPanel;
window.applyTrackFilterFromPanel = applyTrackFilterFromPanel;
window.hideVelocityEditor = hideVelocityEditor;
window.initKeyboardControls = initKeyboardControls;

// Global click handler to close editors when clicking outside
let globalClickHandlerAdded = false;
if (!globalClickHandlerAdded) {
  document.addEventListener('click', function(e) {
    // Close velocity editor if clicking outside
    const velEditor = document.getElementById('velocity-editor');
    if (velEditor && velEditor.style.display === 'block') {
      if (!velEditor.contains(e.target) && !e.target.closest('.seq-step')) {
        hideVelocityEditor();
      }
    }
    
    // Close filter panel if clicking outside
    const filterPanel = document.getElementById('track-filter-panel');
    if (filterPanel && filterPanel.style.display === 'block') {
      if (!filterPanel.contains(e.target) && !e.target.closest('.track-label')) {
        hideTrackFilterPanel();
      }
    }
  });
  globalClickHandlerAdded = true;
}

// ============= VELOCITY EDITOR UI =============

function showVelocityEditor(track, step) {
  let editor = document.getElementById('velocity-editor');
  if (!editor) {
    editor = createVelocityEditor();
  }
  
  const velocity = getStepVelocity(track, step);
  
  editor.querySelector('#vel-slider').value = velocity;
  editor.querySelector('#vel-value').textContent = velocity;
  editor.style.display = 'block';
  
  // Position near selected cell
  const stepElement = document.querySelector(`[data-track="${track}"][data-step="${step}"]`);
  if (stepElement) {
    const rect = stepElement.getBoundingClientRect();
    editor.style.left = `${rect.left}px`;
    editor.style.top = `${rect.bottom + 5}px`;
  }
}

function hideVelocityEditor() {
  const editor = document.getElementById('velocity-editor');
  if (editor) {
    editor.style.display = 'none';
  }
  // Clear selected cell
  document.querySelectorAll('[data-track][data-step]').forEach(el => {
    el.classList.remove('selected');
  });
  selectedCell = null;
}

function createVelocityEditor() {
  const editor = document.createElement('div');
  editor.id = 'velocity-editor';
  editor.className = 'velocity-editor';
  editor.innerHTML = `
    <div class="velocity-editor-content">
      <div class="velocity-editor-header">
        <label>Velocity: <span id="vel-value">127</span></label>
        <button class="velocity-close-btn" onclick="window.hideVelocityEditor()">×</button>
      </div>
      <input type="range" id="vel-slider" min="1" max="127" value="127">
      <div class="velocity-presets">
        <button onclick="applyVelocityPreset(40)" title="Q">Ghost</button>
        <button onclick="applyVelocityPreset(70)" title="W">Soft</button>
        <button onclick="applyVelocityPreset(100)" title="E">Medium</button>
        <button onclick="applyVelocityPreset(127)" title="R">Accent</button>
      </div>
      <div class="keyboard-hints">
        <small>↑↓: ±10 | Shift+↑↓: ±1 | Q/W/E/R: Presets | ESC: Cerrar</small>
      </div>
    </div>
  `;
  
  document.body.appendChild(editor);
  
  // Slider event
  editor.querySelector('#vel-slider').addEventListener('input', function(e) {
    const velocity = parseInt(e.target.value);
    editor.querySelector('#vel-value').textContent = velocity;
    if (selectedCell) {
      setStepVelocity(selectedCell.track, selectedCell.step, velocity);
    }
  });
  
  // Stop propagation on clicks inside editor
  editor.addEventListener('click', function(e) {
    e.stopPropagation();
  });
  
  return editor;
}

function applyVelocityPreset(velocity) {
  if (selectedCell) {
    setStepVelocity(selectedCell.track, selectedCell.step, velocity);
    document.getElementById('vel-slider').value = velocity;
    document.getElementById('vel-value').textContent = velocity;
  }
}

// ============= NOTIFICATION SYSTEM =============

// ============= WEBSOCKET MESSAGE HANDLERS =============

// Export function to window for app.js to call
window.handleKeyboardWebSocketMessage = function(data) {
  if (data.type === 'pattern' && data.velocities) {
    // Store velocities when pattern is received
    window.patternVelocities = data.velocities;
    
    // Update UI for all steps - velocities comes as object with string keys "0", "1", etc.
    for (let track = 0; track < 16; track++) {
      const trackKey = track.toString();
      const trackVels = data.velocities[trackKey];
      if (!trackVels) continue; // Skip if track velocities undefined
      for (let step = 0; step < 16; step++) {
        if (trackVels[step] !== undefined) {
          updateStepVelocityUI(track, step, trackVels[step]);
        }
      }
    }
  }
  
  if (data.type === 'stepVelocitySet') {
    // Another client changed velocity
    updateStepVelocityUI(data.track, data.step, data.velocity);
  }
  
  if (data.type === 'trackFilterSet' || data.type === 'trackFilterCleared') {
    updateFilterIndicator('track', data.track, data.activeFilters);
  }
  
  if (data.type === 'padFilterSet' || data.type === 'padFilterCleared') {
    updateFilterIndicator('pad', data.pad, data.activeFilters);
  }
}

function updateFilterIndicator(type, index, activeCount) {
  // Esta función ahora está deshabilitada porque usamos el nuevo sistema de badges de filtro
  // Los filtros de pads se muestran con .pad-filter-indicator en app.js
  // Los filtros de tracks se muestran con .track-filter-badge en app.js
  
  // Removemos cualquier indicador antiguo .filter-indicator si existe
  const selector = type === 'track' 
    ? `[data-track="${index}"]` 
    : `[data-pad="${index}"]`;
  
  const element = document.querySelector(selector);
  if (element) {
    const oldIndicator = element.querySelector('.filter-indicator');
    if (oldIndicator) {
      oldIndicator.remove();
    }
  }
}

// ============= CLICK HANDLERS =============

// Add click handlers to sequencer grid
document.addEventListener('DOMContentLoaded', function() {
  // Step cells
  document.querySelectorAll('.step').forEach(step => {
    step.addEventListener('click', function(e) {
      const track = parseInt(this.getAttribute('data-track'));
      const stepNum = parseInt(this.getAttribute('data-step'));
      selectCell(track, stepNum);
      
      // Toggle step if not active
      if (!this.classList.contains('active')) {
        toggleStep(track, stepNum);
      }
    });
  });
  
  // Track labels (for track selection)
  document.querySelectorAll('.track-label').forEach(label => {
    label.addEventListener('click', function() {
      const track = parseInt(this.getAttribute('data-track'));
      selectTrack(track);
    });
  });
  
  // Pad buttons (for pad selection)
  document.querySelectorAll('.pad').forEach(pad => {
    pad.addEventListener('click', function() {
      const padNum = parseInt(this.getAttribute('data-pad'));
      selectPad(padNum);
    });
  });
});

// ============= HELP OVERLAY =============

// ============= KEYBOARD LEGEND SIDEBAR =============

let sidebarOpacity = 0.98; // Default opacity (98%)

function toggleKeyboardSidebar() {
  const sidebar = document.getElementById('keyboard-sidebar');
  if (sidebar) {
    const isOpening = !sidebar.classList.contains('open');
    sidebar.classList.toggle('open');
    if (isOpening) {
      showToast('Press H to close sidebar', TOAST_TYPES.INFO, 2000);
    }
  } else {
    createKeyboardSidebar();
    showToast('Keyboard shortcuts sidebar', TOAST_TYPES.INFO, 2000);
  }
}

function toggleSidebarTransparency() {
  const sidebar = document.getElementById('keyboard-sidebar');
  if (!sidebar) return;
  
  // Cycle: 0.98 -> 0.75 -> 0.5 -> 1.0 (opaque) -> 0.98
  if (sidebarOpacity === 0.98) sidebarOpacity = 0.75;
  else if (sidebarOpacity === 0.75) sidebarOpacity = 0.5;
  else if (sidebarOpacity === 0.5) sidebarOpacity = 1.0;
  else sidebarOpacity = 0.98;
  
  sidebar.style.setProperty('--sidebar-opacity', sidebarOpacity);
  const btn = sidebar.querySelector('.transparency-btn');
  if (btn) {
    if (sidebarOpacity === 1.0) btn.textContent = '🔳';
    else if (sidebarOpacity >= 0.9) btn.textContent = '◪';
    else if (sidebarOpacity >= 0.6) btn.textContent = '◫';
    else btn.textContent = '⬚';
  }
}

function createKeyboardSidebar() {
  const sidebar = document.createElement('div');
  sidebar.id = 'keyboard-sidebar';
  sidebar.className = 'open';
  sidebar.style.setProperty('--sidebar-opacity', sidebarOpacity);
  
  sidebar.innerHTML = `
    <div class="sidebar-header">
      <h2>⌨️ Keyboard</h2>
      <div class="sidebar-controls">
        <button class="transparency-btn" onclick="toggleSidebarTransparency()" title="Toggle Transparency">◪</button>
        <button class="close-btn" onclick="toggleKeyboardSidebar()" title="Close (H)">✕</button>
      </div>
    </div>
    
    <div class="sidebar-content">
      <div class="key-section">
        <h3>🎚️ Transport</h3>
        <div class="key-list">
          <div class="key-item"><kbd>Space</kbd><span>Play/Pause</span></div>
          <div class="key-item"><kbd>N</kbd><span>Next Pattern</span></div>
          <div class="key-item"><kbd>B</kbd><span>Prev Pattern</span></div>
          <div class="key-item"><kbd>[</kbd><span>BPM -5</span></div>
          <div class="key-item"><kbd>]</kbd><span>BPM +5</span></div>
          <div class="key-item"><kbd>M</kbd><span>Color Mode</span></div>
        </div>
      </div>
      
      <div class="key-section">
        <h3>🔊 Volume</h3>
        <div class="key-list">
          <div class="key-item"><kbd>A</kbd><span>Seq Vol -5</span></div>
          <div class="key-item"><kbd>S</kbd><span>Seq Vol +5</span></div>
          <div class="key-item"><kbd>-</kbd><span>Master -5</span></div>
          <div class="key-item"><kbd>+</kbd><span>Master +5</span></div>
        </div>
      </div>
      
      <div class="key-section">
        <h3>🎹 Live Pads (16 Instruments)</h3>
        <div class="key-list compact">
          <div class="key-item"><kbd>1</kbd><span>BD (Bass Drum)</span></div>
          <div class="key-item"><kbd>2</kbd><span>SD (Snare)</span></div>
          <div class="key-item"><kbd>3</kbd><span>CH (Closed HH)</span></div>
          <div class="key-item"><kbd>4</kbd><span>OH (Open HH)</span></div>
          <div class="key-item"><kbd>5</kbd><span>CY (Cymbal)</span></div>
          <div class="key-item"><kbd>6</kbd><span>CP (Clap)</span></div>
          <div class="key-item"><kbd>7</kbd><span>RS (Rimshot)</span></div>
          <div class="key-item"><kbd>8</kbd><span>CB (Cowbell)</span></div>
          <div class="key-item"><span>Pads 9-16: touch only (LT, MT, HT, MA, CL, HC, MC, LC)</span></div>
        </div>
        <div class="key-note">Hold = Tremolo | Shift+Key = Mute Track</div>
        <div class="key-note">Auto-selects pad for Shift+F1-F10 filters</div>
      </div>
      
      <div class="key-section">
        <h3>🎵 Patterns</h3>
        <div class="key-list">
          <div class="key-item"><kbd>Q</kbd><span>HIP HOP</span></div>
          <div class="key-item"><kbd>W</kbd><span>TECHNO</span></div>
          <div class="key-item"><kbd>E</kbd><span>DnB</span></div>
          <div class="key-item"><kbd>R</kbd><span>BREAK</span></div>
          <div class="key-item"><kbd>T</kbd><span>HOUSE</span></div>
          <div class="key-item"><kbd>Y</kbd><span>TRAP</span></div>
          <div class="key-item"><kbd>N</kbd><span>Next Pattern</span></div>
          <div class="key-item"><kbd>B</kbd><span>Prev Pattern</span></div>
        </div>
      </div>
      
      <div class="key-section">
        <h3>🎵 Velocity (step selected)</h3>
        <div class="key-list">
          <div class="key-item"><kbd>Z</kbd><span>Ghost (40)</span></div>
          <div class="key-item"><kbd>X</kbd><span>Soft (70)</span></div>
          <div class="key-item"><kbd>C</kbd><span>Medium (100)</span></div>
          <div class="key-item"><kbd>V</kbd><span>Accent (127)</span></div>
        </div>
      </div>
      
      <div class="key-section">
        <h3>🎛️ Filters (track/pad selected)</h3>
        <div class="key-list compact">
          <div class="key-item"><kbd>F1</kbd><span>LowPass 300Hz</span></div>
          <div class="key-item"><kbd>F2</kbd><span>HiPass 3kHz</span></div>
          <div class="key-item"><kbd>F3</kbd><span>BandPass 800Hz</span></div>
          <div class="key-item"><kbd>F4</kbd><span>Resonant 500Hz</span></div>
          <div class="key-item"><kbd>F5</kbd><span>LowShelf +10dB</span></div>
          <div class="key-item"><kbd>F6</kbd><span>HiShelf +10dB</span></div>
          <div class="key-item"><kbd>F7</kbd><span>Peaking 1.5kHz</span></div>
          <div class="key-item"><kbd>F8</kbd><span>Notch 800Hz</span></div>
          <div class="key-item"><kbd>F9</kbd><span>LowPass 150Hz</span></div>
          <div class="key-item"><kbd>F10</kbd><span>Clear Filter</span></div>
        </div>
        <div class="key-note">Shift+F1-F10 = Apply to Live Pad</div>
      </div>
      
      <div class="key-section">
        <h3>🧭 Navigation (step selected)</h3>
        <div class="key-list">
          <div class="key-item"><kbd>,</kbd><span>Prev Step</span></div>
          <div class="key-item"><kbd>.</kbd><span>Next Step</span></div>
          <div class="key-item"><kbd>Shift + -</kbd><span>Prev Track</span></div>
          <div class="key-item"><kbd>Shift + +</kbd><span>Next Track</span></div>
          <div class="key-item"><kbd>Esc</kbd><span>Deselect</span></div>
        </div>
      </div>
      
      <div class="key-section">
        <h3>❓ Help</h3>
        <div class="key-list">
          <div class="key-item"><kbd>H</kbd><span>Toggle This Panel</span></div>
        </div>
      </div>
      
      <div class="key-section unassigned">
        <h3>⚪ Unassigned Keys</h3>
        <div class="key-note">Available: 9, 0, U, I, O, P, D, F, G, J, K, L</div>
      </div>
    </div>
  `;
  
  document.body.appendChild(sidebar);
}

function showKeyboardHelp() {
  // Legacy function - now opens sidebar instead
  toggleKeyboardSidebar();
}

function closeKeyboardHelp() {
  const help = document.getElementById('keyboard-help');
  if (help) {
    help.remove();
  }
}

// ============= EXPORT GLOBAL FUNCTIONS =============

// Export functions to window for use in HTML and app.js
window.showKeyboardHelp = showKeyboardHelp;
window.closeKeyboardHelp = closeKeyboardHelp;
window.toggleKeyboardSidebar = toggleKeyboardSidebar;
window.toggleSidebarTransparency = toggleSidebarTransparency;
window.applyVelocityPreset = applyVelocityPreset;

// ============= TOAST NOTIFICATION SYSTEM =============

const TOAST_TYPES = {
  SUCCESS: 'success',
  INFO: 'info',
  WARNING: 'warning',
  ERROR: 'error',
  UNASSIGNED: 'unassigned'
};

let toastContainer = null;
let activeToasts = [];
const MAX_TOASTS = 5;

function initToastContainer() {
  if (!toastContainer) {
    toastContainer = document.createElement('div');
    toastContainer.id = 'toast-container';
    document.body.appendChild(toastContainer);
  }
}

function showToast(message, type = TOAST_TYPES.INFO, duration = 3000) {
  initToastContainer();
  
  // Create toast element
  const toast = document.createElement('div');
  toast.className = `toast toast-${type}`;
  
  // Icon based on type
  const icons = {
    success: '✓',
    info: 'ℹ',
    warning: '⚠',
    error: '✕',
    unassigned: '○'
  };
  
  toast.innerHTML = `
    <span class="toast-icon">${icons[type] || 'ℹ'}</span>
    <span class="toast-message">${message}</span>
  `;
  
  // Add to container
  toastContainer.appendChild(toast);
  activeToasts.push(toast);
  
  // Limit number of toasts
  if (activeToasts.length > MAX_TOASTS) {
    const oldToast = activeToasts.shift();
    oldToast.classList.add('toast-removing');
    setTimeout(() => oldToast.remove(), 300);
  }
  
  // Trigger animation
  setTimeout(() => toast.classList.add('toast-show'), 10);
  
  // Auto remove
  setTimeout(() => {
    toast.classList.remove('toast-show');
    toast.classList.add('toast-removing');
    setTimeout(() => {
      toast.remove();
      activeToasts = activeToasts.filter(t => t !== toast);
    }, 300);
  }, duration);
}

// Export toast function
window.showToast = showToast;
window.TOAST_TYPES = TOAST_TYPES;
