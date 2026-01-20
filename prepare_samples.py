#!/usr/bin/env python3
"""
prepare_samples.py
Script para copiar SOLO los samples esenciales TR-808 optimizados para ESP32-S3

Uso:
    python prepare_samples.py <directorio_808_samples> <data_folder>
    
Ejemplo:
    python prepare_samples.py "C:/TR-808" "./data"
"""

import os
import sys
import shutil
from pathlib import Path

# SOLO las variaciones esenciales para ahorrar espacio
ESSENTIAL_SAMPLES = {
    'BD': ['BD5050.WAV', 'BD7510.WAV', 'BD2525.WAV'],  # 3 kicks fundamentales
    'SD': ['SD0000.WAV', 'SD5000.WAV', 'SD0050.WAV'],  # 3 snares
    'CH': ['CH.WAV'],                                    # 1 closed hat
    'OH': ['OH00.WAV'],                                  # 1 open hat
    'CP': ['CP.WAV'],                                    # 1 clap
    'CB': ['CB.WAV'],                                    # 1 cowbell
    'CY': ['CY0000.WAV'],                               # 1 cymbal
    'RS': ['RS.WAV'],                                    # 1 rimshot
    'LT': ['LT00.WAV'],                                  # 1 low tom
    'MT': ['MT00.WAV'],                                  # 1 mid tom
    'HT': ['HT00.WAV'],                                  # 1 high tom
    'LC': ['LC00.WAV'],                                  # 1 low conga
    'MC': ['MC00.WAV'],                                  # 1 mid conga
    'HC': ['HC00.WAV'],                                  # 1 high conga
    'MA': ['MA.WAV'],                                    # 1 maracas
}

def copy_essential_samples(source_dir, data_dir):
    """Copia solo los samples esenciales a data/"""
    source_path = Path(source_dir)
    data_path = Path(data_dir)
    
    if not source_path.exists():
        print(f"❌ Error: Directorio fuente no existe: {source_path}")
        return False
    
    # Crear data/ si no existe
    data_path.mkdir(exist_ok=True)
    
    total_copied = 0
    total_size = 0
    
    print(f"📁 Copiando samples esenciales desde: {source_path}")
    print(f"📁 Destino: {data_path}")
    print(f"=" * 60)
    
    for folder, files in ESSENTIAL_SAMPLES.items():
        source_folder = source_path / folder
        dest_folder = data_path / folder
        
        if not source_folder.exists():
            print(f"⚠️  Carpeta no encontrada: {source_folder}")
            continue
        
        # Crear carpeta destino
        dest_folder.mkdir(exist_ok=True)
        
        for filename in files:
            source_file = source_folder / filename
            dest_file = dest_folder / filename
            
            if source_file.exists():
                try:
                    shutil.copy2(source_file, dest_file)
                    file_size = source_file.stat().st_size
                    total_size += file_size
                    total_copied += 1
                    print(f"✅ {folder}/{filename} ({file_size/1024:.1f} KB)")
                except Exception as e:
                    print(f"❌ Error copiando {filename}: {e}")
            else:
                print(f"⚠️  No encontrado: {source_file}")
    
    print(f"=" * 60)
    print(f"✅ Total copiado: {total_copied} archivos")
    print(f"📊 Tamaño total: {total_size/1024:.1f} KB ({total_size/1024/1024:.2f} MB)")
    
    return True

def main():
    if len(sys.argv) != 3:
        print("Uso: python prepare_samples.py <directorio_808> <data_folder>")
        print("Ejemplo: python prepare_samples.py C:/TR-808 ./data")
        sys.exit(1)
    
    source_dir = sys.argv[1]
    data_dir = sys.argv[2]
    
    print("🥁 ESP32-S3 Drum Machine - Sample Optimizer")
    print(f"🎵 Seleccionando solo {sum(len(v) for v in ESSENTIAL_SAMPLES.values())} samples esenciales")
    print()
    
    if copy_essential_samples(source_dir, data_dir):
        print("\n✅ Proceso completado!")
        print("📌 Siguiente paso: pio run --target uploadfs")
    else:
        print("\n❌ Error en el proceso")
        sys.exit(1)

if __name__ == "__main__":
    main()
        return json.loads(result.stdout)
    except:
        return None

def convert_sample(input_file, output_file, normalize=True):
    """
    Converteix un sample al format correcte
    
    Args:
        input_file: Path del fitxer d'entrada
        output_file: Path del fitxer de sortida
        normalize: Si True, normalitza el volum
    """
    cmd = [
        'ffmpeg',
        '-i', str(input_file),
        '-ar', str(TARGET_SAMPLE_RATE),
        '-ac', str(TARGET_CHANNELS),
        '-sample_fmt', TARGET_BIT_DEPTH,
        '-y'  # Sobreescriu si existeix
    ]
    
    # Afegir normalització si es demana
    if normalize:
        cmd.extend(['-af', 'loudnorm=I=-16:TP=-1.5:LRA=11'])
    
    cmd.append(str(output_file))
    
    try:
        subprocess.run(cmd, 
                      stdout=subprocess.PIPE, 
                      stderr=subprocess.PIPE,
                      check=True)
        return True
    except subprocess.CalledProcessError as e:
        print(f"Error convertint {input_file}: {e}")
        return False

