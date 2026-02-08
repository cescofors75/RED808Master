// ============================================
// MIDI File Import for RED808 Sequencer
// Parses Standard MIDI Files (.mid) and maps
// drum notes to the 16-pad sequencer grid
// ============================================

// GM Drum Map: MIDI note -> pad index (16 pads)
// Complete General MIDI Percussion Map (notes 27-87)
// Pad 0:  BD (Bass Drum)     - Notes: 35, 36
// Pad 1:  SD (Snare Drum)    - Notes: 38, 40
// Pad 2:  CH (Closed Hi-Hat) - Notes: 42, 44
// Pad 3:  OH (Open Hi-Hat)   - Notes: 46
// Pad 4:  CY (Crash/Ride)    - Notes: 49, 51, 52, 53, 55, 57, 59
// Pad 5:  CP (Hand Clap)     - Notes: 39
// Pad 6:  RS (Side Stick)    - Notes: 37
// Pad 7:  CB (Cowbell/Agogo) - Notes: 56, 67, 68
// Pad 8:  LT (Low Tom)       - Notes: 41, 43
// Pad 9:  MT (Mid Tom)       - Notes: 45, 47
// Pad 10: HT (High Tom)      - Notes: 48, 50
// Pad 11: MA (Maracas/Shaker)- Notes: 70, 54, 69, 82
// Pad 12: CL (Claves/Blocks) - Notes: 75, 76, 77, 80, 81
// Pad 13: HC (High Conga)    - Notes: 62, 63
// Pad 14: MC (Mid Conga)     - Notes: 64, 66
// Pad 15: LC (Low Conga/Bongo)- Notes: 61, 60, 65, 58

const GM_DRUM_TO_PAD = {
    // Kicks (Pad 0: BD)
    35: 0, 36: 0,
    // Snares (Pad 1: SD)
    38: 1, 40: 1,
    // Closed Hi-Hat (Pad 2: CH)
    42: 2, 44: 2,
    // Open Hi-Hat (Pad 3: OH)
    46: 3,
    // Cymbals - Crash, Ride, Splash, Chinese (Pad 4: CY)
    49: 4, 51: 4, 52: 4, 53: 4, 55: 4, 57: 4, 59: 4,
    // Clap (Pad 5: CP)
    39: 5,
    // Rim Shot / Side Stick (Pad 6: RS)
    37: 6,
    // Cowbell, Agogo bells (Pad 7: CB)
    56: 7, 67: 7, 68: 7,
    // Low Tom, Low Floor Tom (Pad 8: LT)
    41: 8, 43: 8,
    // Mid Tom, Low-Mid Tom (Pad 9: MT)
    45: 9, 47: 9,
    // High Tom, High Floor Tom (Pad 10: HT)
    48: 10, 50: 10,
    // Maracas, Tambourine, Cabasa, Shaker (Pad 11: MA)
    70: 11, 54: 11, 69: 11, 82: 11,
    // Claves, Wood Blocks, Triangles (Pad 12: CL)
    75: 12, 76: 12, 77: 12, 80: 12, 81: 12,
    // High Conga (Mute, Open), High Timbale (Pad 13: HC)
    62: 13, 63: 13, 66: 13,
    // Low Conga, Low Timbale (Pad 14: MC)
    64: 14,
    // Low Conga, Hi/Lo Bongo, Vibraslap (Pad 15: LC)
    61: 15, 60: 15, 65: 15, 58: 15,
    // Whistles → Maracas (shaker-like)
    71: 11, 72: 11,
    // Guiro → Claves
    73: 12, 74: 12,
    // Cuica → High Conga
    78: 13, 79: 13
};

