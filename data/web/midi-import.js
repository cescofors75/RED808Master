// ============================================
// MIDI File Import for RED808 Sequencer
// Parses Standard MIDI Files (.mid) and maps
// drum notes to the 8-pad sequencer grid
// ============================================

// GM Drum Map: MIDI note -> pad index
// Pad 0: BD (Bass Drum)     - Notes: 35, 36
// Pad 1: SD (Snare Drum)    - Notes: 38, 40
// Pad 2: CH (Closed Hi-Hat) - Notes: 42, 44
// Pad 3: OH (Open Hi-Hat)   - Notes: 46
// Pad 4: CP (Hand Clap)     - Notes: 39
// Pad 5: RS (Side Stick)    - Notes: 37, 56 (Cowbell)
// Pad 6: CL (Claves)        - Notes: 75, 41, 43, 45, 47, 48, 50 (Toms)
// Pad 7: CY (Crash Cymbal)  - Notes: 49, 51, 52, 53, 55, 57, 59

const GM_DRUM_TO_PAD = {
    35: 0, 36: 0,               // Bass Drum
    38: 1, 40: 1,               // Snare
    42: 2, 44: 2,               // Closed Hi-Hat, Pedal Hi-Hat
    46: 3,                      // Open Hi-Hat
    39: 4,                      // Hand Clap
    37: 5, 56: 5,               // Side Stick, Cowbell
    75: 6, 41: 6, 43: 6, 45: 6, 47: 6, 48: 6, 50: 6, // Claves, Toms
    49: 7, 51: 7, 52: 7, 53: 7, 55: 7, 57: 7, 59: 7  // Cymbals
};

const PAD_NAMES = ['BD', 'SD', 'CH', 'OH', 'CP', 'RS', 'CL', 'CY'];

// MIDI File Parser
class MIDIFileParser {
    constructor(arrayBuffer) {
        this.data = new DataView(arrayBuffer);
        this.offset = 0;
        this.tracks = [];
        this.format = 0;
        this.numTracks = 0;
        this.ticksPerBeat = 480;
        this.tempo = 120; // Default BPM
    }

    parse() {
        this.parseHeader();
        console.log(`[MIDI Parser] Header: format=${this.format}, tracks=${this.numTracks}, ticksPerBeat=${this.ticksPerBeat}, fileSize=${this.data.byteLength}`);
        for (let i = 0; i < this.numTracks; i++) {
            this.parseTrack();
        }
        return {
            format: this.format,
            numTracks: this.numTracks,
            ticksPerBeat: this.ticksPerBeat,
            tempo: this.tempo,
            tracks: this.tracks
        };
    }

    readUint8() {
        if (this.offset >= this.data.byteLength) return 0;
        return this.data.getUint8(this.offset++);
    }

    readUint16() {
        if (this.offset + 1 >= this.data.byteLength) return 0;
        const val = this.data.getUint16(this.offset);
        this.offset += 2;
        return val;
    }

    readUint32() {
        if (this.offset + 3 >= this.data.byteLength) return 0;
        const val = this.data.getUint32(this.offset);
        this.offset += 4;
        return val;
    }

    readVarLen() {
        let value = 0;
        let byte;
        let safety = 0;
        do {
            if (this.offset >= this.data.byteLength) break;
            byte = this.readUint8();
            value = (value << 7) | (byte & 0x7F);
            if (++safety > 4) break; // VarLen max 4 bytes in MIDI
        } while (byte & 0x80);
        return value;
    }

    readString(length) {
        let str = '';
        for (let i = 0; i < length; i++) {
            str += String.fromCharCode(this.readUint8());
        }
        return str;
    }

