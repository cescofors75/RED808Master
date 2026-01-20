# 🎛️ Conexión de Botonera de 4 Botones
## ESP32-S3 Drum Machine RED808

---

## 📋 Resumen de Funciones

| Botón | GPIO | Función | Descripción |
|-------|------|---------|-------------|
| **BTN 1** | **16** | **PLAY/STOP** | Inicia/Detiene el sequencer |
| **BTN 2** | **17** | **CLEAR** | Borra el pattern actual |
| **BTN 3** | **18** | **PATTERN** | Cambia al siguiente pattern |
| **BTN 4** | **19** | **KIT** | Cambia al siguiente kit |

---

## 🔌 Esquema de Conexión

### Conexión de cada botón:

```
┌────────────────────────────────────────┐
│          ESP32-S3-N16R8                │
│                                        │
│  GPIO 16 ────────┬─── BTN1 ─── GND   │  PLAY/STOP
│                  └─── 10kΩ ─── 3.3V  │  (Pull-up interno)
│                                        │
│  GPIO 17 ────────┬─── BTN2 ─── GND   │  CLEAR PATTERN
│                  └─── 10kΩ ─── 3.3V  │  (Pull-up interno)
│                                        │
│  GPIO 18 ────────┬─── BTN3 ─── GND   │  NEXT PATTERN
│                  └─── 10kΩ ─── 3.3V  │  (Pull-up interno)
│                                        │
│  GPIO 19 ────────┬─── BTN4 ─── GND   │  NEXT KIT
│                  └─── 10kΩ ─── 3.3V  │  (Pull-up interno)
│                                        │
└────────────────────────────────────────┘
```

### Diagrama físico:

```
        3.3V
         │
      [10kΩ] ← Opcional (hay pull-up interno)
         │
    ┌────┴────┐
    │         │
   BTN       GPIO
    │         │
   GND        └─── ESP32-S3
```

---

## 🛠️ Tipos de Botones Compatibles

### 1. **Pulsadores táctiles (Recomendado)**
- Tipo: Momentáneo normalmente abierto (NO)
- Tamaño: 6x6mm o 12x12mm
- 2 pines (un lado conectado)
- Ejemplo: Tact Switch

### 2. **Botones arcade**
- Tipo: Microswitch normalmente abierto
- Tamaño: 24mm, 30mm
- Conexión: COM y NO
- Ideal para drum machine estilo TR-808

### 3. **Botones panel**
- Tipo: Pulsador redondo
- Voltaje: 3.3V compatible
- Con o sin LED (LED independiente)

---

## ⚡ Instalación Paso a Paso

### Opción A: Con Pull-up Interno (Más Simple)

```
BOTÓN 1:
  Terminal 1 → GPIO 16
  Terminal 2 → GND

BOTÓN 2:
  Terminal 1 → GPIO 17
  Terminal 2 → GND

BOTÓN 3:
  Terminal 1 → GPIO 18
  Terminal 2 → GND

BOTÓN 4:
  Terminal 1 → GPIO 19
  Terminal 2 → GND
```

El código ya tiene configurado `INPUT_PULLUP`, por lo que NO necesitas resistencias externas.

### Opción B: Con Pull-up Externo (Más Robusto)

```
BOTÓN 1:
  Resistencia 10kΩ entre GPIO 16 y 3.3V
  Botón entre GPIO 16 y GND

BOTÓN 2:
  Resistencia 10kΩ entre GPIO 17 y 3.3V
  Botón entre GPIO 17 y GND

BOTÓN 3:
  Resistencia 10kΩ entre GPIO 18 y 3.3V
  Botón entre GPIO 18 y GND

BOTÓN 4:
  Resistencia 10kΩ entre GPIO 19 y 3.3V
  Botón entre GPIO 19 y GND
```

---

## 📦 Lista de Materiales

| Cantidad | Componente | Especificación | Notas |
|----------|------------|----------------|-------|
| 4 | Pulsadores | 6x6mm o 12x12mm | Momentáneos NO |
| 4 | Resistencias 10kΩ | 1/4W | Opcional (hay pull-up interno) |
| - | Cable | Dupont o similar | Para conexiones |

---

## 🎮 Funcionalidad Detallada

