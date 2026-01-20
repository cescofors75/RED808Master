# 📌 PINOUT FINAL - La Teva Configuració Exacta

## Hardware Real que Tens

✅ ESP32-S3 + Placa Expansió  
✅ Display ST7789 240x240  
✅ DAC PCM5102A  
✅ Botonera 4 botons  

---

## 🔌 Connexions Completes (12 pins)

### 1️⃣ Audio I2S - PCM5102A (3 pins)

```
ESP32-S3       PCM5102A
--------       --------
GPIO 42   →    BCK
GPIO 41   →    LRCK
GPIO 2    →    DIN
3.3V      →    VIN
GND       →    GND

Configuració PCM5102A (ponts/jumpers):
SCK  → GND
FMT  → GND
XMT  → 3.3V
```

---

### 2️⃣ Display SPI - ST7789 (5 pins + power)

```
ESP32-S3       ST7789
--------       ------
GPIO 10   →    CS
GPIO 11   →    DC
GPIO 12   →    RST
GPIO 13   →    MOSI (SDA)
GPIO 14   →    SCLK
            
3.3V      →    VCC
3.3V      →    BL (Backlight) ← Directe a 3.3V
GND       →    GND
```

**IMPORTANT**: El backlight (BL) va **directament a 3.3V**, no a GPIO.

---

### 3️⃣ Botonera - 4 Botons (4 pins)

```
ESP32-S3       Botó
--------       ----
GPIO 15   →    UP      → GND
GPIO 16   →    DOWN    → GND
GPIO 17   →    SELECT  → GND
GPIO 18   →    BACK    → GND
```

Cada botó:
- Un terminal al GPIO
- Altre terminal a GND
- Pull-up intern (3.3V en repòs)
- Premut = LOW (0V)

---

## 📊 Resum Visual

```
        ESP32-S3 DevKit
   ┌─────────────────────┐
   │                     │
   │  GPIO 2  → PCM5102A DIN
   │                     │
   │  GPIO 10 → ST7789 CS
   │  GPIO 11 → ST7789 DC
   │  GPIO 12 → ST7789 RST
   │  GPIO 13 → ST7789 MOSI
   │  GPIO 14 → ST7789 SCLK
   │                     │
   │  GPIO 15 → BTN UP
   │  GPIO 16 → BTN DOWN
   │  GPIO 17 → BTN SELECT
   │  GPIO 18 → BTN BACK
   │                     │
   │  GPIO 41 → PCM5102A LRCK
   │  GPIO 42 → PCM5102A BCK
   │                     │
   │  3.3V  → PCM5102A, ST7789
   │  GND   → Comú
   │                     │
   └─────────────────────┘
```

---

## 🎨 Codi de Colors Recomanat

```
🔴 Vermell  → 3.3V / VCC
⚫ Negre    → GND
🟡 Groc     → I2S (GPIO 2, 41, 42)
🟢 Verd     → SPI Display (GPIO 10-14)
🔵 Blau     → Botons (GPIO 15-18)
```

---

## 📐 Layout de Connexions

```
┌────────────────────────────────────────┐
│                                        │
│  [Display ST7789]                      │
│   240x240                              │
│                                        │
│   Connexions:                          │
│   • GPIO 10 → CS                       │
│   • GPIO 11 → DC                       │
│   • GPIO 12 → RST                      │
│   • GPIO 13 → MOSI                     │
│   • GPIO 14 → SCLK                     │
│   • 3.3V → VCC                         │
│   • 3.3V → BL (backlight)              │
│   • GND → GND                          │
│                                        │
├────────────────────────────────────────┤
│                                        │
│  [Botonera 4 Botons]                   │
│                                        │
│   [UP]     [DOWN]   [SELECT]   [BACK]  │
│   GPIO15   GPIO16   GPIO17     GPIO18  │
│     │        │         │          │    │
│     └────────┴─────────┴──────────┘    │
│                  │                     │
│                 GND                    │
│                                        │
├────────────────────────────────────────┤
│                                        │
│  [PCM5102A DAC]                        │
│                                        │
│   BCK  ← GPIO 42                       │
│   LRCK ← GPIO 41                       │
│   DIN  ← GPIO 2                        │
│   VIN  ← 3.3V                          │
│   GND  ← GND                           │
│                                        │
│   Config:                              │
│   SCK → GND                            │
│   FMT → GND                            │
│   XMT → 3.3V                           │
│                                        │
│   LOUT → Amplificador L                │
│   ROUT → Amplificador R                │
│                                        │
└────────────────────────────────────────┘
```