    parseHeader() {
        const chunk = this.readString(4);
        if (chunk !== 'MThd') {
            throw new Error('Not a valid MIDI file (missing MThd header)');
        }
        const headerLen = this.readUint32();
        this.format = this.readUint16();
        this.numTracks = this.readUint16();
        const division = this.readUint16();

        if (division & 0x8000) {
            // SMPTE time - convert to approximate ticks per beat
            const fps = -(division >> 8);
            const tpf = division & 0xFF;
            this.ticksPerBeat = fps * tpf;
        } else {
            this.ticksPerBeat = division;
        }

        // Skip any extra header bytes
        if (headerLen > 6) {
            this.offset += headerLen - 6;
        }
    }

    parseTrack() {
        const chunk = this.readString(4);
        if (chunk !== 'MTrk') {
            throw new Error('Expected MTrk chunk, got: ' + chunk);
        }
        const trackLen = this.readUint32();
        const trackEnd = this.offset + trackLen;
        const events = [];
        let absoluteTick = 0;
        let runningStatus = 0;

        while (this.offset < trackEnd && this.offset < this.data.byteLength) {
            const deltaTime = this.readVarLen();
            absoluteTick += deltaTime;

            if (this.offset >= this.data.byteLength) break;
            let statusByte = this.data.getUint8(this.offset);

            // Meta event
            if (statusByte === 0xFF) {
                this.offset++;
                const metaType = this.readUint8();
                const metaLen = this.readVarLen();

                if (metaType === 0x51 && metaLen === 3) {
                    // Tempo change
                    const microsecondsPerBeat =
                        (this.readUint8() << 16) |
                        (this.readUint8() << 8) |
                        this.readUint8();
                    this.tempo = Math.round(60000000 / microsecondsPerBeat);
                } else {
                    this.offset += metaLen;
                }
                continue;
            }

            // SysEx event
            if (statusByte === 0xF0 || statusByte === 0xF7) {
                this.offset++;
                const sysexLen = this.readVarLen();
                this.offset += sysexLen;
                continue;
            }

            // Channel event
            if (statusByte & 0x80) {
                runningStatus = statusByte;
                this.offset++;
            } else {
                statusByte = runningStatus;
            }

            const type = statusByte & 0xF0;
            const channel = statusByte & 0x0F;

            let data1 = 0, data2 = 0;

            switch (type) {
                case 0x80: // Note Off
                case 0x90: // Note On
                case 0xA0: // Aftertouch
                case 0xB0: // Control Change
                case 0xE0: // Pitch Bend
                    data1 = this.readUint8();
                    data2 = this.readUint8();
                    break;
                case 0xC0: // Program Change
                case 0xD0: // Channel Pressure
                    data1 = this.readUint8();
                    break;
                default:
                    // Unknown - skip to end
                    this.offset = trackEnd;
                    continue;
            }

            if (type === 0x90 && data2 > 0) {
                events.push({
                    tick: absoluteTick,
                    type: 'noteOn',
                    channel: channel,
                    note: data1,
                    velocity: data2
                });
            } else if (type === 0x80 || (type === 0x90 && data2 === 0)) {
                events.push({
                    tick: absoluteTick,
                    type: 'noteOff',
                    channel: channel,
                    note: data1,
                    velocity: 0
                });
            }
        }

        // Ensure we're at the end of the track
        this.offset = trackEnd;
        this.tracks.push(events);

        // Debug logging
        const noteOnCount = events.filter(e => e.type === 'noteOn').length;
        const channels = [...new Set(events.filter(e => e.type === 'noteOn').map(e => e.channel))];
        console.log(`[MIDI Parser] Track parsed: ${events.length} events, ${noteOnCount} noteOn, channels: ${channels.map(c => c + 1).join(',')}`);
        if (noteOnCount > 0) {
            const firstNote = events.find(e => e.type === 'noteOn');
            const lastNote = [...events].reverse().find(e => e.type === 'noteOn');
            console.log(`[MIDI Parser] First noteOn: tick=${firstNote.tick}, ch=${firstNote.channel + 1}, note=${firstNote.note}`);
            console.log(`[MIDI Parser] Last noteOn: tick=${lastNote.tick}, ch=${lastNote.channel + 1}, note=${lastNote.note}`);
            // Log per-channel note counts
            const chCounts = {};
            events.filter(e => e.type === 'noteOn').forEach(e => { chCounts[e.channel] = (chCounts[e.channel] || 0) + 1; });
            console.log(`[MIDI Parser] Notes per channel:`, Object.entries(chCounts).map(([ch, n]) => `Ch${Number(ch) + 1}:${n}`).join(', '));
        }
    }
}

