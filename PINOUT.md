# Listado Completo de Pines y Conexiones
## ESP32-S3 Drum Machine

> **NOTA IMPORTANT**: Els **pads físics són OPCIONALS**. Pots usar la drum machine només amb:
> - ✅ Pantalla ST7789
> - ✅ DAC PCM5102A  
> - ✅ 4 botons navegació
> - ✅ Control des de l'iPad (16 pads virtuals)
> 
> Consulta **SIMPLE_SETUP.md** per configuració sense pads físics.

---

## 📋 Tabla Resumen de Pines

| Función | GPIO | Dispositivo | Pin Destino | Notas |
|---------|------|-------------|-------------|-------|
| **I2S Audio (PCM5102A)** |
| Bit Clock | 42 | PCM5102A | BCK | Clock señal |
| Word Select | 41 | PCM5102A | LRCK (LCK) | Left/Right channel |
| Data Out | 2 | PCM5102A | DIN | Audio data |
| **Botones Físicos con LED RGB (WS2812B)** |
| BTN PLAY/PAUSE | 10 | Táctil+LED RGB | Terminal | Pull-up interno |
| BTN MULTIVIEW | 11 | Táctil+LED RGB | Terminal | Pull-up interno |
| BTN NEXT PATTERN | 12 | Táctil+LED RGB | Terminal | Pull-up interno |
| BTN PREV PATTERN | 13 | Táctil+LED RGB | Terminal | Pull-up interno |
| LED DATA (WS2812B) | 14 | LED RGB x4 | DIN cadena | Dato NeoPixel |
| (libre) | 15 | — | — | Disponible |
| **Botons Navegació** |
| Button Up | 16 | Botón | Terminal | Pull-up intern |
| Button Down | 17 | Botón | Terminal | Pull-up intern |
| Button Select | 18 | Botón | Terminal | Pull-up intern |
| Button Back | 19 | Botón | Terminal | Pull-up intern |
| **Pads/Triggers (OPCIONALS - Deixa sense connectar si no tens)** |
| Pad 1 (Kick) | 4 | Botón/Piezo | Terminal | Pull-up intern |
| Pad 2 (Snare) | 5 | Botón/Piezo | Terminal | Pull-up intern |
| Pad 3 (HiHat) | 6 | Botón/Piezo | Terminal | Pull-up intern |
| Pad 4 (Clap) | 7 | Botón/Piezo | Terminal | Pull-up intern |
| **Pads/Triggers (Fila 2)** |
| Pad 5 (Tom1) | 8 | Botón/Piezo | Terminal | Pull-up intern |
| Pad 6 (Tom2) | 9 | Botón/Piezo | Terminal | Pull-up intern |
| Pad 7 (Tom3) | 20 | Botón/Piezo | Terminal | Pull-up intern |
| Pad 8 (Crash) | 21 | Botón/Piezo | Terminal | Pull-up intern |
| **Pads/Triggers (Fila 3)** |
| Pad 9 (Ride) | 35 | Botón/Piezo | Terminal | Pull-up intern |
| Pad 10 (OpenHH) | 36 | Botón/Piezo | Terminal | Pull-up intern |
| Pad 11 (Cowbell) | 37 | Botón/Piezo | Terminal | Pull-up intern |
| Pad 12 (Rim) | 38 | Botón/Piezo | Terminal | Pull-up intern |
| **Pads/Triggers (Fila 4)** |
| Pad 13 (Claves) | 39 | Botón/Piezo | Terminal | Pull-up intern |
| Pad 14 (Maracas) | 40 | Botón/Piezo | Terminal | Pull-up intern |
| Pad 15 (Shaker) | 45 | Botón/Piezo | Terminal | Pull-up intern |
| Pad 16 (Perc) | 46 | Botón/Piezo | Terminal | Pull-up intern |
| **Alimentación** |
| 3.3V | 3.3V | PCM5102A, ST7789 | VCC/VIN | Regulado |
| 5V | 5V/USB | - | - | Entrada power |
| GND | GND | Todos | GND | Común |