const PAD_NAMES = ['BD', 'SD', 'CH', 'OH', 'CY', 'CP', 'RS', 'CB', 'LT', 'MT', 'HT', 'MA', 'CL', 'HC', 'MC', 'LC'];

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
    const pattern = Array.from({ length: 16 }, () => Array(16).fill(false));
    const velocities = Array.from({ length: 16 }, () => Array(16).fill(127));
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
let importAllBars = true; // Default: import all bars (song mode)
let lastMidiFileName = ''; // Store MIDI file name for progress display

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
                        <label>Modo importación:</label>
                        <div class="import-mode-toggle">
                            <button id="importModeAll" class="import-mode-btn active" onclick="setImportMode(true)">🎵 Canción completa</button>
                            <button id="importModeSingle" class="import-mode-btn" onclick="setImportMode(false)">1️⃣ Un compás</button>
                        </div>
                    </div>
                    <div class="import-option-row" id="barSelectorRow">
                        <label>Compás a previsualizar:</label>
                        <div class="bar-selector">
                            <button class="bar-nav-btn" onclick="changeImportBar(-1)">◀</button>
                            <span id="barDisplay">1 / 1</span>
                            <button class="bar-nav-btn" onclick="changeImportBar(1)">▶</button>
                        </div>
                    </div>
                    <div id="barMapRow" class="import-option-row" style="display:none">
                        <label>Compases con datos:</label>
                        <div id="barMap" class="bar-map"></div>
                    </div>
                    <div id="songInfoRow" class="import-option-row song-info-row">
                        <label>📋 Song Mode:</label>
                        <span id="songModeInfo" class="song-mode-info">Se importarán X compases → Patterns 1-X</span>
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
                    <button class="btn-import-confirm" id="btnImportConfirm" onclick="confirmMidiImport()">🎵 Importar Canción</button>
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
    importAllBars = true;
    // Resume background animations
    document.body.classList.remove('midi-dialog-open');
}

function setImportMode(allBars) {
    importAllBars = allBars;
    document.getElementById('importModeAll').classList.toggle('active', allBars);
    document.getElementById('importModeSingle').classList.toggle('active', !allBars);
    
    const songInfoRow = document.getElementById('songInfoRow');
    const btnConfirm = document.getElementById('btnImportConfirm');
    
    if (allBars) {
        songInfoRow.style.display = 'flex';
        btnConfirm.textContent = '🎵 Importar Canción';
        updateSongModeInfo();
    } else {
        songInfoRow.style.display = 'none';
        btnConfirm.textContent = '✅ Importar Compás';
    }
}

function updateSongModeInfo() {
    if (!parsedMidiData) return;
    const totalBars = getMidiTotalBars(parsedMidiData);
    const barsToImport = Math.min(totalBars, 16); // Max 16 patterns
    const infoEl = document.getElementById('songModeInfo');
    if (infoEl) {
        infoEl.textContent = `${barsToImport} compases → Patterns 1-${barsToImport}${totalBars > 16 ? ` (máx 16 de ${totalBars})` : ''}`;
    }
}

function handleMidiFile(file) {
    if (!file.name.match(/\.(mid|midi)$/i)) {
        alert('Por favor selecciona un archivo MIDI (.mid)');
        return;
    }

    lastMidiFileName = file.name;
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
        updateBarMap(midiData);
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

    // Set default import mode and show song info
    setImportMode(true);
    updateSongModeInfo();

    // Hide dropzone
    document.getElementById('midiDropzone').style.display = 'none';

    // Build bar map showing which bars have drum data
    updateBarMap(midiData);
}

// Show a visual map of which bars contain drum notes
function updateBarMap(midiData) {
    if (!midiData) return;
    const totalBars = getMidiTotalBars(midiData);
    const barMapEl = document.getElementById('barMap');
    const barMapRow = document.getElementById('barMapRow');
    if (!barMapEl || !barMapRow) return;

    const ticksPerBar = midiData.ticksPerBeat * 4;
    const channel = currentImportChannel;
    const drumMap = GM_DRUM_TO_PAD;

    // Count mappable notes per bar
    const barNoteCounts = new Array(totalBars).fill(0);
    for (const track of midiData.tracks) {
        for (const event of track) {
            if (event.type !== 'noteOn') continue;
            if (channel >= 0 && event.channel !== channel) continue;
            if (drumMap[event.note] === undefined) continue;
            const bar = Math.floor(event.tick / ticksPerBar);
            if (bar >= 0 && bar < totalBars) barNoteCounts[bar]++;
        }
    }

    const maxNotes = Math.max(...barNoteCounts, 1);
    const barsWithData = barNoteCounts.filter(n => n > 0).length;

    // Build compact bar map (clickable squares)
    let html = '';
    const maxDisplay = Math.min(totalBars, 32); // Show max 32 bars
    for (let b = 0; b < maxDisplay; b++) {
        const hasData = barNoteCounts[b] > 0;
        const isCurrent = b === currentImportBar;
        const intensity = hasData ? Math.max(0.3, barNoteCounts[b] / maxNotes) : 0;
        const cls = `bar-map-cell${hasData ? ' has-data' : ''}${isCurrent ? ' current' : ''}`;
        html += `<span class="${cls}" onclick="jumpToBar(${b})" title="Compás ${b + 1}: ${barNoteCounts[b]} notas" style="${hasData ? `opacity:${intensity}` : ''}">${b + 1}</span>`;
    }
    if (totalBars > 32) html += `<span class="bar-map-more">+${totalBars - 32}</span>`;
    html += `<div class="bar-map-summary">${barsWithData} de ${totalBars} compases con datos drum</div>`;

    barMapEl.innerHTML = html;
    barMapRow.style.display = 'flex';
}