// Convert parsed MIDI to 16-step pattern for RED808
function midiToPattern(midiData, options = {}) {
    const {
        bars = 1,          // How many bars to import (1 bar = 16 steps)
        startBar = 0,      // Which bar to start from
        channel = -1,      // MIDI channel filter (-1 = all, 9 = GM drums)
        quantize = true,   // Quantize to grid
        drumMap = GM_DRUM_TO_PAD
    } = options;

    const ticksPerBeat = midiData.ticksPerBeat;
    const ticksPerStep = ticksPerBeat / 4; // 16th notes
    const ticksPerBar = ticksPerBeat * 4; // 4/4 time

    const startTick = startBar * ticksPerBar;
    const endTick = startTick + (bars * ticksPerBar);

    console.log(`[midiToPattern] ticksPerBeat=${ticksPerBeat}, ticksPerStep=${ticksPerStep}, ticksPerBar=${ticksPerBar}`);
    console.log(`[midiToPattern] startBar=${startBar}, channel=${channel}, startTick=${startTick}, endTick=${endTick}`);

    // Collect all note events
    let totalEventsScanned = 0;
    let channelFiltered = 0;
    let tickFiltered = 0;
    const allNotes = [];
    for (const track of midiData.tracks) {
        for (const event of track) {
            if (event.type !== 'noteOn') continue;
            totalEventsScanned++;
            if (channel >= 0 && event.channel !== channel) { channelFiltered++; continue; }
            if (event.tick < startTick || event.tick >= endTick) { tickFiltered++; continue; }
            allNotes.push(event);
        }
    }
    console.log(`[midiToPattern] Scanned ${totalEventsScanned} noteOns: ${channelFiltered} filtered by channel, ${tickFiltered} filtered by tick range, ${allNotes.length} passed`);

    // Create pattern grid [track][step]
    const pattern = Array.from({ length: 8 }, () => Array(16).fill(false));
    const velocities = Array.from({ length: 8 }, () => Array(16).fill(127));
    const unmappedNotes = new Set();
    let mappedCount = 0;

    for (const note of allNotes) {
        const pad = drumMap[note.note];
        if (pad === undefined) {
            unmappedNotes.add(note.note);
            continue;
        }

        // Calculate step position (relative to start)
        const relativeTick = note.tick - startTick;
        let step;
        if (quantize) {
            step = Math.round(relativeTick / ticksPerStep) % 16;
        } else {
            step = Math.floor(relativeTick / ticksPerStep) % 16;
        }

        if (step >= 0 && step < 16) {
            pattern[pad][step] = true;
            velocities[pad][step] = Math.min(note.velocity, 127);
            mappedCount++;
        }
    }

    return {
        pattern,
        velocities,
        tempo: midiData.tempo,
        mappedNotes: mappedCount,
        unmappedNotes: Array.from(unmappedNotes),
        totalNotes: allNotes.length,
        bars: bars
    };
}

// Get total bars in the MIDI file
function getMidiTotalBars(midiData) {
    let maxTick = 0;
    for (const track of midiData.tracks) {
        for (const event of track) {
            if (event.tick > maxTick) maxTick = event.tick;
        }
    }
    const ticksPerBar = midiData.ticksPerBeat * 4;
    return Math.max(1, Math.ceil(maxTick / ticksPerBar));
}