---

## 🔌 Conexiones Detalladas por Componente

### 1️⃣ PCM5102A DAC (Salida Audio I2S)

```
ESP32-S3           PCM5102A
========           =========
GPIO 42    ───→    BCK (Bit Clock)
GPIO 41    ───→    LRCK (Word Select / LCK)
GPIO 2     ───→    DIN (Data In)
3.3V       ───→    VIN
GND        ───→    GND

Configuración PCM5102A (puentes):
SCK   ───→  GND    (Modo I2S)
FMT   ───→  GND    (Formato I2S)
XMT   ───→  3.3V   (Unmute - normal operation)

Salida Audio:
LOUT  ───→  Amplificador Left
ROUT  ───→  Amplificador Right
```

**Notas:**
- BCK: Clock de bits (2.8224 MHz @ 44.1kHz)
- LRCK: Clock de muestras (44.1 kHz)
- DIN: Datos de audio stereo 16-bit
- Si PCM5102A tiene pads de soldadura, soldarlos según especificación

---

### 2️⃣ Botones Físicos con LED RGB WS2812B

```
ESP32-S3           Botón+LED
========           =========
GPIO 10    ────    BTN PLAY/PAUSE  (switch → GND, INPUT_PULLUP)
GPIO 11    ────    BTN MULTIVIEW   (switch → GND, INPUT_PULLUP)
GPIO 12    ────    BTN NEXT PAT    (switch → GND, INPUT_PULLUP)
GPIO 13    ────    BTN PREV PAT    (switch → GND, INPUT_PULLUP)
GPIO 14    ───→    DIN (datos WS2812B — cadena de 4 LEDs)
3.3V       ───→    VCC botones / VDD LEDs
GND        ───→    GND común
```

**Comportamiento LED por defecto:**
- 🔴 Rojo — parado / inactivo (todos al arrancar)
- 🟢 Verde — PLAY/PAUSE cuando está reproduciendo
- 🩵 Cian — MULTIVIEW cuando está activo
- 🟠 Naranja — destello breve al pulsar NEXT o PREV

**Tipo de LED:** WS2812B (NeoPixel) — 4 píxeles en cadena.
Brillo predeterminado: 80/255 (≈31%) para no deslumbrar.

**Tipo de botón:** táctil normalmente abierto (NO).
Un terminal al GPIO, el otro a GND. El firmware usa INPUT_PULLUP.

---

### 3️⃣ Botons de Navegació

Todos los botones usan **pull-up interno**, activos en LOW.

```
        ┌─── Botón ───┐
ESP32   │             │
GPIO ───┤             ├─── GND
        └─────────────┘
```

**Conexiones:**
```
GPIO 16 ───[BTN UP]─── GND
GPIO 17 ───[BTN DOWN]─── GND
GPIO 18 ───[BTN SELECT]─── GND
GPIO 19 ───[BTN BACK]─── GND
```

**Función de cada botón:**
- **UP**: Kit anterior / Navegación arriba
- **DOWN**: Kit siguiente / Navegación abajo
- **SELECT**: VU Meter / Confirmar
- **BACK**: Configuración / Volver

---

### 4️⃣ Pads/Triggers (16 unidades)

#### Opción A: Botones Simples (Digital)

```
        ┌─── Botón ───┐
GPIO ───┤             ├─── GND
        └─────────────┘
```

