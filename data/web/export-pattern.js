// ============================================
// Pattern Export for RED808 Sequencer
// Export patterns as MIDI, WAV, JSON
// Render tracks to WAV using Web Audio API
// ============================================

(function() {
    'use strict';

    // === PAD → GM MIDI NOTE MAP ===
    const PAD_TO_MIDI_NOTE = {
        0: 36,  // BD - Bass Drum 1
        1: 38,  // SD - Acoustic Snare
        2: 42,  // CH - Closed Hi-Hat
        3: 46,  // OH - Open Hi-Hat
        4: 49,  // CY - Crash Cymbal 1
        5: 39,  // CP - Hand Clap
        6: 37,  // RS - Side Stick
        7: 56,  // CB - Cowbell
        8: 41,  // LT - Low Floor Tom
        9: 45,  // MT - Low Tom
        10: 48, // HT - Hi-Mid Tom
        11: 70, // MA - Maracas
        12: 75, // CL - Claves
        13: 62, // HC - Mute Hi Conga
        14: 64, // MC - Low Conga
        15: 61  // LC - Low Bongo
    };

    const PAD_NAMES = ['BD','SD','CH','OH','CY','CP','RS','CB','LT','MT','HT','MA','CL','HC','MC','LC'];

    const TRACK_COLORS = [
        '#ff0000', '#ffa500', '#ffff00', '#00ffff',
        '#e6194b', '#ff00ff', '#00ff00', '#f58231',
        '#911eb4', '#46f0f0', '#f032e6', '#bcf60c',
        '#38ceff', '#fabebe', '#008080', '#484dff'
    ];

    // =========================================================
    //  UTILITY: Read current pattern from DOM
    // =========================================================
    function readPatternFromDOM() {
        const pattern = [];
        const velocities = [];
        const noteLens = [];
        for (let t = 0; t < 16; t++) {
            pattern[t] = new Array(16).fill(false);
            velocities[t] = new Array(16).fill(0);
            noteLens[t] = new Array(16).fill(1);
        }

        document.querySelectorAll('.seq-step').forEach(el => {
            const track = parseInt(el.dataset.track);
            const step = parseInt(el.dataset.step);
            if (isNaN(track) || isNaN(step)) return;
            pattern[track][step] = el.classList.contains('active');
            velocities[track][step] = parseInt(el.dataset.velocity || '127');
            noteLens[track][step] = parseInt(el.dataset.notelen || '1');
        });

        return { pattern, velocities, noteLens };
    }

    function getBPM() {
        const slider = document.getElementById('tempoSlider');
        if (slider) return parseFloat(slider.value) || 120;
        const val = document.getElementById('tempoValue');
        if (val) return parseFloat(val.textContent) || 120;
        return 120;
    }

    function getPatternName() {
        const el = document.getElementById('currentPatternName');
        return el ? el.textContent.trim() : 'RED808_Pattern';
    }

    // =========================================================
    //  MIDI FILE GENERATOR (SMF Type 0, Channel 10)
    // =========================================================
    function generateMIDIFile(patternData, bpm, patternName) {
        const { pattern, velocities } = patternData;
        const ticksPerBeat = 480;
        const ticksPer16th = ticksPerBeat / 4; // 120 ticks per 16th note

        // Build events
        const events = [];

        // Tempo meta event (microseconds per quarter note)
        const usPerBeat = Math.round(60000000 / bpm);

        for (let t = 0; t < 16; t++) {
            const note = PAD_TO_MIDI_NOTE[t];
            for (let s = 0; s < 16; s++) {
                if (!pattern[t][s]) continue;
                const vel = velocities[t][s] || 127;
                const tickOn = s * ticksPer16th;
                const tickOff = tickOn + ticksPer16th - 1;

                events.push({ tick: tickOn, type: 'noteOn',  note, vel, channel: 9 });
                events.push({ tick: tickOff, type: 'noteOff', note, vel: 0, channel: 9 });
            }
        }

        // Sort by tick, noteOff before noteOn at same tick
        events.sort((a, b) => {
            if (a.tick !== b.tick) return a.tick - b.tick;
            if (a.type === 'noteOff' && b.type === 'noteOn') return -1;
            if (a.type === 'noteOn' && b.type === 'noteOff') return 1;
            return 0;
        });

        // === Build track chunk ===
        const trackBytes = [];

        function writeVarLen(value) {
            const bytes = [];
            bytes.push(value & 0x7F);
            value >>= 7;
            while (value > 0) {
                bytes.unshift((value & 0x7F) | 0x80);
                value >>= 7;
            }
            return bytes;
        }

        // Track Name meta event (delta=0)
        const nameBytes = new TextEncoder().encode(patternName);
        trackBytes.push(0x00); // delta time
        trackBytes.push(0xFF, 0x03); // meta: track name
        trackBytes.push(...writeVarLen(nameBytes.length));
        trackBytes.push(...nameBytes);

        // Tempo meta event (delta=0)
        trackBytes.push(0x00); // delta time
        trackBytes.push(0xFF, 0x51, 0x03); // meta: tempo, length=3
        trackBytes.push((usPerBeat >> 16) & 0xFF);
        trackBytes.push((usPerBeat >> 8) & 0xFF);
        trackBytes.push(usPerBeat & 0xFF);

        // Time signature: 4/4, 24 clocks/click, 8 32nd notes per beat
        trackBytes.push(0x00);
        trackBytes.push(0xFF, 0x58, 0x04, 0x04, 0x02, 0x18, 0x08);

        // Note events
        let lastTick = 0;
        for (const evt of events) {
            const delta = evt.tick - lastTick;
            trackBytes.push(...writeVarLen(delta));

            if (evt.type === 'noteOn') {
                trackBytes.push(0x99); // Note On, channel 10 (0x90 | 9)
                trackBytes.push(evt.note & 0x7F);
                trackBytes.push(evt.vel & 0x7F);
            } else {
                trackBytes.push(0x89); // Note Off, channel 10 (0x80 | 9)
                trackBytes.push(evt.note & 0x7F);
                trackBytes.push(0x00);
            }
            lastTick = evt.tick;
        }

        // End of Track meta event
        trackBytes.push(0x00, 0xFF, 0x2F, 0x00);

        // === Assemble MIDI file ===
        const trackLen = trackBytes.length;
        const headerLen = 14; // MThd (4) + size (4) + format (2) + tracks (2) + ticks (2)
        const totalLen = headerLen + 8 + trackLen; // +8 for MTrk header

        const buffer = new ArrayBuffer(totalLen);
        const view = new DataView(buffer);
        const bytes = new Uint8Array(buffer);
        let pos = 0;

        // MThd
        bytes.set([0x4D, 0x54, 0x68, 0x64], pos); pos += 4;
        view.setUint32(pos, 6); pos += 4;         // header data length
        view.setUint16(pos, 0); pos += 2;         // format 0
        view.setUint16(pos, 1); pos += 2;         // 1 track
        view.setUint16(pos, ticksPerBeat); pos += 2; // ticks per beat

        // MTrk
        bytes.set([0x4D, 0x54, 0x72, 0x6B], pos); pos += 4;
        view.setUint32(pos, trackLen); pos += 4;

        // Track data
        bytes.set(trackBytes, pos);

        return buffer;
    }

    // =========================================================
    //  MULTI-TRACK MIDI (SMF Type 1 – 1 track per instrument)
    // =========================================================
    function generateMIDIFileMultiTrack(patternData, bpm, patternName) {
        const { pattern, velocities } = patternData;
        const ticksPerBeat = 480;
        const ticksPer16th = ticksPerBeat / 4;
        const usPerBeat = Math.round(60000000 / bpm);

        function writeVarLen(value) {
            const bytes = [];
            bytes.push(value & 0x7F);
            value >>= 7;
            while (value > 0) {
                bytes.unshift((value & 0x7F) | 0x80);
                value >>= 7;
            }
            return bytes;
        }

        // Build tempo track (track 0)
        const tempoTrack = [];
        // Track name
        const name0 = new TextEncoder().encode(patternName);
        tempoTrack.push(0x00, 0xFF, 0x03, ...writeVarLen(name0.length), ...name0);
        // Tempo
        tempoTrack.push(0x00, 0xFF, 0x51, 0x03,
            (usPerBeat >> 16) & 0xFF, (usPerBeat >> 8) & 0xFF, usPerBeat & 0xFF);
        // Time sig
        tempoTrack.push(0x00, 0xFF, 0x58, 0x04, 0x04, 0x02, 0x18, 0x08);
        // End of track
        tempoTrack.push(0x00, 0xFF, 0x2F, 0x00);

        const allTracks = [tempoTrack];

        // Build one track per pad that has notes
        for (let t = 0; t < 16; t++) {
            const hasNotes = pattern[t].some(s => s);
            if (!hasNotes) continue;

            const trk = [];
            const note = PAD_TO_MIDI_NOTE[t];

            // Track name meta
            const tn = new TextEncoder().encode(PAD_NAMES[t]);
            trk.push(0x00, 0xFF, 0x03, ...writeVarLen(tn.length), ...tn);

            let lastTick = 0;
            for (let s = 0; s < 16; s++) {
                if (!pattern[t][s]) continue;
                const vel = velocities[t][s] || 127;
                const tickOn = s * ticksPer16th;
                const tickOff = tickOn + ticksPer16th - 1;

                // Note On
                trk.push(...writeVarLen(tickOn - lastTick));
                trk.push(0x99, note & 0x7F, vel & 0x7F);
                lastTick = tickOn;

                // Note Off
                trk.push(...writeVarLen(tickOff - lastTick));
                trk.push(0x89, note & 0x7F, 0x00);
                lastTick = tickOff;
            }

            trk.push(0x00, 0xFF, 0x2F, 0x00);
            allTracks.push(trk);
        }

        // Calculate total size
        let totalLen = 14; // MThd
        for (const trk of allTracks) totalLen += 8 + trk.length;

        const buffer = new ArrayBuffer(totalLen);
        const view = new DataView(buffer);
        const bytes = new Uint8Array(buffer);
        let pos = 0;

        // MThd
        bytes.set([0x4D, 0x54, 0x68, 0x64], pos); pos += 4;
        view.setUint32(pos, 6); pos += 4;
        view.setUint16(pos, 1); pos += 2;             // format 1
        view.setUint16(pos, allTracks.length); pos += 2;
        view.setUint16(pos, ticksPerBeat); pos += 2;

        for (const trk of allTracks) {
            bytes.set([0x4D, 0x54, 0x72, 0x6B], pos); pos += 4;
            view.setUint32(pos, trk.length); pos += 4;
            bytes.set(trk, pos); pos += trk.length;
        }

        return buffer;
    }

    // =========================================================
    //  WAV RENDERER  – Offline render using Web Audio API
    // =========================================================
    async function loadSampleBuffer(audioCtx, trackIndex) {
        // Try to fetch the current sample for this track from ESP32
        const padName = PAD_NAMES[trackIndex];
        // The samples are served from the ESP32 at /sample/<track>
        // But we can also try to load from the data directory
        const urls = [
            `/sample/${trackIndex}`,
            `/${padName}/sample.raw`
        ];

        for (const url of urls) {
            try {
                const resp = await fetch(url);
                if (!resp.ok) continue;
                const arrayBuffer = await resp.arrayBuffer();
                if (arrayBuffer.byteLength === 0) continue;

                // Try to decode as audio first
                try {
                    const decoded = await audioCtx.decodeAudioData(arrayBuffer.slice(0));
                    return decoded;
                } catch(e) {
                    // Raw 16-bit signed PCM at 44100Hz mono
                    const samples = new Int16Array(arrayBuffer);
                    const floatBuf = audioCtx.createBuffer(1, samples.length, 44100);
                    const chan = floatBuf.getChannelData(0);
                    for (let i = 0; i < samples.length; i++) {
                        chan[i] = samples[i] / 32768.0;
                    }
                    return floatBuf;
                }
            } catch(e) {
                continue;
            }
        }
        return null;
    }

    async function renderPatternToWAV(patternData, bpm, options = {}) {
        const { pattern, velocities } = patternData;
        const sampleRate = options.sampleRate || 44100;
        const bitDepth = options.bitDepth || 16;

        // Duration: 16 16th-notes = 4 beats
        const beatsPerSecond = bpm / 60;
        const secondsPer16th = 1 / (beatsPerSecond * 4);
        const totalDuration = 16 * secondsPer16th + 2; // +2s for tail/reverb

        const offlineCtx = new OfflineAudioContext(2, Math.ceil(totalDuration * sampleRate), sampleRate);

        // Load all needed samples
        const sampleBuffers = {};
        const tracksWithNotes = [];

        for (let t = 0; t < 16; t++) {
            if (pattern[t].some(s => s)) {
                tracksWithNotes.push(t);
            }
        }

        // Show progress
        updateExportProgress('Cargando samples...', 10);

        // Use a temporary AudioContext for decoding
        const tempCtx = new (window.AudioContext || window.webkitAudioContext)();
        for (let i = 0; i < tracksWithNotes.length; i++) {
            const t = tracksWithNotes[i];
            sampleBuffers[t] = await loadSampleBuffer(tempCtx, t);
            updateExportProgress(`Cargando ${PAD_NAMES[t]}...`, 10 + (i / tracksWithNotes.length) * 30);
        }
        tempCtx.close();

        updateExportProgress('Renderizando audio...', 45);

        // Schedule all notes
        for (const t of tracksWithNotes) {
            const buf = sampleBuffers[t];
            if (!buf) continue;

            for (let s = 0; s < 16; s++) {
                if (!pattern[t][s]) continue;

                const vel = (velocities[t][s] || 127) / 127;
                const time = s * secondsPer16th;

                const source = offlineCtx.createBufferSource();
                source.buffer = buf;

                const gain = offlineCtx.createGain();
                gain.gain.value = vel;

                source.connect(gain);
                gain.connect(offlineCtx.destination);
                source.start(time);
            }
        }

        updateExportProgress('Procesando...', 60);

        // Render
        const renderedBuffer = await offlineCtx.startRendering();

        updateExportProgress('Codificando WAV...', 80);

        // Encode to WAV
        const wavBuffer = encodeWAV(renderedBuffer, bitDepth);

        updateExportProgress('¡Listo!', 100);

        return wavBuffer;
    }

    // Render individual tracks as separate WAV files
    async function renderIndividualTracks(patternData, bpm, options = {}) {
        const { pattern, velocities } = patternData;
        const sampleRate = options.sampleRate || 44100;
        const bitDepth = options.bitDepth || 16;
        const selectedTracks = options.selectedTracks || null; // null = all with notes

        const beatsPerSecond = bpm / 60;
        const secondsPer16th = 1 / (beatsPerSecond * 4);
        const totalDuration = 16 * secondsPer16th + 2;

        const tempCtx = new (window.AudioContext || window.webkitAudioContext)();
        const renders = [];

        for (let t = 0; t < 16; t++) {
            // If specific tracks selected, only render those
            if (selectedTracks && !selectedTracks.includes(t)) continue;
            if (!pattern[t].some(s => s)) continue;

            updateExportProgress(`Renderizando ${PAD_NAMES[t]}...`, (t / 16) * 90);

            const buf = await loadSampleBuffer(tempCtx, t);
            if (!buf) continue;

            const offlineCtx = new OfflineAudioContext(2, Math.ceil(totalDuration * sampleRate), sampleRate);

            for (let s = 0; s < 16; s++) {
                if (!pattern[t][s]) continue;
                const vel = (velocities[t][s] || 127) / 127;
                const time = s * secondsPer16th;
                const source = offlineCtx.createBufferSource();
                source.buffer = buf;
                const gain = offlineCtx.createGain();
                gain.gain.value = vel;
                source.connect(gain);
                gain.connect(offlineCtx.destination);
                source.start(time);
            }

            const rendered = await offlineCtx.startRendering();
            renders.push({
                name: PAD_NAMES[t],
                track: t,
                wav: encodeWAV(rendered, bitDepth)
            });
        }

        tempCtx.close();
        updateExportProgress('¡Listo!', 100);
        return renders;
    }

    // =========================================================
    //  WAV ENCODER
    // =========================================================
    function encodeWAV(audioBuffer, bitDepth) {
        const numChannels = audioBuffer.numberOfChannels;
        const sampleRate = audioBuffer.sampleRate;
        const length = audioBuffer.length;
        const bytesPerSample = bitDepth / 8;
        const blockAlign = numChannels * bytesPerSample;
        const dataSize = length * blockAlign;
        const bufferSize = 44 + dataSize;

        const buffer = new ArrayBuffer(bufferSize);
        const view = new DataView(buffer);

        function writeString(offset, str) {
            for (let i = 0; i < str.length; i++) {
                view.setUint8(offset + i, str.charCodeAt(i));
            }
        }

        // RIFF header
        writeString(0, 'RIFF');
        view.setUint32(4, bufferSize - 8, true);
        writeString(8, 'WAVE');

        // fmt sub-chunk
        writeString(12, 'fmt ');
        view.setUint32(16, 16, true);           // subchunk size
        view.setUint16(20, 1, true);            // PCM format
        view.setUint16(22, numChannels, true);
        view.setUint32(24, sampleRate, true);
        view.setUint32(28, sampleRate * blockAlign, true);
        view.setUint16(32, blockAlign, true);
        view.setUint16(34, bitDepth, true);

        // data sub-chunk
        writeString(36, 'data');
        view.setUint32(40, dataSize, true);

        // Interleave channels and write samples
        const channels = [];
        for (let ch = 0; ch < numChannels; ch++) {
            channels.push(audioBuffer.getChannelData(ch));
        }

        let offset = 44;
        if (bitDepth === 16) {
            for (let i = 0; i < length; i++) {
                for (let ch = 0; ch < numChannels; ch++) {
                    let sample = channels[ch][i];
                    sample = Math.max(-1, Math.min(1, sample));
                    view.setInt16(offset, sample < 0 ? sample * 0x8000 : sample * 0x7FFF, true);
                    offset += 2;
                }
            }
        } else if (bitDepth === 24) {
            for (let i = 0; i < length; i++) {
                for (let ch = 0; ch < numChannels; ch++) {
                    let sample = channels[ch][i];
                    sample = Math.max(-1, Math.min(1, sample));
                    const val = sample < 0 ? sample * 0x800000 : sample * 0x7FFFFF;
                    const intVal = Math.round(val);
                    view.setUint8(offset, intVal & 0xFF);
                    view.setUint8(offset + 1, (intVal >> 8) & 0xFF);
                    view.setUint8(offset + 2, (intVal >> 16) & 0xFF);
                    offset += 3;
                }
            }
        } else { // 32-bit float
            for (let i = 0; i < length; i++) {
                for (let ch = 0; ch < numChannels; ch++) {
                    view.setFloat32(offset, channels[ch][i], true);
                    offset += 4;
                }
            }
        }

        return buffer;
    }

    // =========================================================
    //  JSON PATTERN EXPORT
    // =========================================================
    function generateJSON(patternData, bpm, patternName) {
        const { pattern, velocities, noteLens } = patternData;
        const data = {
            format: 'RED808',
            version: '1.0',
            name: patternName,
            bpm: bpm,
            stepsPerPattern: 16,
            tracks: []
        };

        for (let t = 0; t < 16; t++) {
            const hasNotes = pattern[t].some(s => s);
            if (!hasNotes) continue;
            data.tracks.push({
                index: t,
                name: PAD_NAMES[t],
                midiNote: PAD_TO_MIDI_NOTE[t],
                steps: pattern[t].map((active, s) => ({
                    step: s,
                    active: active,
                    velocity: velocities[t][s],
                    noteLen: noteLens[t][s]
                }))
            });
        }

        return JSON.stringify(data, null, 2);
    }

    // =========================================================
    //  DOWNLOAD HELPER
    // =========================================================
    function downloadBlob(blob, filename) {
        const url = URL.createObjectURL(blob);
        const a = document.createElement('a');
        a.href = url;
        a.download = filename;
        document.body.appendChild(a);
        a.click();
        setTimeout(() => {
            document.body.removeChild(a);
            URL.revokeObjectURL(url);
        }, 200);
    }

    function sanitizeFilename(name) {
        return name.replace(/[^a-zA-Z0-9_\- ]/g, '').replace(/\s+/g, '_') || 'RED808_Pattern';
    }

    // =========================================================
    //  PROGRESS UI
    // =========================================================
    function updateExportProgress(message, percent) {
        const bar = document.getElementById('exportProgressBar');
        const text = document.getElementById('exportProgressText');
        if (bar) bar.style.width = percent + '%';
        if (text) text.textContent = message;
    }

    // =========================================================
    //  EXPORT DIALOG
    // =========================================================
    function showExportDialog() {
        // Remove existing dialog if any
        const existing = document.getElementById('exportDialogOverlay');
        if (existing) existing.remove();

        const patternName = getPatternName();
        const bpm = getBPM();
        const patternData = readPatternFromDOM();

        const overlay = document.createElement('div');
        overlay.id = 'exportDialogOverlay';
        overlay.className = 'export-overlay';
        overlay.innerHTML = `
            <div class="export-dialog">
                <div class="export-dialog-header">
                    <span class="export-dialog-icon">💾</span>
                    <h2>EXPORT PATTERN</h2>
                    <button class="export-close-btn" onclick="closeExportDialog()">✕</button>
                </div>

                <div class="export-info-row">
                    <span class="export-info-label">Pattern:</span>
                    <span class="export-info-value" id="exportPatternName">${patternName}</span>
                    <span class="export-info-label">BPM:</span>
                    <span class="export-info-value" id="exportBPM">${bpm}</span>
                </div>

                <div class="export-sections">
                    <!-- MIDI Export -->
                    <div class="export-section">
                        <div class="export-section-header">
                            <span class="export-section-icon">🎹</span>
                            <div>
                                <h3>MIDI File (.mid)</h3>
                                <p>Archivo MIDI estándar compatible con todos los DAWs</p>
                            </div>
                        </div>
                        <div class="export-options-row">
                            <label class="export-radio">
                                <input type="radio" name="midiFormat" value="type0" checked>
                                <span>Type 0 (1 pista)</span>
                            </label>
                            <label class="export-radio">
                                <input type="radio" name="midiFormat" value="type1">
                                <span>Type 1 (multi-pista)</span>
                            </label>
                        </div>
                        <button class="export-action-btn export-midi-btn" onclick="doExportMIDI()">
                            <span>⬇</span> Descargar MIDI
                        </button>
                    </div>

                    <!-- WAV Export -->
                    <div class="export-section">
                        <div class="export-section-header">
                            <span class="export-section-icon">🔊</span>
                            <div>
                                <h3>Audio WAV (.wav)</h3>
                                <p>Renderizar el patrón como audio WAV</p>
                            </div>
                        </div>
                        <div class="export-options-row">
                            <div class="export-option-group">
                                <label>Calidad:</label>
                                <select id="exportWavBitDepth" class="export-select">
                                    <option value="16" selected>16-bit</option>
                                    <option value="24">24-bit</option>
                                    <option value="32">32-bit float</option>
                                </select>
                            </div>
                            <div class="export-option-group">
                                <label>Sample Rate:</label>
                                <select id="exportWavSampleRate" class="export-select">
                                    <option value="44100" selected>44.1 kHz</option>
                                    <option value="48000">48 kHz</option>
                                </select>
                            </div>
                        </div>
                        <div class="export-btn-row">
                            <button class="export-action-btn export-wav-btn" onclick="doExportWAV('mix')">
                                <span>🎚️</span> Mix Stereo
                            </button>
                            <button class="export-action-btn export-wav-btn export-wav-stems" onclick="doExportWAV('stems')">
                                <span>📂</span> Stems (todos)
                            </button>
                        </div>

                        <!-- Track selector for individual rendering -->
                        <div class="export-track-selector">
                            <div class="export-track-selector-header">
                                <span>🎯 Renderizar pistas individuales:</span>
                                <div class="export-track-select-btns">
                                    <button class="export-track-sel-all" onclick="exportSelectAllTracks(true)">ALL</button>
                                    <button class="export-track-sel-none" onclick="exportSelectAllTracks(false)">NONE</button>
                                </div>
                            </div>
                            <div class="export-track-grid" id="exportTrackGrid">
                                ${PAD_NAMES.map((name, i) => {
                                    const hasNotes = patternData.pattern[i].some(s => s);
                                    return `<label class="export-track-chip ${hasNotes ? 'has-notes' : 'no-notes'}" data-track="${i}">
                                        <input type="checkbox" value="${i}" ${hasNotes ? 'checked' : ''}>
                                        <span class="export-chip-name">${name}</span>
                                        <span class="export-chip-dot"></span>
                                    </label>`;
                                }).join('')}
                            </div>
                            <button class="export-action-btn export-wav-btn export-wav-selected" onclick="doExportWAV('selected')">
                                <span>🎯</span> Descargar selección
                            </button>
                            <button class="export-action-btn export-render-inline-btn" onclick="doRenderInline()">
                                <span>📊</span> Render en Sequencer
                            </button>
                            <button class="export-action-btn export-clear-render-btn" onclick="clearInlineWaveforms(); closeExportDialog()">
                                <span>🗑️</span> Limpiar renders
                            </button>
                        </div>
                    </div>

                    <!-- JSON Export -->
                    <div class="export-section">
                        <div class="export-section-header">
                            <span class="export-section-icon">📋</span>
                            <div>
                                <h3>Pattern Data (.json)</h3>
                                <p>Datos del patrón en formato JSON (velocidades, noteLen)</p>
                            </div>
                        </div>
                        <button class="export-action-btn export-json-btn" onclick="doExportJSON()">
                            <span>⬇</span> Descargar JSON
                        </button>
                    </div>
                </div>

                <!-- Progress bar -->
                <div class="export-progress" id="exportProgress" style="display:none">
                    <div class="export-progress-track">
                        <div class="export-progress-bar" id="exportProgressBar"></div>
                    </div>
                    <span class="export-progress-text" id="exportProgressText">Preparando...</span>
                </div>
            </div>
        `;

        document.body.appendChild(overlay);

        // Close on overlay click
        overlay.addEventListener('click', (e) => {
            if (e.target === overlay) closeExportDialog();
        });

        // Close on Escape
        const escHandler = (e) => {
            if (e.key === 'Escape') {
                closeExportDialog();
                document.removeEventListener('keydown', escHandler);
            }
        };
        document.addEventListener('keydown', escHandler);
    }

    function closeExportDialog() {
        const overlay = document.getElementById('exportDialogOverlay');
        if (overlay) {
            overlay.classList.add('closing');
            setTimeout(() => overlay.remove(), 200);
        }
    }

    // =========================================================
    //  EXPORT ACTIONS
    // =========================================================
    async function doExportMIDI() {
        const patternData = readPatternFromDOM();
        const bpm = getBPM();
        const name = getPatternName();
        const filename = sanitizeFilename(name);

        const formatRadio = document.querySelector('input[name="midiFormat"]:checked');
        const isType1 = formatRadio && formatRadio.value === 'type1';

        let buffer;
        if (isType1) {
            buffer = generateMIDIFileMultiTrack(patternData, bpm, name);
        } else {
            buffer = generateMIDIFile(patternData, bpm, name);
        }

        const blob = new Blob([buffer], { type: 'audio/midi' });
        downloadBlob(blob, `${filename}_${Math.round(bpm)}bpm.mid`);
    }

    async function doExportWAV(mode) {
        const patternData = readPatternFromDOM();
        const bpm = getBPM();
        const name = getPatternName();
        const filename = sanitizeFilename(name);

        const bitDepth = parseInt(document.getElementById('exportWavBitDepth').value);
        const sampleRate = parseInt(document.getElementById('exportWavSampleRate').value);

        // Show progress
        const progressEl = document.getElementById('exportProgress');
        if (progressEl) progressEl.style.display = 'flex';

        try {
            if (mode === 'stems' || mode === 'selected') {
                let selectedTracks = null;

                if (mode === 'selected') {
                    // Get selected tracks from checkboxes
                    const checkboxes = document.querySelectorAll('#exportTrackGrid input[type="checkbox"]:checked');
                    selectedTracks = Array.from(checkboxes).map(cb => parseInt(cb.value));
                    if (selectedTracks.length === 0) {
                        updateExportProgress('⚠ Selecciona al menos una pista', 0);
                        setTimeout(() => { if (progressEl) progressEl.style.display = 'none'; }, 2000);
                        return;
                    }
                }

                updateExportProgress('Renderizando stems...', 5);
                const tracks = await renderIndividualTracks(patternData, bpm, { sampleRate, bitDepth, selectedTracks });

                // If only 1 stem, download directly
                if (tracks.length <= 1 && tracks.length > 0) {
                    const blob = new Blob([tracks[0].wav], { type: 'audio/wav' });
                    downloadBlob(blob, `${filename}_${tracks[0].name}.wav`);
                } else {
                    // Download each stem
                    for (const trk of tracks) {
                        const blob = new Blob([trk.wav], { type: 'audio/wav' });
                        downloadBlob(blob, `${filename}_${trk.name}.wav`);
                        await new Promise(r => setTimeout(r, 300)); // Stagger downloads
                    }
                }
            } else {
                updateExportProgress('Renderizando mix...', 5);
                const wavBuffer = await renderPatternToWAV(patternData, bpm, { sampleRate, bitDepth });
                const blob = new Blob([wavBuffer], { type: 'audio/wav' });
                downloadBlob(blob, `${filename}_${Math.round(bpm)}bpm.wav`);
            }
        } catch(err) {
            console.error('[Export WAV]', err);
            updateExportProgress('Error: ' + err.message, 0);
            return;
        }

        setTimeout(() => {
            if (progressEl) progressEl.style.display = 'none';
        }, 2000);
    }

    function doExportJSON() {
        const patternData = readPatternFromDOM();
        const bpm = getBPM();
        const name = getPatternName();
        const filename = sanitizeFilename(name);

        const json = generateJSON(patternData, bpm, name);
        const blob = new Blob([json], { type: 'application/json' });
        downloadBlob(blob, `${filename}_${Math.round(bpm)}bpm.json`);
    }

    // =========================================================
    //  INLINE WAVEFORM RENDER (DAW-style in sequencer)
    // =========================================================

    // Store rendered waveform data per track
    const renderedWaveforms = {};

    function clearInlineWaveforms(trackIndex) {
        if (trackIndex !== undefined) {
            const existing = document.querySelector(`.seq-waveform-canvas[data-track="${trackIndex}"]`);
            if (existing) existing.remove();
            delete renderedWaveforms[trackIndex];
        } else {
            document.querySelectorAll('.seq-waveform-canvas').forEach(el => el.remove());
            Object.keys(renderedWaveforms).forEach(k => delete renderedWaveforms[k]);
        }
    }

    function drawInlineWaveform(trackIndex, audioBuffer) {
        const grid = document.getElementById('sequencerGrid');
        if (!grid) return;

        // Remove existing canvas for this track
        const existing = document.querySelector(`.seq-waveform-canvas[data-track="${trackIndex}"]`);
        if (existing) existing.remove();

        // Create canvas element
        const canvas = document.createElement('canvas');
        canvas.className = 'seq-waveform-canvas';
        canvas.dataset.track = trackIndex;

        // Place in grid: row = trackIndex + 1, spans columns 2-17 (the 16 step columns)
        const row = trackIndex + 1;
        canvas.style.gridRow = `${row}`;
        canvas.style.gridColumn = '2 / 18';

        grid.appendChild(canvas);

        // Size canvas to match actual rendered pixels
        const rect = canvas.getBoundingClientRect();
        const dpr = window.devicePixelRatio || 1;
        canvas.width = rect.width * dpr;
        canvas.height = rect.height * dpr;

        const ctx = canvas.getContext('2d');
        ctx.scale(dpr, dpr);

        const w = rect.width;
        const h = rect.height;
        const color = TRACK_COLORS[trackIndex] || '#00ff88';

        // Get audio data (mix down to mono for visualization)
        const numChannels = audioBuffer.numberOfChannels;
        const length = audioBuffer.length;
        const monoData = new Float32Array(length);
        for (let ch = 0; ch < numChannels; ch++) {
            const chanData = audioBuffer.getChannelData(ch);
            for (let i = 0; i < length; i++) {
                monoData[i] += chanData[i] / numChannels;
            }
        }

        // Downsample to pixel width
        const samplesPerPixel = Math.floor(length / w);
        const peaks = [];
        for (let x = 0; x < w; x++) {
            let min = 1, max = -1;
            const start = Math.floor(x * length / w);
            const end = Math.min(start + samplesPerPixel, length);
            for (let i = start; i < end; i++) {
                if (monoData[i] < min) min = monoData[i];
                if (monoData[i] > max) max = monoData[i];
            }
            peaks.push({ min, max });
        }

        // Store for redraw
        renderedWaveforms[trackIndex] = { peaks, color, audioBuffer };

        // Draw background
        ctx.clearRect(0, 0, w, h);

        // Draw beat grid lines
        ctx.strokeStyle = 'rgba(255,255,255,0.06)';
        ctx.lineWidth = 1;
        for (let i = 1; i < 16; i++) {
            const x = (i / 16) * w;
            ctx.beginPath();
            ctx.moveTo(x, 0);
            ctx.lineTo(x, h);
            ctx.stroke();
        }
        // Beat markers (every 4 steps)
        ctx.strokeStyle = 'rgba(255,255,255,0.12)';
        for (let i = 4; i < 16; i += 4) {
            const x = (i / 16) * w;
            ctx.beginPath();
            ctx.moveTo(x, 0);
            ctx.lineTo(x, h);
            ctx.stroke();
        }

        // Draw waveform
        const midY = h / 2;
        const amp = h * 0.42; // leave some padding

        // Filled area
        const r = parseInt(color.slice(1, 3), 16);
        const g = parseInt(color.slice(3, 5), 16);
        const b = parseInt(color.slice(5, 7), 16);

        const gradient = ctx.createLinearGradient(0, 0, 0, h);
        gradient.addColorStop(0, `rgba(${r},${g},${b},0.35)`);
        gradient.addColorStop(0.5, `rgba(${r},${g},${b},0.15)`);
        gradient.addColorStop(1, `rgba(${r},${g},${b},0.35)`);

        ctx.fillStyle = gradient;
        ctx.beginPath();
        ctx.moveTo(0, midY);
        for (let x = 0; x < peaks.length; x++) {
            ctx.lineTo(x, midY - peaks[x].max * amp);
        }
        for (let x = peaks.length - 1; x >= 0; x--) {
            ctx.lineTo(x, midY - peaks[x].min * amp);
        }
        ctx.closePath();
        ctx.fill();

        // Waveform outline
        ctx.strokeStyle = color;
        ctx.lineWidth = 1;
        ctx.globalAlpha = 0.9;

        // Top edge
        ctx.beginPath();
        for (let x = 0; x < peaks.length; x++) {
            const y = midY - peaks[x].max * amp;
            if (x === 0) ctx.moveTo(x, y);
            else ctx.lineTo(x, y);
        }
        ctx.stroke();

        // Bottom edge
        ctx.beginPath();
        for (let x = 0; x < peaks.length; x++) {
            const y = midY - peaks[x].min * amp;
            if (x === 0) ctx.moveTo(x, y);
            else ctx.lineTo(x, y);
        }
        ctx.stroke();

        // Center line
        ctx.globalAlpha = 0.25;
        ctx.strokeStyle = color;
        ctx.lineWidth = 0.5;
        ctx.beginPath();
        ctx.moveTo(0, midY);
        ctx.lineTo(w, midY);
        ctx.stroke();

        ctx.globalAlpha = 1;

        // Glow effect
        ctx.shadowColor = color;
        ctx.shadowBlur = 4;
        ctx.strokeStyle = `rgba(${r},${g},${b},0.4)`;
        ctx.lineWidth = 1.5;
        ctx.beginPath();
        for (let x = 0; x < peaks.length; x++) {
            const y = midY - ((peaks[x].max + peaks[x].min) / 2) * amp;
            if (x === 0) ctx.moveTo(x, y);
            else ctx.lineTo(x, y);
        }
        ctx.stroke();
        ctx.shadowBlur = 0;

        // Track name label on the canvas
        ctx.font = 'bold 9px "JetBrains Mono", monospace';
        ctx.fillStyle = `rgba(${r},${g},${b},0.6)`;
        ctx.textAlign = 'right';
        ctx.fillText(`${PAD_NAMES[trackIndex]} ▸ WAV`, w - 4, 10);
    }

    // Render selected tracks inline in the sequencer
    async function renderTracksInline(selectedTracks) {
        const patternData = readPatternFromDOM();
        const bpm = getBPM();
        const sampleRate = 44100;

        const beatsPerSecond = bpm / 60;
        const secondsPer16th = 1 / (beatsPerSecond * 4);
        const totalDuration = 16 * secondsPer16th + 2;

        const tempCtx = new (window.AudioContext || window.webkitAudioContext)();

        // Show progress
        const progressEl = document.getElementById('exportProgress');
        if (progressEl) progressEl.style.display = 'flex';

        for (let i = 0; i < selectedTracks.length; i++) {
            const t = selectedTracks[i];
            if (!patternData.pattern[t].some(s => s)) {
                clearInlineWaveforms(t);
                continue;
            }

            updateExportProgress(`Renderizando ${PAD_NAMES[t]}...`, (i / selectedTracks.length) * 90);

            const buf = await loadSampleBuffer(tempCtx, t);
            if (!buf) continue;

            const offlineCtx = new OfflineAudioContext(2, Math.ceil(totalDuration * sampleRate), sampleRate);

            for (let s = 0; s < 16; s++) {
                if (!patternData.pattern[t][s]) continue;
                const vel = (patternData.velocities[t][s] || 127) / 127;
                const time = s * secondsPer16th;
                const source = offlineCtx.createBufferSource();
                source.buffer = buf;
                const gain = offlineCtx.createGain();
                gain.gain.value = vel;
                source.connect(gain);
                gain.connect(offlineCtx.destination);
                source.start(time);
            }

            const rendered = await offlineCtx.startRendering();
            drawInlineWaveform(t, rendered);
        }

        tempCtx.close();
        updateExportProgress('¡Renderizado!', 100);

        setTimeout(() => {
            if (progressEl) progressEl.style.display = 'none';
        }, 1500);
    }

    async function doRenderInline() {
        const checkboxes = document.querySelectorAll('#exportTrackGrid input[type="checkbox"]:checked');
        const selectedTracks = Array.from(checkboxes).map(cb => parseInt(cb.value));
        if (selectedTracks.length === 0) {
            const progressEl = document.getElementById('exportProgress');
            if (progressEl) progressEl.style.display = 'flex';
            updateExportProgress('⚠ Selecciona al menos una pista', 0);
            setTimeout(() => { if (progressEl) progressEl.style.display = 'none'; }, 2000);
            return;
        }

        await renderTracksInline(selectedTracks);
        // Close dialog after rendering so user sees the waveforms
        setTimeout(() => closeExportDialog(), 500);
    }

    function toggleInlineWaveforms() {
        const canvases = document.querySelectorAll('.seq-waveform-canvas');
        if (canvases.length > 0) {
            const visible = canvases[0].style.display !== 'none';
            canvases.forEach(c => { c.style.display = visible ? 'none' : ''; });
        }
    }

    // =========================================================
    //  EXPOSE TO GLOBAL SCOPE
    // =========================================================
    // Select/deselect all tracks
    function exportSelectAllTracks(selectAll) {
        const checkboxes = document.querySelectorAll('#exportTrackGrid input[type="checkbox"]');
        checkboxes.forEach(cb => { cb.checked = selectAll; });
    }

    window.showExportDialog = showExportDialog;
    window.closeExportDialog = closeExportDialog;
    window.doExportMIDI = doExportMIDI;
    window.doExportWAV = doExportWAV;
    window.doExportJSON = doExportJSON;
    window.doRenderInline = doRenderInline;
    window.clearInlineWaveforms = clearInlineWaveforms;
    window.toggleInlineWaveforms = toggleInlineWaveforms;
    window.exportSelectAllTracks = exportSelectAllTracks;

})();