def process_directory(input_dir, output_dir, normalize=True):
    """
    Processa tots els fitxers WAV d'un directori
    
    Args:
        input_dir: Directori amb samples originals
        output_dir: Directori de sortida
        normalize: Si True, normalitza el volum
    """
    input_path = Path(input_dir)
    output_path = Path(output_dir)
    
    # Crea directori de sortida si no existeix
    output_path.mkdir(parents=True, exist_ok=True)
    
    # Busca tots els fitxers WAV
    wav_files = list(input_path.glob('*.wav')) + list(input_path.glob('*.WAV'))
    
    if not wav_files:
        print(f"No s'han trobat fitxers WAV a {input_dir}")
        return
    
    print(f"Trobats {len(wav_files)} fitxers WAV")
    print(f"Convertint a: {TARGET_SAMPLE_RATE}Hz, {TARGET_CHANNELS} canal, 16-bit")
    if normalize:
        print("Normalització: Activada")
    print()
    
    converted = 0
    skipped = 0
    errors = 0
    
    for input_file in wav_files:
        output_file = output_path / input_file.name
        
        print(f"Processant: {input_file.name}... ", end='', flush=True)
        
        if convert_sample(input_file, output_file, normalize):
            # Comprova mida
            size = output_file.stat().st_size
            
            if size > MAX_FILE_SIZE:
                print(f"ALERTA! Massa gran ({size/1024:.1f}KB > 512KB)")
                skipped += 1
            else:
                print(f"OK ({size/1024:.1f}KB)")
                converted += 1
        else:
            print("ERROR")
            errors += 1
    
    print()
    print("=" * 50)
    print(f"Convertits: {converted}")
    print(f"Massa grans: {skipped}")
    print(f"Errors: {errors}")
    print("=" * 50)
    
    if skipped > 0:
        print("\nSuggestions per fitxers massa grans:")
        print("  - Redueix la durada del sample")
        print("  - Usa un sample rate més baix (22050Hz)")
        print("  - Comprova que no tingui silencis innecessaris")

def create_sample_pack(output_dir, pack_name="Default"):
    """
    Crea un pack de samples bàsic per provar
    Genera samples sintètics si ffmpeg ho suporta
    """
    output_path = Path(output_dir)
    output_path.mkdir(parents=True, exist_ok=True)
    
    print(f"Creant pack de samples '{pack_name}'...")
    
    # Definició de samples sintètics
    samples = {
        'kick.wav': {
            'freq': 60,
            'duration': 0.5,
            'envelope': 'exp'
        },
        'snare.wav': {
            'freq': 200,
            'duration': 0.3,
            'noise': True
        },
        'hihat.wav': {
            'freq': 8000,
            'duration': 0.1,
            'noise': True
        },
    }
    
    # Nota: Generar samples sintètics amb ffmpeg és complex
    # Millor descarregar un sample pack gratis o usar samples existents
    print("Per crear samples de prova, descarrega un sample pack gratis de:")
    print("  - https://samples.kb6.de/downloads.php")
    print("  - https://www.musicradar.com/news/tech/free-music-samples-royalty-free-loops-hits-and-multis-to-download")
    print("\nDesprés executa aquest script per convertir-los al format correcte")

def main():
    if len(sys.argv) < 3:
        print("Ús: python3 prepare_samples.py <dir_entrada> <dir_sortida> [--no-normalize]")
        print()
        print("Exemple:")
        print("  python3 prepare_samples.py ./raw_samples ./data/samples")
        print()
        print("Opcions:")
        print("  --no-normalize    No normalitza el volum")
        sys.exit(1)
    
    # Comprova ffmpeg
    if not check_ffmpeg():
        print("ERROR: ffmpeg no està instal·lat!")
        print()
        print("Instal·lació:")
        print("  macOS:   brew install ffmpeg")
        print("  Ubuntu:  sudo apt install ffmpeg")
        print("  Windows: Descarrega de https://ffmpeg.org/download.html")
        sys.exit(1)
    
    input_dir = sys.argv[1]
    output_dir = sys.argv[2]
    normalize = '--no-normalize' not in sys.argv
    
    # Comprova que el directori d'entrada existeix
    if not os.path.isdir(input_dir):
        print(f"ERROR: El directori {input_dir} no existeix!")
        sys.exit(1)
    
    # Processa samples
    process_directory(input_dir, output_dir, normalize)
    
    print()
    print("Següent pas:")
    print(f"  1. Copia els fitxers de {output_dir} a <sketch>/data/samples/")
    print("  2. A Arduino IDE: Tools -> ESP32 Sketch Data Upload")
    print("  3. Espera que es completi la pujada a SPIFFS")

if __name__ == "__main__":
    main()