**Grid 4x4:**
```
Fila 1:
GPIO 4  ───[PAD 1: Kick]─── GND
GPIO 5  ───[PAD 2: Snare]─── GND
GPIO 6  ───[PAD 3: HiHat]─── GND
GPIO 7  ───[PAD 4: Clap]─── GND

Fila 2:
GPIO 8  ───[PAD 5: Tom1]─── GND
GPIO 9  ───[PAD 6: Tom2]─── GND
GPIO 20 ───[PAD 7: Tom3]─── GND
GPIO 21 ───[PAD 8: Crash]─── GND

Fila 3:
GPIO 35 ───[PAD 9: Ride]─── GND
GPIO 36 ───[PAD 10: OpenHH]─── GND
GPIO 37 ───[PAD 11: Cowbell]─── GND
GPIO 38 ───[PAD 12: Rim]─── GND

Fila 4:
GPIO 39 ───[PAD 13: Claves]─── GND
GPIO 40 ───[PAD 14: Maracas]─── GND
GPIO 45 ───[PAD 15: Shaker]─── GND
GPIO 46 ───[PAD 16: Perc]─── GND
```

#### Opción B: Sensores Piezo (Velocity Sensing)

Para detectar intensidad del golpe:

```
        Piezo          Comparador      ADC (velocity)
         ┌─┐              LM393            GPIO
    ─────┤ ├─────┬────→ [Comparador] ──→ Digital (trigger)
         └─┘     │
                 └────→ [10kΩ] ──→ ADC_PIN (velocity)
                        │
                       GND
```

**Ejemplo con Piezo + ADC:**
```
Piezo (+) ──┬──→ Comparador ──→ GPIO (trigger)
            │
            └──→ 10kΩ ──→ ADC GPIO (velocity)
                  │
                 GND

Piezo (-) ──→ GND
```

Si usas piezos, necesitarás GPIOs ADC adicionales. Los ESP32-S3 ADC1 están en:
- GPIOs 1-10 (ADC1_CH0 a ADC1_CH9)

---

## 🔋 Alimentación

### Esquema de Power

```
USB 5V ──→ ESP32-S3 (Regulador 3.3V interno)
             │
             ├──→ 3.3V ──→ PCM5102A VIN
             │            ST7789 VCC
             │            (Pull-ups internos)
             │
             └──→ GND ──→ Común a todos
```

**Consumo Estimado:**
- ESP32-S3: ~200-300 mA (con WiFi)
- PCM5102A: ~10 mA
- ST7789: ~50 mA (con backlight)
- **Total**: ~300-400 mA @ 5V

**Fuente recomendada:**
- USB 5V 1A (suficiente)
- Powerbank (para portabilidad)

---

## 🛠️ Esquema de Conexión Completo

```
                    ┌─────────────────────────────┐
                    │                             │
                    │      ESP32-S3 DevKit        │
                    │      (con expansión)        │
                    │                             │
                    └─────────────────────────────┘
                              │
        ┌─────────────────────┼─────────────────────┐
        │                     │                     │
        │                     │                     │
   I2S Audio             SPI Display           GPIO Controls
        │                     │                     │
        ▼                     ▼                     ▼
  ┌──────────┐          ┌──────────┐          ┌──────────┐
  │PCM5102A  │          │ ST7789   │          │  Botons  │
  │   DAC    │          │ 240x240  │          │   (4)    │
  └──────────┘          └──────────┘          └──────────┘
        │                     │                     │
        │                     │                     ▼
        ▼                     │              ┌──────────┐
  [Amplificador]              │              │ 16 Pads  │
        │                     │              │ Triggers │
        ▼                     │              └──────────┘
   [Altavoces]                │
                              ▼
                        [Visualización]
```

---

## 📐 Layout Físico Recomendado

### Distribución de Componentes

```
Vista Superior:
┌─────────────────────────────────────────┐
│                                         │
│  [Display ST7789]    [ESP32-S3 + PCB]  │
│      240x240                            │
│                                         │
│  ┌────┬────┬────┬────┐                 │
│  │ 1  │ 2  │ 3  │ 4  │  ← Pads Grid   │
│  ├────┼────┼────┼────┤     4x4         │
│  │ 5  │ 6  │ 7  │ 8  │                 │
│  ├────┼────┼────┼────┤                 │
│  │ 9  │ 10 │ 11 │ 12 │                 │
│  ├────┼────┼────┼────┤                 │
│  │ 13 │ 14 │ 15 │ 16 │                 │
│  └────┴────┴────┴────┘                 │
│                                         │
│  [▲] [▼] [✓] [✗]  ← Botons navegació  │
│                                         │
│  [PCM5102A] → [Audio Out 3.5mm]        │
│                                         │
└─────────────────────────────────────────┘
```