// Find the first bar that has notes for a given channel (or all channels if ch=-1)
function findFirstBarWithNotes(midiData, channel, drumMap) {
    const ticksPerBar = midiData.ticksPerBeat * 4;
    let minTick = Infinity;
    for (const track of midiData.tracks) {
        for (const event of track) {
            if (event.type !== 'noteOn') continue;
            if (channel >= 0 && event.channel !== channel) continue;
            // If using drum map, only count mappable notes
            if (drumMap && drumMap[event.note] === undefined) continue;
            if (event.tick < minTick) minTick = event.tick;
        }
    }
    if (minTick === Infinity) return 0;
    return Math.floor(minTick / ticksPerBar);
}

// Count notes per channel
function getChannelNoteCounts(midiData) {
    const counts = {};
    for (const track of midiData.tracks) {
        for (const event of track) {
            if (event.type !== 'noteOn') continue;
            counts[event.channel] = (counts[event.channel] || 0) + 1;
        }
    }
    return counts;
}

// Count how many notes are mappable to drum pads for a given channel
function countMappableNotes(midiData, channel, drumMap) {
    let total = 0;
    let mappable = 0;
    for (const track of midiData.tracks) {
        for (const event of track) {
            if (event.type !== 'noteOn') continue;
            if (channel >= 0 && event.channel !== channel) continue;
            total++;
            if (drumMap[event.note] !== undefined) mappable++;
        }
    }
    return { total, mappable };
}

// Get summary of notes in the MIDI file
function getMidiNoteSummary(midiData) {
    const noteCounts = {};
    const channelNotes = {};

    for (const track of midiData.tracks) {
        for (const event of track) {
            if (event.type !== 'noteOn') continue;

            const key = event.note;
            noteCounts[key] = (noteCounts[key] || 0) + 1;

            if (!channelNotes[event.channel]) {
                channelNotes[event.channel] = new Set();
            }
            channelNotes[event.channel].add(event.note);
        }
    }

    return { noteCounts, channelNotes };
}

// Note name helper
function midiNoteName(note) {
    const names = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'];
    const octave = Math.floor(note / 12) - 1;
    return names[note % 12] + octave;
}

// GM Drum instrument name
function gmDrumName(note) {
    const names = {
        35: 'Acoustic Bass Drum', 36: 'Bass Drum 1', 37: 'Side Stick', 38: 'Acoustic Snare',
        39: 'Hand Clap', 40: 'Electric Snare', 41: 'Low Floor Tom', 42: 'Closed Hi-Hat',
        43: 'High Floor Tom', 44: 'Pedal Hi-Hat', 45: 'Low Tom', 46: 'Open Hi-Hat',
        47: 'Low-Mid Tom', 48: 'Hi-Mid Tom', 49: 'Crash Cymbal 1', 50: 'High Tom',
        51: 'Ride Cymbal 1', 52: 'Chinese Cymbal', 53: 'Ride Bell', 54: 'Tambourine',
        55: 'Splash Cymbal', 56: 'Cowbell', 57: 'Crash Cymbal 2', 58: 'Vibraslap',
        59: 'Ride Cymbal 2', 60: 'Hi Bongo', 61: 'Low Bongo', 62: 'Mute Hi Conga',
        63: 'Open Hi Conga', 64: 'Low Conga', 65: 'High Timbale', 66: 'Low Timbale',
        67: 'High Agogo', 68: 'Low Agogo', 69: 'Cabasa', 70: 'Maracas',
        71: 'Short Whistle', 72: 'Long Whistle', 73: 'Short Guiro', 74: 'Long Guiro',
        75: 'Claves', 76: 'Hi Wood Block', 77: 'Low Wood Block',
        78: 'Mute Cuica', 79: 'Open Cuica', 80: 'Mute Triangle', 81: 'Open Triangle'
    };
    return names[note] || `Note ${note}`;
}

// ============================================
// Import UI Logic
// ============================================

let parsedMidiData = null;
let currentImportBar = 0;
let currentImportChannel = -1; // -1 = auto detect

