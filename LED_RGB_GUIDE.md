# LED RGB de la Placa d'Expansió ESP32-S3

## 🎨 Què és el LED RGB?

La majoria de plaques d'expansió ESP32-S3 tenen un **LED RGB integrat** (normalment WS2812B o compatible) que es pot controlar via un sol pin GPIO.

---

## 📍 Pin del LED RGB

**Generalment és GPIO 48** (però pot variar segons la placa)

Altres possibles pins:
- GPIO 48 (més comú)
- GPIO 38
- GPIO 8

**Com saber quin és el teu?**
- Mira el PCB de la placa d'expansió
- Busca "RGB" o "WS2812" serigrafiats
- Comprova documentació de la teva placa

---

## 💡 Tipus de LED: WS2812B (NeoPixel)

És un LED RGB **addressable** amb control digital:
- **1 pin de dades** (DIN)
- **1 LED** a la placa
- Protocol: Timing específic (no PWM normal)
- Voltatge dades: **3.3V** (compatible ESP32)
- Voltatge alimentació: **5V** (però funciona amb 3.3V)

---

## 🔌 Connexió Interna (ja feta a la placa)

```
ESP32-S3                    LED RGB WS2812B
GPIO 48  ────────────────→  DIN (Data In)
5V (USB) ─────────────────→ VDD
GND      ─────────────────→ GND
```

**IMPORTANT**: El LED RGB normalment **ja està connectat** a la placa d'expansió. No has de cablear res!

