# Guia Ràpida - Organització de 3 Kits 808

## Estructura dels teus samples

Tens 3 directoris amb samples 808 (probablement ja en WAV 44kHz mono). Així és com organitzar-los:

### Pas 1: Organitza els directoris

Assumint que tens alguna cosa com:
```
raw_samples/
├── 808_kit_1/
│   ├── kick.wav
│   ├── snare.wav
│   ├── hihat.wav
│   └── ... (altres samples)
├── 808_kit_2/
│   └── ... (samples del segon kit)
└── 808_kit_3/
    └── ... (samples del tercer kit)
```

### Pas 2: Executar l'script d'organització

```bash
python3 organize_808_kits.py ./raw_samples ./data
```

Això crearà:
```
data/
├── kits/
│   ├── kit1.txt    # Metadata del primer kit
│   ├── kit2.txt    # Metadata del segon kit
│   └── kit3.txt    # Metadata del tercer kit
└── samples/
    ├── kit1_kick.wav
    ├── kit1_snare.wav
    ├── kit1_hihat.wav
    ├── kit2_kick.wav
    ├── kit2_snare.wav
    └── ... (tots els samples)
```

### Pas 3: Pujar a SPIFFS

1. **Copia** la carpeta `data/` al directori del teu sketch Arduino:
   ```
   DrumMachine_ESP32S3/
   ├── DrumMachine_ESP32S3.ino
   ├── AudioEngine.h
   ├── ...
   └── data/              ← Aquí
       ├── kits/
       └── samples/
   ```

2. **Obre Arduino IDE**

3. **Tools → ESP32 Sketch Data Upload**

4. **Espera** que es completi la pujada (pot trigar uns minuts)

### Pas 4: Compilar i pujar el codi

1. **Verifica** que tens totes les llibreries:
   - TFT_eSPI (configurat per ST7789)

2. **Selecciona** la placa:
   - Board: ESP32S3 Dev Module
   - PSRAM: OPI PSRAM
   - Partition Scheme: Default 4MB with spiffs

3. **Compila i puja**

## Ús de la Drum Machine amb 3 Kits

### Controls dels Botons

**A la pantalla principal:**
- **UP**: Kit anterior
- **DOWN**: Kit següent
- **SELECT**: Veure VU meter
- **BACK**: Configuració

**Al VU meter:**
- **BACK**: Tornar a pantalla principal

### Exemples de Noms de Samples Reconeguts

L'script reconeix automàticament aquests noms i els assigna als pads correctes:

| Noms de fitxer | Pad | Instrument |
|----------------|-----|------------|
| kick, bd, bassdrum | 0 | Kick Drum |
| snare, sd | 1 | Snare Drum |
| hihat, hh, closedhat, ch | 2 | Closed Hi-Hat |
| clap, cp, handclap | 3 | Clap |
| tom1, lowtom, lt | 4 | Low Tom |
| tom2, midtom, mt | 5 | Mid Tom |
| tom3, hightom, ht | 6 | High Tom |
| crash, cymbal, cy | 7 | Crash |
| ride, rd | 8 | Ride |
| openhat, oh, openhi | 9 | Open Hi-Hat |
| cowbell, cb | 10 | Cowbell |
| rimshot, rim, rs | 11 | Rimshot |
| claves, cl | 12 | Claves |
| maracas, ma | 13 | Maracas |
| shaker | 14 | Shaker |
| (altres percussion) | 15 | Percussion 6 |

**Nota**: Els noms són case-insensitive i ignoren guions baixos i guions.

## Verificació

### Després de pujar a SPIFFS

Obre el **Serial Monitor** (115200 baud) i veuràs:

```
=== ESP32-S3 Drum Machine ===
SPIFFS mounted
PSRAM found: 8388608 bytes
Free PSRAM: 8388608 bytes
Initializing Display...
Display initialized
Initializing Input...
Input manager initialized
Initializing Sample Manager...
PSRAM available: 8388608 bytes
Initializing Audio Engine...
I2S initialized successfully
Loading kits...
Initializing Kit Manager...
Found kit file: /kits/kit1.txt
Loaded kit 'kit1' with 8 samples
Found kit file: /kits/kit2.txt
Loaded kit 'kit2' with 8 samples
Found kit file: /kits/kit3.txt
Loaded kit 'kit3' with 8 samples
Found 3 kits
Loading kit 0: kit1
Sample loaded: kit1_kick.wav (45123 samples) -> Pad 1
Sample loaded: kit1_snare.wav (23456 samples) -> Pad 2
...
Kit loaded: 8/8 samples
System ready!
```

### A la pantalla

Veuràs:
- Títol: "DRUM MACHINE"
- Nom del kit: "Kit: kit1" (o el nom que tingui)
- Grid de 16 pads (4x4)
- Pads carregats en gris fosc
- Stats: Veus actives, CPU%, PSRAM

## Troubleshooting

### Els samples no es carreguen

1. **Verifica que els WAV són correctes:**
   ```bash
   ffprobe sample.wav
   ```
   Ha de mostrar:
   - Sample rate: 44100 Hz
   - Channels: 1 (mono)
   - Bits per sample: 16

2. **Converteix si cal:**
   ```bash
   ffmpeg -i input.wav -ar 44100 -ac 1 -sample_fmt s16 output.wav
   ```

### Els kits no apareixen

1. Verifica que la carpeta `data/` està al directori del sketch
2. Comprova que has fet "ESP32 Sketch Data Upload"
3. Revisa el Serial Monitor per errors de SPIFFS

### Sample massa gran (> 512KB)

Retalla'l o redueix la durada:
```bash
# Retalla primers 2 segons
ffmpeg -i input.wav -t 2 -ar 44100 -ac 1 output.wav
```

## Personalització

### Canviar noms dels kits

Edita els fitxers `data/kits/kitX.txt`:

```
# El Meu Kit Personalitzat
# Samples: 8

0 kit1_kick.wav
1 kit1_snare.wav
2 kit1_hihat.wav
...
```

La primera línia amb `#` serà el nom que es mostra.

### Afegir més kits

1. Crea nou directori amb samples
2. Executa l'script d'organització
3. Torna a pujar a SPIFFS

### Assignació manual de pads

Si l'script no reconeix algun sample, pots editar manualment els fitxers `.txt`:

```
# Kit Custom
5 meu_sample_especial.wav
12 efecte_sonor.wav
```

## Recursos

- **Free 808 Samples**: 
  - https://samples.kb6.de/downloads.php
  - https://99sounds.org/drum-samples/
  - https://reverb.com/news/free-sample-packs

- **Conversió WAV Online**:
  - https://online-audio-converter.com/
  - Format: WAV, 44100Hz, Mono, 16-bit

Bon sampling! 🥁