function showMidiImportDialog() {
    // Remove existing dialog
    const existing = document.getElementById('midiImportDialog');
    if (existing) existing.remove();

    // Pause background animations to prevent flickering
    document.body.classList.add('midi-dialog-open');

    const dialog = document.createElement('div');
    dialog.id = 'midiImportDialog';
    dialog.className = 'midi-import-overlay';
    dialog.innerHTML = `
        <div class="midi-import-dialog">
            <div class="midi-import-header">
                <h3>🎵 Importar Archivo MIDI</h3>
                <button class="midi-import-close" onclick="closeMidiImportDialog()">&times;</button>
            </div>
            <div class="midi-import-body">
                <div class="midi-import-dropzone" id="midiDropzone">
                    <div class="dropzone-content">
                        <span class="dropzone-icon">📂</span>
                        <p>Arrastra un archivo .mid aquí</p>
                        <p class="dropzone-hint">o haz clic para seleccionar</p>
                        <input type="file" id="midiFileInput" accept=".mid,.midi" style="display:none">
                    </div>
                </div>
                <div id="midiFileInfo" class="midi-file-info" style="display:none"></div>
                <div id="midiImportOptions" class="midi-import-options" style="display:none">
                    <div class="import-option-row">
                        <label>Canal MIDI:</label>
                        <select id="midiChannelSelect">
                            <option value="-1">Auto (todos)</option>
                            <option value="9">Canal 10 (GM Drums)</option>
                        </select>
                    </div>
                    <div class="import-option-row">
                        <label>Compás a importar:</label>
                        <div class="bar-selector">
                            <button class="bar-nav-btn" onclick="changeImportBar(-1)">◀</button>
                            <span id="barDisplay">1 / 1</span>
                            <button class="bar-nav-btn" onclick="changeImportBar(1)">▶</button>
                        </div>
                    </div>
                    <div class="import-option-row">
                        <label>Usar tempo del MIDI:</label>
                        <label class="toggle-switch">
                            <input type="checkbox" id="useTempoCheckbox" checked>
                            <span class="toggle-slider"></span>
                        </label>
                        <span id="midiTempoDisplay" class="tempo-display">120 BPM</span>
                    </div>
                </div>
                <div id="midiPreview" class="midi-preview" style="display:none">
                    <h4>Vista previa del patrón:</h4>
                    <div class="preview-grid" id="previewGrid"></div>
                    <div class="preview-stats" id="previewStats"></div>
                </div>
                <div id="midiImportActions" class="midi-import-actions" style="display:none">
                    <button class="btn-import-cancel" onclick="closeMidiImportDialog()">Cancelar</button>
                    <button class="btn-import-confirm" onclick="confirmMidiImport()">✅ Importar al Sequencer</button>
                </div>
            </div>
        </div>
    `;

    document.body.appendChild(dialog);

    // Setup event listeners
    const dropzone = document.getElementById('midiDropzone');
    const fileInput = document.getElementById('midiFileInput');

    dropzone.addEventListener('click', (e) => {
        e.stopPropagation();
        fileInput.click();
    });
    dropzone.addEventListener('dragover', (e) => {
        e.preventDefault();
        e.stopPropagation();
        dropzone.classList.add('dragover');
    });
    dropzone.addEventListener('dragleave', (e) => {
        e.stopPropagation();
        dropzone.classList.remove('dragover');
    });
    dropzone.addEventListener('drop', (e) => {
        e.preventDefault();
        e.stopPropagation();
        dropzone.classList.remove('dragover');
        if (e.dataTransfer.files.length > 0) {
            handleMidiFile(e.dataTransfer.files[0]);
        }
    });
    fileInput.addEventListener('change', (e) => {
        e.stopPropagation();
        if (e.target.files.length > 0) {
            handleMidiFile(e.target.files[0]);
        }
    });
    // Prevent overlay click from bubbling
    dialog.addEventListener('click', (e) => {
        if (e.target === dialog) {
            // Click on overlay background - do nothing (don't close accidentally)
            e.stopPropagation();
        }
    });
}