### BOTÓN 1 - PLAY/STOP (GPIO 16)
```cpp
- Presionar una vez: PLAY
- Presionar de nuevo: STOP
- Indicador: Mensaje en display
- Serial output: "[BTN1] PLAY" / "[BTN1] STOP"
```

### BOTÓN 2 - CLEAR PATTERN (GPIO 17)
```cpp
- Presionar: Borra todos los steps del pattern actual
- Útil para: Empezar de cero
- Serial output: "[BTN2] CLEAR PATTERN"
```

### BOTÓN 3 - NEXT PATTERN (GPIO 18)
```cpp
- Presionar: Cambia al siguiente pattern (cíclico)
- Patterns disponibles:
  0: Hip Hop (110 BPM)
  1: Techno (128 BPM)
  2: Drum & Bass (174 BPM)
  3: Breakbeat (140 BPM)
  4: House (125 BPM)
- Serial output: "[BTN3] Pattern -> N"
```

### BOTÓN 4 - NEXT KIT (GPIO 19)
```cpp
- Presionar: Cambia al siguiente kit (cíclico)
- Kits disponibles:
  0: TR-808 Classic (BD5050, SD0000)
  1: TR-808 Heavy (BD7510, SD5000)
  2: TR-808 Soft (BD2525, SD0050)
- Serial output: "[BTN4] Kit -> N: nombre"
```

---

## 🔧 Características Técnicas

- **Debounce**: 50ms implementado en software
- **Hold time**: 500ms para mantener presionado
- **Pull-up interno**: 45kΩ típico
- **Voltaje lógico**: 3.3V
- **Corriente por GPIO**: Máx 40mA (no necesaria para botones)
- **Frecuencia de lectura**: 100Hz (cada 10ms)

---

## 🧪 Prueba de Funcionamiento

### Paso 1: Subir el código
```bash
pio run --target upload
```

### Paso 2: Abrir Serial Monitor
```bash
pio device monitor
```

### Paso 3: Probar cada botón

Deberías ver:
```
[BTN1] PLAY          ← Al presionar BTN1
[BTN1] STOP          ← Al presionar BTN1 de nuevo
[BTN2] CLEAR PATTERN ← Al presionar BTN2
[BTN3] Pattern -> 1  ← Al presionar BTN3
[BTN4] Kit -> 1: TR-808 Heavy ← Al presionar BTN4
```

---

## 🎨 Ideas de Mejora

### Panel físico estilo TR-808:
```
┌─────────────────────────────────┐
│                                 │
│  [●]      [●]      [●]      [●] │
│  PLAY    CLEAR   PATTERN   KIT  │
│                                 │
└─────────────────────────────────┘
```

### Con LEDs indicadores:
- LED rojo parpadeante: PLAY activo
- LED verde: Pattern activo
- LED azul: Kit seleccionado

### Futuras funciones:
- **Hold BTN2**: Clear ALL patterns
- **Hold BTN3**: Guardar pattern en SD
- **Hold BTN4**: Recargar kits desde LittleFS
- **BTN1 + BTN3**: Tempo tap

---

## ⚠️ Notas Importantes

1. **NO conectar botones a pines de FLASH/BOOT**:
   - Evitar GPIO 0, 45, 46 durante boot
   - Los pines 16-19 son seguros

2. **Protección ESD**:
   - Usa botones con montaje PCB
   - Considera capacitores 100nF cerca de cada botón

3. **Cables largos**:
   - Si cables > 30cm, usa resistencias pull-up externas
   - Considera cable apantallado

4. **Interferencias**:
   - Mantén cables lejos de I2S y SPI
   - Ruteado limpio en PCB

---

## 📞 Troubleshooting

### Problema: Botones no responden
- ✅ Verifica conexiones (multímetro)
- ✅ Comprueba que botones sean NO (normalmente abiertos)
- ✅ Revisa Serial Monitor para debug

### Problema: Rebotes (múltiples lecturas)
- ✅ Debounce está implementado (50ms)
- ✅ Si persiste, aumenta `debounceTime` en código

### Problema: Lectura invertida
- ✅ Verifica que `INPUT_PULLUP` esté configurado
- ✅ Comprueba lógica `digitalRead() == LOW`

---

**¡Ahora tienes control físico total de tu RED808!** 🥁🔴