---

## 🔧 Tips de Conexión

### 1. Orden de Montaje Recomendado

```
1. Soldar pins ESP32-S3 a placa expansión
2. Conectar Display ST7789 (SPI)
3. Conectar PCM5102A (I2S)
4. Añadir botons navegación
5. Añadir pads (comenzar con 4, luego expandir)
6. Verificar cada componente antes de añadir el siguiente
```

### 2. Gestión de Cables

**Longitud máxima recomendada:**
- I2S Audio: < 30 cm
- SPI Display: < 20 cm
- Botons: < 50 cm
- Pads: < 1 metro (con cable apantallado si >50cm)

**Separación:**
- Mantén cables de audio alejados de cables de power
- Usa cable trenzado para I2S si es largo
- GND común grueso (mínimo AWG22)

### 3. Protección

**Opcional pero recomendado:**
```
Cada pad digital:
GPIO ──[100Ω]── Pad ── GND

Backlight display:
GPIO 15 ──[220Ω]── LED Backlight

Audio out:
DAC ──[100μF]── Jack 3.5mm (DC blocking)
```

---

## 🧪 Test de Conexiones

### Checklist Antes de Programar

```
☐ 3.3V presente en PCM5102A y ST7789
☐ GND común en todos los componentes
☐ Display enciende backlight
☐ Botons leen HIGH en reposo (pull-up)
☐ Botons leen LOW al presionar
☐ No hay cortos entre VCC y GND
☐ ESP32-S3 detectado en Arduino IDE
```

### Test Simple con Multímetro

```
1. Power OFF
2. Modo continuidad
3. Verificar GND común
4. Verificar NO corto VCC-GND
5. Power ON
6. Voltímetro: Verificar 3.3V en componentes
```

---

## 🆘 Troubleshooting de Conexiones

| Problema | Posible Causa | Solución |
|----------|---------------|----------|
| Display negro | Sin VCC/GND | Verificar alimentación |
| Display blanco | Pin DC mal | Verificar GPIO 11 → DC |
| No audio | I2S mal | Verificar GPIOs 42,41,2 |
| Pads no responden | Pull-up | Añadir resistencia 10kΩ externa |
| ESP32 no arranca | Corto | Desconectar todo, probar solo ESP32 |
| WiFi no funciona | Antena | Verificar antena soldada/conectada |

---

## 📝 Notas Importantes

1. **PSRAM**: Asegúrate que tu ESP32-S3 tiene PSRAM soldada
2. **Antena WiFi**: Necesaria para web interface
3. **USB**: El ESP32-S3 DevKit suele tener USB-C nativo
4. **Placa expansión**: Facilita mucho las conexiones (la que tienes en las fotos)
5. **Nivel lógico**: Todo es 3.3V, compatible entre sí

---

## 🎨 Código de Colores Sugerido (Cableado)

```
Rojo    → 3.3V / 5V
Negro   → GND
Amarillo → I2S (BCK, LRCK, DIN)
Verde   → SPI (CS, DC, RST, MOSI, SCLK)
Azul    → Botons navegación
Blanco  → Pads/Triggers
```

---

## 📊 Resumen de Pines por Función

**Total GPIOs usados: 26**
- I2S: 3 pines (42, 41, 2)
- SPI: 6 pines (10-15)
- Botons: 4 pines (16-19)
- Pads: 16 pines (4-9, 20-21, 35-40, 45-46)

**GPIOs libres** para expansiones futuras:
- GPIOs 1, 3, 47, 48 (entre otros)
- Uso potencial: MIDI, LEDs, más botons, encoders

---

¿Necesitas un esquemático visual en Fritzing o KiCad? Puedo generarte uno detallado! 🔧