function closeMidiImportDialog() {
    const dialog = document.getElementById('midiImportDialog');
    if (dialog) dialog.remove();
    parsedMidiData = null;
    // Resume background animations
    document.body.classList.remove('midi-dialog-open');
}

function handleMidiFile(file) {
    if (!file.name.match(/\.(mid|midi)$/i)) {
        alert('Por favor selecciona un archivo MIDI (.mid)');
        return;
    }

    const reader = new FileReader();
    reader.onload = (e) => {
        try {
            const parser = new MIDIFileParser(e.target.result);
            parsedMidiData = parser.parse();
            currentImportBar = 0;
            showMidiFileInfo(file, parsedMidiData);
            updateMidiPreview();
        } catch (err) {
            console.error('Error parsing MIDI:', err);
            alert('Error al leer el archivo MIDI: ' + err.message);
        }
    };
    reader.readAsArrayBuffer(file);
}

function showMidiFileInfo(file, midiData) {
    const totalBars = getMidiTotalBars(midiData);
    const summary = getMidiNoteSummary(midiData);
    const totalNotes = Object.values(summary.noteCounts).reduce((a, b) => a + b, 0);
    const channels = Object.keys(summary.channelNotes).map(Number);

    // Count notes per channel for smart selection
    const chNoteCounts = getChannelNoteCounts(midiData);

    // Auto-detect best drum channel: prefer ch10 (internal 9), else find channel with most mappable drum notes
    const hasCh10 = channels.includes(9);
    if (hasCh10) {
        currentImportChannel = 9;
    } else {
        // Try to find channel with most mappable drum notes
        let bestCh = -1;
        let bestMappable = 0;
        for (const ch of channels) {
            const stats = countMappableNotes(midiData, ch, GM_DRUM_TO_PAD);
            if (stats.mappable > bestMappable) {
                bestMappable = stats.mappable;
                bestCh = ch;
            }
        }
        currentImportChannel = bestCh >= 0 ? bestCh : -1;
    }

    // Auto-navigate to first bar with notes for the selected channel
    const firstBar = findFirstBarWithNotes(midiData, currentImportChannel, GM_DRUM_TO_PAD);
    currentImportBar = firstBar;
    console.log(`[MIDI Import] Auto-detected channel: ${currentImportChannel + 1}, first bar with drum notes: ${firstBar + 1}`);

    // Get mappable note stats for the selected channel
    const chStats = countMappableNotes(midiData, currentImportChannel, GM_DRUM_TO_PAD);
    console.log(`[MIDI Import] Channel ${currentImportChannel + 1}: ${chStats.total} total notes, ${chStats.mappable} mappable to drum pads`);

    // Update channel select with note counts
    const channelSelect = document.getElementById('midiChannelSelect');
    channelSelect.innerHTML = '<option value="-1">Todos los canales</option>';
    channels.forEach(ch => {
        const opt = document.createElement('option');
        opt.value = ch;
        const noteCount = chNoteCounts[ch] || 0;
        const mappable = countMappableNotes(midiData, ch, GM_DRUM_TO_PAD).mappable;
        opt.textContent = `Canal ${ch + 1}${ch === 9 ? ' (GM Drums)' : ''} — ${noteCount} notas${mappable > 0 ? ` (${mappable} drum)` : ''}`;
        opt.selected = ch === currentImportChannel;
        channelSelect.appendChild(opt);
    });
    channelSelect.addEventListener('change', (e) => {
        currentImportChannel = parseInt(e.target.value);
        // Auto-navigate to first bar with notes for new channel
        const fb = findFirstBarWithNotes(midiData, currentImportChannel, GM_DRUM_TO_PAD);
        currentImportBar = fb;
        document.getElementById('barDisplay').textContent = `${currentImportBar + 1} / ${totalBars}`;
        console.log(`[MIDI Import] Channel changed to ${currentImportChannel + 1}, jumping to bar ${fb + 1}`);
        updateMidiPreview();
    });

    // File info with per-channel detail
    const infoEl = document.getElementById('midiFileInfo');
    infoEl.innerHTML = `
        <div class="file-info-grid">
            <div class="info-item"><span class="info-icon">📄</span> <strong>${file.name}</strong> (${(file.size / 1024).toFixed(1)} KB)</div>
            <div class="info-item"><span class="info-icon">🎵</span> Formato: ${midiData.format} | ${midiData.numTracks} tracks</div>
            <div class="info-item"><span class="info-icon">📊</span> ${totalNotes} notas | ${totalBars} compases</div>
            <div class="info-item"><span class="info-icon">⏱️</span> Tempo: ${midiData.tempo} BPM</div>
            <div class="info-item"><span class="info-icon">🎹</span> Canales: ${channels.map(c => c + 1).join(', ')}${hasCh10 ? ' (Drums detectado)' : ''}</div>
            <div class="info-item"><span class="info-icon">🥁</span> Notas drum mapeables: ${chStats.mappable} de ${chStats.total} en canal ${currentImportChannel + 1}</div>
        </div>
    `;
    infoEl.style.display = 'block';

    // Tempo display
    document.getElementById('midiTempoDisplay').textContent = `${midiData.tempo} BPM`;

    // Bar display - show the auto-detected first bar
    document.getElementById('barDisplay').textContent = `${currentImportBar + 1} / ${totalBars}`;

    // Show options and actions
    document.getElementById('midiImportOptions').style.display = 'block';
    document.getElementById('midiImportActions').style.display = 'flex';

    // Hide dropzone
    document.getElementById('midiDropzone').style.display = 'none';
}