---

## ⚡ Alimentació

```
USB 5V (USB-C ESP32-S3)
  │
  └─→ ESP32-S3 Regulador Intern
         │
         ├─→ 3.3V → PCM5102A VIN
         ├─→ 3.3V → ST7789 VCC
         ├─→ 3.3V → ST7789 BL (backlight)
         ├─→ 3.3V → Pull-ups botons (intern)
         │
         └─→ GND → Comú a tots
```

**Consum:** ~300-350 mA @ 5V

---

## ✅ Checklist de Connexions

```
PCM5102A (Audio):
☐ GPIO 42 → BCK
☐ GPIO 41 → LRCK
☐ GPIO 2  → DIN
☐ 3.3V    → VIN
☐ GND     → GND
☐ SCK     → GND (jumper)
☐ FMT     → GND (jumper)
☐ XMT     → 3.3V (jumper)

ST7789 (Display):
☐ GPIO 10 → CS
☐ GPIO 11 → DC
☐ GPIO 12 → RST
☐ GPIO 13 → MOSI
☐ GPIO 14 → SCLK
☐ 3.3V    → VCC
☐ 3.3V    → BL (directe)
☐ GND     → GND

Botons:
☐ GPIO 15 → UP → GND
☐ GPIO 16 → DOWN → GND
☐ GPIO 17 → SELECT → GND
☐ GPIO 18 → BACK → GND

General:
☐ GND comú entre tots els components
☐ 3.3V estable
☐ No cortos VCC-GND
```

---

## 🧪 Test Ràpid

### Amb Multímetre (Power OFF):

```
1. Mode continuïtat
2. Test GND comú → Tots els GND connectats
3. Test NO curt VCC-GND → Infinit
```

### Amb Multímetre (Power ON):

```
1. Voltímetre DC
2. 3.3V al PCM5102A VIN → Ha de llegir 3.3V
3. 3.3V al ST7789 VCC → Ha de llegir 3.3V
4. GPIO 15 (sense prémer) → 3.3V (pull-up)
5. GPIO 15 (premut) → 0V
```

---

## 🎯 Pins Utilitzats vs Disponibles

### Utilitzats (12 pins):
- **GPIO 2**: PCM5102A DIN
- **GPIO 10-14**: ST7789 (5 pins)
- **GPIO 15-18**: Botons (4 pins)
- **GPIO 41-42**: PCM5102A LRCK, BCK

### Disponibles per Expansions:
- GPIO 1, 3, 4-9, 19-21, 35-40, 45-48
- **Ús potencial:**
  - Pads físics (si vols afegir-los)
  - MIDI In/Out
  - Encoders rotatoris
  - LEDs status
  - Sensors addicionals

---

## 🔧 Notes Importants

1. **Backlight Display**: Va **directament a 3.3V**
   - No cal PWM per controlar brightness
   - Sempre encès al màxim
   - Alternativa: Pots posar resistència si vols menys brillantor

2. **Botons**: Pull-up **intern** activat
   - No calen resistències externes
   - Configurats al codi

3. **PCM5102A Jumpers**: 
   - SCK, FMT a GND per mode I2S
   - XMT a 3.3V per unmute

4. **Voltatge**: Tot 3.3V lògic
   - **NO** connectar 5V a GPIOs!

---

## 🆘 Troubleshooting

| Símptoma | Revisa | Solució |
|----------|--------|---------|
| Display negre | 3.3V i GND | Verifica alimentació |
| Display blanc | GPIO 11 (DC) | Revisa connexió DC |
| No audio | GPIOs 2,41,42 | Verifica I2S |
| Botons no van | GPIO 15-18 | Test continuïtat a GND |
| ESP32 no arranca | Corto circuit | Desconnecta tot, prova sol |

---

## 📷 Referència Visual Ràpida

```
Tu Configuració:

Display: GPIO 10,11,12,13,14 (sense pin backlight)
Botons:  GPIO 15,16,17,18
Audio:   GPIO 2,41,42

Total: 12 pins GPIO + power
```

---

**Ara ja tens els pins correctes per la teva configuració!** 🎉

Compila amb aquests pins i hauria de funcionar perfectament! 🚀
