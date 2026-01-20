# Solución de Errores

## Error 1: SPIFFS lleno ✅ SOLUCIONADO

Se ha creado una tabla de particiones personalizada `partitions_custom.csv` que asigna **~2 MB** para SPIFFS (antes era ~1.5 MB).

Nueva configuración:
- App: 3 MB (para el código)
- SPIFFS: 2 MB (para los samples)

## Error 2: No se puede conectar al ESP32-S3

### Solución Rápida
1. **Mantén presionado el botón BOOT** en la placa ESP32-S3
2. **Mientras lo mantienes presionado**, ejecuta el comando de upload
3. Suelta BOOT cuando empiece a subir

### Comando con el botón BOOT presionado:

```bash
# 1. PRESIONA Y MANTÉN el botón BOOT
# 2. Ejecuta:
platformio run --target uploadfs

# 3. Cuando veas "Connecting....." suelta BOOT
```

### Alternativa: Forzar modo bootloader

```bash
# Con puerto específico
platformio run --target uploadfs --upload-port COM5 --upload-flags "--before=default_reset" "--after=hard_reset"
```

### Si sigue sin funcionar:

1. **Resetea la placa**:
   - Presiona BOOT + RESET
   - Suelta RESET (mantén BOOT)
   - Ejecuta el comando upload
   - Suelta BOOT cuando conecte

2. **Verifica el driver USB**:
   - En Windows: Device Manager → Ports (COM & LPT)
   - Debe aparecer "USB Serial Device (COM5)" o similar
   - Si no aparece, instala drivers CH340 o CP2102

3. **Prueba otro puerto USB**:
   - Algunos puertos USB3 dan problemas
   - Usa un puerto USB2 si es posible

### Comando completo (con botón BOOT):

```bash
# Paso 1: Subir SPIFFS (PRESIONA BOOT)
platformio run --target uploadfs

# Paso 2: Subir código (PRESIONA BOOT)
platformio run --target upload

# Paso 3: Monitor
platformio device monitor -b 115200
```

## Verificar espacio SPIFFS

Después de subir, el monitor serial mostrará:
```
SPIFFS mounted
Total: XXXXXX bytes
Used: XXXXXX bytes
```

Con la nueva partición deberías tener ~2 MB disponibles.