function changeImportBar(delta) {
    if (!parsedMidiData) return;
    const totalBars = getMidiTotalBars(parsedMidiData);
    currentImportBar = Math.max(0, Math.min(totalBars - 1, currentImportBar + delta));
    document.getElementById('barDisplay').textContent = `${currentImportBar + 1} / ${totalBars}`;
    updateMidiPreview();
}

function updateMidiPreview() {
    if (!parsedMidiData) return;

    const result = midiToPattern(parsedMidiData, {
        bars: 1,
        startBar: currentImportBar,
        channel: currentImportChannel,
        quantize: true
    });

    // Build preview grid
    const previewGrid = document.getElementById('previewGrid');
    previewGrid.innerHTML = '';

    for (let track = 0; track < 8; track++) {
        const row = document.createElement('div');
        row.className = 'preview-row';

        const label = document.createElement('span');
        label.className = 'preview-label';
        label.textContent = PAD_NAMES[track];
        label.style.color = [
            '#ff0000', '#ffa500', '#ffff00', '#00ffff',
            '#ff00ff', '#00ff00', '#38ceff', '#484dff'
        ][track];
        row.appendChild(label);

        for (let step = 0; step < 16; step++) {
            const cell = document.createElement('span');
            cell.className = 'preview-step' + (result.pattern[track][step] ? ' active' : '');
            if (result.pattern[track][step]) {
                cell.style.backgroundColor = [
                    '#ff0000', '#ffa500', '#ffff00', '#00ffff',
                    '#ff00ff', '#00ff00', '#38ceff', '#484dff'
                ][track];
            }
            row.appendChild(cell);
        }

        previewGrid.appendChild(row);
    }

    // Stats
    const statsEl = document.getElementById('previewStats');
    let statsHtml = '';
    if (result.totalNotes === 0) {
        statsHtml = `<div class="no-notes-warning">⚠️ No hay notas en este compás para el canal seleccionado. Usa ◀ ▶ para navegar.</div>`;
        // Show hint about where notes are
        if (parsedMidiData) {
            const firstBar = findFirstBarWithNotes(parsedMidiData, currentImportChannel, GM_DRUM_TO_PAD);
            const totalBars = getMidiTotalBars(parsedMidiData);
            if (firstBar > 0 && firstBar < totalBars) {
                statsHtml += `<div class="note-hint">💡 Primer compás con notas drum: ${firstBar + 1}. <a href="#" onclick="event.preventDefault(); currentImportBar=${firstBar}; document.getElementById('barDisplay').textContent='${firstBar + 1} / ${totalBars}'; updateMidiPreview();">Ir al compás ${firstBar + 1}</a></div>`;
            } else {
                // Maybe no mappable notes at all on this channel
                const chStats = countMappableNotes(parsedMidiData, currentImportChannel, GM_DRUM_TO_PAD);
                if (chStats.mappable === 0 && chStats.total > 0) {
                    statsHtml += `<div class="note-hint">⚠️ Este canal tiene ${chStats.total} notas pero ninguna coincide con el mapeo drum GM. Prueba otro canal.</div>`;
                } else if (chStats.total === 0) {
                    statsHtml += `<div class="note-hint">⚠️ No hay notas en el canal seleccionado. Prueba "Todos los canales".</div>`;
                }
            }
        }
    } else {
        statsHtml = `<div>✅ ${result.mappedNotes} notas mapeadas de ${result.totalNotes} totales</div>`;
        if (result.unmappedNotes.length > 0) {
            statsHtml += `<div class="unmapped-notes">⚠️ Notas sin mapear: ${
                result.unmappedNotes.map(n => `${midiNoteName(n)} (${n}) - ${gmDrumName(n)}`).join(', ')
            }</div>`;
        }
    }
    statsEl.innerHTML = statsHtml;
    console.log(`[MIDI Import] Bar ${currentImportBar + 1}: ${result.totalNotes} notes, ${result.mappedNotes} mapped, channel=${currentImportChannel}`);

    document.getElementById('midiPreview').style.display = 'block';
}

