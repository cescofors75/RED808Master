# ⚡ Voltatges: 3.3V vs 5V - Guia Completa

## 🔌 Voltatges a l'ESP32-S3

### Entrada d'Alimentació
```
USB-C 5V ──→ ESP32-S3 Regulador ──→ 3.3V (Sistema)
```

---

## 📊 Què va a cada voltatge?

### ✅ 3.3V - Lògica i Components

**TOT el sistema ESP32-S3 funciona a 3.3V:**

| Component | Voltatge | Pin |
|-----------|----------|-----|
| **GPIOs** | 3.3V | Tots |
| **PCM5102A DAC** | 3.3V | VIN |
| **Display ST7789** | 3.3V | VCC, BL |
| **Botons** | 3.3V | Pull-ups |
| **SPI, I2S, I2C** | 3.3V | Senyals |
| **WiFi, Bluetooth** | 3.3V | Intern |

**⚠️ CRÍTIC: MAI connectis 5V a un GPIO!**

---

### ✅ 5V - Només Alimentació USB

**El 5V només s'usa per alimentar la placa:**

| Component | Voltatge | Font |
|-----------|----------|------|
| **USB Input** | 5V | USB-C |
| **LED RGB WS2812B** | 5V* | Intern placa |
| **Regulador 3.3V** | 5V → 3.3V | Intern |

*El WS2812B funciona amb 5V ideal, però també amb 3.3V

---

## 🎯 La Teva Configuració

```
USB 5V (Entrada)
  │
  ├─→ Regulador 3.3V ESP32-S3
  │     │
  │     ├─→ 3.3V → PCM5102A VIN
  │     ├─→ 3.3V → ST7789 VCC
  │     ├─→ 3.3V → ST7789 BL
  │     ├─→ 3.3V → GPIOs (lògica)
  │     └─→ 3.3V → Pull-ups botons
  │
  └─→ 5V → LED RGB WS2812B (intern)
```

**Resposta curta**: 
- ❌ **NO necessites connectar res a 5V**
- ✅ Només USB 5V per alimentar
- ✅ Tot el teu hardware va a 3.3V

---

## 🔍 Detall per Component

### PCM5102A DAC

```
✅ CORRECTE:
   3.3V → VIN
   GND  → GND

❌ INCORRECTE:
   5V → VIN (massa voltatge!)
```

**Voltatge recomanat**: 2.7V - 3.6V
**Voltatge màxim**: 4.0V
**Per tant**: 3.3V perfecte, 5V pot **cremar el DAC** ⚠️

---

### Display ST7789

```
✅ CORRECTE:
   3.3V → VCC
   3.3V → BL (backlight)

❌ INCORRECTE:
   5V → VCC (pot danyar el display)
```

**Voltatge recomanat**: 2.8V - 3.6V
**Per tant**: 3.3V perfecte, 5V pot danyar ⚠️

---

### LED RGB WS2812B

```
✅ IDEAL (intern placa):
   5V → VDD (des de USB intern)
   
✅ FUNCIONA:
   3.3V → VDD (menys brillantor)
```

**Voltatge recomanat**: 5V ± 0.5V
**Voltatge mínim**: 3.5V (funciona però menys brillant)
**Senyal de dades**: 3.3V (compatible amb ESP32)

**Important**: El LED RGB està **intern a la placa**, ja connectat a 5V des de l'USB. No cal tocar res!

---

## ⚠️ Perills del 5V

### ❌ NO facis això MAI:

```
GPIO ESP32 ←──✗──── 5V
(Cremarà el GPIO!)

PCM5102A VIN ←──✗──── 5V
(Pot cremar el DAC!)

ST7789 VCC ←──✗──── 5V
(Pot danyar el display!)
```

### ✅ Sempre això:

```
GPIO ESP32 ←──✓──── 3.3V

Components ←──✓──── 3.3V del regulador
```

---

## 🔧 Com Obtenir 3.3V?