// Jump to a specific bar from bar map click
function jumpToBar(bar) {
    currentImportBar = bar;
    const totalBars = getMidiTotalBars(parsedMidiData);
    document.getElementById('barDisplay').textContent = `${bar + 1} / ${totalBars}`;
    updateMidiPreview();
    updateBarMap(parsedMidiData);
}

function changeImportBar(delta) {
    if (!parsedMidiData) return;
    const totalBars = getMidiTotalBars(parsedMidiData);
    currentImportBar = Math.max(0, Math.min(totalBars - 1, currentImportBar + delta));
    document.getElementById('barDisplay').textContent = `${currentImportBar + 1} / ${totalBars}`;
    updateMidiPreview();
    updateBarMap(parsedMidiData);
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

    for (let track = 0; track < 16; track++) {
        const row = document.createElement('div');
        row.className = 'preview-row';

        const label = document.createElement('span');
        label.className = 'preview-label';
        label.textContent = PAD_NAMES[track];
        const previewColors = [
            '#ff0000', '#ffa500', '#ffff00', '#00ffff',
            '#e6194b', '#ff00ff', '#00ff00', '#f58231',
            '#911eb4', '#46f0f0', '#f032e6', '#bcf60c',
            '#38ceff', '#fabebe', '#008080', '#484dff'
        ];
        label.style.color = previewColors[track];
        row.appendChild(label);

        for (let step = 0; step < 16; step++) {
            const cell = document.createElement('span');
            cell.className = 'preview-step' + (result.pattern[track][step] ? ' active' : '');
            if (result.pattern[track][step]) {
                cell.style.backgroundColor = previewColors[track];
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

// ============= IMPORT PROGRESS BAR =============

function showImportProgress(fileName, totalBars) {
    // Remove existing progress overlay if any
    const existing = document.getElementById('midiProgressOverlay');
    if (existing) existing.remove();

    const overlay = document.createElement('div');
    overlay.id = 'midiProgressOverlay';
    overlay.className = 'midi-progress-overlay';
    overlay.innerHTML = `
        <div class="midi-progress-card">
            <div class="midi-progress-header">
                <span class="midi-progress-icon">🎵</span>
                <span class="midi-progress-title">Importando MIDI</span>
            </div>
            <div class="midi-progress-filename">${fileName}</div>
            <div class="midi-progress-bar-container">
                <div class="midi-progress-bar-fill" id="midiProgressFill"></div>
            </div>
            <div class="midi-progress-info">
                <span id="midiProgressPercent">0%</span>
                <span id="midiProgressBars">Compás 0 / ${totalBars}</span>
            </div>
            <div class="midi-progress-status" id="midiProgressStatus">Enviando patrones al secuenciador...</div>
        </div>
    `;
    document.body.appendChild(overlay);
    
    // Animate in
    requestAnimationFrame(() => overlay.classList.add('visible'));
}

function updateImportProgress(percent, currentBar, totalBars) {
    const fill = document.getElementById('midiProgressFill');
    const percentEl = document.getElementById('midiProgressPercent');
    const barsEl = document.getElementById('midiProgressBars');
    const statusEl = document.getElementById('midiProgressStatus');

    if (fill) fill.style.width = percent + '%';
    if (percentEl) percentEl.textContent = percent + '%';
    if (barsEl) barsEl.textContent = `Compás ${currentBar} / ${totalBars}`;
    
    if (statusEl) {
        if (percent < 30) {
            statusEl.textContent = 'Enviando patrones al secuenciador...';
        } else if (percent < 70) {
            statusEl.textContent = 'Cargando notas y velocidades...';
        } else if (percent < 95) {
            statusEl.textContent = 'Casi listo...';
        } else {
            statusEl.textContent = 'Finalizando...';
        }
    }
}

function completeImportProgress(fileName, totalBars, totalNotes) {
    const fill = document.getElementById('midiProgressFill');
    const percentEl = document.getElementById('midiProgressPercent');
    const barsEl = document.getElementById('midiProgressBars');
    const statusEl = document.getElementById('midiProgressStatus');
    const icon = document.querySelector('.midi-progress-icon');
    const title = document.querySelector('.midi-progress-title');
    const card = document.querySelector('.midi-progress-card');

    if (fill) {
        fill.style.width = '100%';
        fill.classList.add('complete');
    }
    if (percentEl) percentEl.textContent = '100%';
    if (barsEl) barsEl.textContent = `${totalBars} compases importados`;
    if (statusEl) statusEl.textContent = `✅ ${totalNotes} notas mapeadas correctamente`;
    if (icon) icon.textContent = '✅';
    if (title) title.textContent = 'Importación Completa';
    if (card) card.classList.add('complete');

    // Auto close after 2.5 seconds
    setTimeout(() => {
        const overlay = document.getElementById('midiProgressOverlay');
        if (overlay) {
            overlay.classList.remove('visible');
            setTimeout(() => overlay.remove(), 400);
        }
    }, 2500);
}

function confirmMidiImport() {
    if (!parsedMidiData) return;

    // Apply tempo if checkbox is checked
    const useTempo = document.getElementById('useTempoCheckbox').checked;
    if (useTempo && parsedMidiData.tempo > 0) {
        sendWebSocket({ cmd: 'tempo', value: parsedMidiData.tempo });
        const tempoSlider = document.getElementById('tempoSlider');
        if (tempoSlider) {
            tempoSlider.value = parsedMidiData.tempo;
            const tempoVal = document.getElementById('tempoValue');
            if (tempoVal) tempoVal.textContent = parsedMidiData.tempo;
            // Update BPM meter display
            if (typeof window.updateBpmMeter === 'function') {
                window.updateBpmMeter(parsedMidiData.tempo);
            }
        }
    }

    if (importAllBars) {
        // === SONG MODE: Import all bars into sequential patterns ===
        const totalBars = getMidiTotalBars(parsedMidiData);
        const barsToImport = Math.min(totalBars, 16); // Max 16 patterns
        let totalMapped = 0;
        const midiFileName = lastMidiFileName || 'MIDI';

        console.log(`[MIDI Import] Starting song import: ${barsToImport} bars`);

        // Show progress overlay
        showImportProgress(midiFileName, barsToImport);

        // Queue commands with small delays to avoid flooding WebSocket
        let cmdQueue = [];
        // Mark bar boundaries for progress tracking
        let barBoundaries = []; // index in cmdQueue where each bar starts

        // Clear all patterns that will be used
        for (let bar = 0; bar < barsToImport; bar++) {
            cmdQueue.push({ cmd: 'clearPattern', pattern: bar });
        }

        // Generate patterns for all bars
        for (let bar = 0; bar < barsToImport; bar++) {
            barBoundaries.push(cmdQueue.length);
            const result = midiToPattern(parsedMidiData, {
                bars: 1,
                startBar: bar,
                channel: currentImportChannel,
                quantize: true
            });

            totalMapped += result.mappedNotes;

            for (let track = 0; track < 16; track++) {
                for (let step = 0; step < 16; step++) {
                    if (result.pattern[track][step]) {
                        cmdQueue.push({
                            cmd: 'setStep',
                            pattern: bar,
                            track: track,
                            step: step,
                            active: true
                        });
                        if (result.velocities[track][step] !== 127) {
                            cmdQueue.push({
                                cmd: 'setStepVelocity',
                                pattern: bar,
                                track: track,
                                step: step,
                                velocity: result.velocities[track][step]
                            });
                        }
                    }
                }
            }
        }

        // Enable song mode
        cmdQueue.push({ cmd: 'setSongMode', enabled: true, length: barsToImport });
        // Select first pattern
        cmdQueue.push({ cmd: 'selectPattern', index: 0 });

        // Send commands in small batches with generous delays to avoid ESP32 crash
        const BATCH_SIZE = 8; // Smaller batches to prevent WebSocket buffer overflow
        let batchIndex = 0;

        function sendBatch() {
            const end = Math.min(batchIndex + BATCH_SIZE, cmdQueue.length);
            for (let i = batchIndex; i < end; i++) {
                sendWebSocket(cmdQueue[i]);
            }
            batchIndex = end;

            // Update progress bar
            const percent = Math.round((batchIndex / cmdQueue.length) * 100);
            // Find which bar we're at
            let currentBar = 0;
            for (let b = barBoundaries.length - 1; b >= 0; b--) {
                if (batchIndex >= barBoundaries[b]) { currentBar = b + 1; break; }
            }
            updateImportProgress(percent, currentBar, barsToImport);

            if (batchIndex < cmdQueue.length) {
                setTimeout(sendBatch, 120); // 120ms between batches (safer for ESP32)
            } else {
                // All sent - refresh pattern display after a delay
                setTimeout(() => {
                    sendWebSocket({ cmd: 'getPattern' });
                    // Notify app.js about song mode
                    if (typeof window.onSongModeActivated === 'function') {
                        window.onSongModeActivated(barsToImport, midiFileName);
                    }
                    completeImportProgress(midiFileName, barsToImport, totalMapped);
                }, 500);
                console.log(`[MIDI Import] Song imported: ${barsToImport} bars, ${totalMapped} total notes, ${cmdQueue.length} commands sent`);
            }
        }

        sendBatch();
    } else {
        // === SINGLE BAR MODE: Import one bar into current pattern ===
        const result = midiToPattern(parsedMidiData, {
            bars: 1,
            startBar: currentImportBar,
            channel: currentImportChannel,
            quantize: true
        });

        console.log(`[MIDI Import] Single bar: bar ${currentImportBar + 1}, ${result.totalNotes} notes found, ${result.mappedNotes} mapped`);

        if (result.mappedNotes === 0) {
            alert(`No hay notas mapeables en el compás ${currentImportBar + 1}. Usa las flechas ◀ ▶ para encontrar un compás con datos.`);
            return;
        }

        // Build command queue (same batching approach as song mode)
        let cmdQueue = [];
        cmdQueue.push({ cmd: 'clearPattern' });

        for (let track = 0; track < 16; track++) {
            for (let step = 0; step < 16; step++) {
                if (result.pattern[track][step]) {
                    cmdQueue.push({
                        cmd: 'setStep',
                        track: track,
                        step: step,
                        active: true
                    });
                    if (result.velocities[track][step] !== 127) {
                        cmdQueue.push({
                            cmd: 'setStepVelocity',
                            track: track,
                            step: step,
                            velocity: result.velocities[track][step]
                        });
                    }
                }
            }
        }

        // Send in batches with delays to avoid flooding ESP32
        const BATCH_SIZE = 8;
        let batchIndex = 0;

        function sendSingleBarBatch() {
            const end = Math.min(batchIndex + BATCH_SIZE, cmdQueue.length);
            for (let i = batchIndex; i < end; i++) {
                sendWebSocket(cmdQueue[i]);
            }
            batchIndex = end;

            if (batchIndex < cmdQueue.length) {
                setTimeout(sendSingleBarBatch, 100);
            } else {
                // All sent - refresh pattern display after delay
                setTimeout(() => {
                    sendWebSocket({ cmd: 'getPattern' });
                }, 400);
                console.log(`[MIDI Import] Single bar imported: bar ${currentImportBar + 1}, ${result.mappedNotes} notes, ${cmdQueue.length} commands sent`);
            }
        }

        // Close dialog FIRST (saves parsedMidiData reference before nulling)
        closeMidiImportDialog();
        sendSingleBarBatch();
    }
}

// Export functions
window.showMidiImportDialog = showMidiImportDialog;
window.closeMidiImportDialog = closeMidiImportDialog;
window.changeImportBar = changeImportBar;
window.confirmMidiImport = confirmMidiImport;
window.setImportMode = setImportMode;
window.updateMidiPreview = updateMidiPreview;
window.jumpToBar = jumpToBar;