function confirmMidiImport() {
    if (!parsedMidiData) return;

    const result = midiToPattern(parsedMidiData, {
        bars: 1,
        startBar: currentImportBar,
        channel: currentImportChannel,
        quantize: true
    });

    // Apply tempo if checkbox is checked
    const useTempo = document.getElementById('useTempoCheckbox').checked;
    if (useTempo && result.tempo > 0) {
        sendWebSocket({ cmd: 'tempo', value: result.tempo });
        const tempoSlider = document.getElementById('tempoSlider');
        if (tempoSlider) {
            tempoSlider.value = result.tempo;
            const tempoVal = document.getElementById('tempoValue');
            if (tempoVal) tempoVal.textContent = result.tempo;
        }
    }

    // Clear current pattern first
    sendWebSocket({ cmd: 'clearPattern' });

    // Set each step
    for (let track = 0; track < 8; track++) {
        for (let step = 0; step < 16; step++) {
            if (result.pattern[track][step]) {
                sendWebSocket({
                    cmd: 'setStep',
                    track: track,
                    step: step,
                    active: true
                });
                // Set velocity
                if (result.velocities[track][step] !== 127) {
                    sendWebSocket({
                        cmd: 'setStepVelocity',
                        track: track,
                        step: step,
                        velocity: result.velocities[track][step]
                    });
                }
            }
        }
    }

    // Request pattern refresh
    setTimeout(() => {
        sendWebSocket({ cmd: 'getPattern' });
    }, 200);

    closeMidiImportDialog();

    // Show notification
    const barNum = currentImportBar + 1;
    console.log(`MIDI imported: bar ${barNum}, ${result.mappedNotes} notes mapped`);
}

// Export functions
window.showMidiImportDialog = showMidiImportDialog;
window.closeMidiImportDialog = closeMidiImportDialog;
window.changeImportBar = changeImportBar;
window.confirmMidiImport = confirmMidiImport;