### Opció 1: Pins de la Placa (Recomanat)

```
ESP32-S3 DevKit
  │
  ├─→ Pin 3.3V (regulat)
  └─→ Pin GND
```

Usa aquests pins per alimentar:
- PCM5102A
- ST7789
- Altres components

**Corrent màxim**: ~500mA (suficient per tot)

---

### Opció 2: Regulador Extern (si necessites més corrent)

```
5V USB ──→ [Regulador LM1117-3.3] ──→ 3.3V
            o AMS1117-3.3
```

**Només necessari** si:
- Afegeixis molts LEDs
- Servorators
- Motors
- Altres components de >500mA

**Per la teva configuració NO cal!**

---

## 📊 Consum Total del Teu Sistema

| Component | Voltatge | Corrent |
|-----------|----------|---------|
| ESP32-S3 (amb WiFi) | 3.3V | ~250 mA |
| PCM5102A | 3.3V | ~10 mA |
| ST7789 Display | 3.3V | ~50 mA |
| LED RGB | 5V | ~20 mA |
| **TOTAL** | | **~330 mA** |

**Font necessària**: USB 5V 500mA (qualsevol USB ho té)

---

## 🧪 Test de Voltatges

### Amb Multímetre (Power ON):

```
Test 1: USB Input
Vermell → USB 5V
Negre → GND
Lectura: 4.8V - 5.2V ✓

Test 2: Pin 3.3V ESP32
Vermell → Pin 3.3V
Negre → GND
Lectura: 3.2V - 3.4V ✓

Test 3: PCM5102A VIN
Vermell → VIN PCM5102A
Negre → GND
Lectura: ~3.3V ✓

Test 4: ST7789 VCC
Vermell → VCC Display
Negre → GND
Lectura: ~3.3V ✓
```

---

## 🎨 Esquema Visual Complet

```
        ┌─────────────────┐
        │   USB-C 5V      │
        │   (Font)        │
        └────────┬─────────┘
                 │
                 │ 5V
                 ▼
        ┌─────────────────┐
        │   ESP32-S3      │
        │                 │
        │   Regulador     │
        │   5V → 3.3V     │
        └────────┬─────────┘
                 │
                 │ 3.3V
                 │
      ┌──────────┼──────────┐
      │          │          │
      ▼          ▼          ▼
  PCM5102A   ST7789    GPIO Logic
   (3.3V)    (3.3V)     (3.3V)
      │          │          │
   Audio      Display   Botons
```

---

## ✅ Checklist Final

```
☐ USB 5V connectat (única font 5V)
☐ PCM5102A VIN connectat a 3.3V
☐ ST7789 VCC connectat a 3.3V
☐ ST7789 BL connectat a 3.3V
☐ Tots els GND comuns
☐ CAP GPIO connectat a 5V
☐ LED RGB funciona (intern, ja a 5V)
```

---

## 🆘 Troubleshooting Voltatges

| Problema | Possible Causa | Solució |
|----------|----------------|---------|
| ESP32 no arranca | Curt 5V-GND | Desconnecta tot, prova sol |
| Display no funciona | 5V al VCC | Verifica que sigui 3.3V |
| DAC no sona | 5V al VIN | Pot estar cremat, usa 3.3V |
| LED RGB massa fluix | Baix voltatge | Normal a 3.3V, funciona |
| GPIO cremat | 5V connectat | Mai connectar 5V a GPIO! |

---

## 💡 Consells Finals

1. **3.3V per TOT** (excepte USB input)
2. **Verifica voltatges** amb multímetre abans d'encendre
3. **5V només USB** - no tocar
4. **LED RGB** ja està configurat internament
5. **Mai 5V a GPIOs** - molt important!

---

**Resum Ultra-Simple:**

🔴 **5V**: Només entrada USB  
🟢 **3.3V**: Tot el teu hardware  
⚫ **GND**: Comú a tot  

Fàcil! 😊
