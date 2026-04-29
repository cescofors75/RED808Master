# =============================================================================
# flash_esp32.ps1 — RED808 Master ESP32-S3 N16R8
# Flash completo: bootloader + partitions + firmware [+ LittleFS]
#
# USO:
#   .\flash_esp32.ps1                   # firmware + LittleFS
#   .\flash_esp32.ps1 -SkipFS          # solo firmware (más rápido)
#   .\flash_esp32.ps1 -Port COM12      # otro puerto
#
# REQUISITO: poner el ESP32-S3 en modo bootloader (BOOT + RESET) antes de ejecutar.
# =============================================================================
param(
    [string]$Port     = "COM11",
    [int]   $Baud     = 460800,
    [switch]$SkipFS
)

$env:PYTHONIOENCODING = 'utf-8'
chcp 65001 > $null

$py      = "$env:USERPROFILE\.platformio\penv\Scripts\python.exe"
$esptool = "$env:USERPROFILE\.platformio\packages\tool-esptoolpy\esptool.py"
$b       = "$PSScriptRoot\.pio\build\redmaster-s3-r8n16"

# Verificar archivos
$required = @("$b\bootloader.bin", "$b\partitions.bin", "$b\firmware.bin")
foreach ($f in $required) {
    if (-not (Test-Path $f)) {
        Write-Host "ERROR: No encontrado: $f" -ForegroundColor Red
        Write-Host "Compila primero con: pio run (o PlatformIO: Build)" -ForegroundColor Yellow
        exit 1
    }
}
if (-not $SkipFS -and -not (Test-Path "$b\littlefs.bin")) {
    Write-Host "AVISO: littlefs.bin no encontrado. Usa -SkipFS para omitir el filesystem." -ForegroundColor Yellow
    Write-Host "       O compila con: pio run --target buildfs" -ForegroundColor Yellow
    exit 1
}

Write-Host ""
Write-Host "=== RED808 Master Flash ===" -ForegroundColor Cyan
Write-Host "Puerto : $Port @ $Baud baud"
Write-Host "Chip   : ESP32-S3 R8N16 (dio/80m/16MB, PSRAM OPI)"
Write-Host "Build  : $b"
if ($SkipFS) { Write-Host "Modo   : Solo firmware (sin LittleFS)" -ForegroundColor Yellow }
else         { Write-Host "Modo   : Firmware + LittleFS"            -ForegroundColor Green }
Write-Host ""

# ----- PASO 1: Firmware completo -----
Write-Host "--- Flashing firmware ---" -ForegroundColor Cyan
& $py $esptool `
    --chip esp32s3 --port $Port --baud $Baud `
    --before default-reset --after hard-reset `
    write-flash -z --flash-mode dio --flash-freq 80m --flash-size 16MB `
    0x00000 "$b\bootloader.bin" `
    0x08000 "$b\partitions.bin" `
    0x10000 "$b\firmware.bin"

if ($LASTEXITCODE -ne 0) {
    Write-Host "ERROR: Firmware flash falló (exit $LASTEXITCODE)" -ForegroundColor Red
    exit $LASTEXITCODE
}

if ($SkipFS) {
    Write-Host ""
    Write-Host "=== Firmware flasheado OK ===" -ForegroundColor Green
    exit 0
}

# ----- PASO 2: LittleFS -----
Write-Host ""
Write-Host "--- Flashing LittleFS (web assets) ---" -ForegroundColor Cyan
Write-Host "NOTA: Vuelve a poner en modo bootloader si el ESP32 ya arrancó (BOOT+RESET)" -ForegroundColor Yellow
Write-Host "Pulsa Enter cuando esté en modo bootloader..."
Read-Host | Out-Null

& $py $esptool `
    --chip esp32s3 --port $Port --baud $Baud `
    --before default-reset --after hard-reset `
    write-flash -z --flash-mode dio --flash-freq 80m --flash-size 16MB `
    0x410000 "$b\littlefs.bin"

if ($LASTEXITCODE -ne 0) {
    Write-Host "ERROR: LittleFS flash falló (exit $LASTEXITCODE)" -ForegroundColor Red
    exit $LASTEXITCODE
}

Write-Host ""
Write-Host "=== Flash completo OK (firmware + LittleFS) ===" -ForegroundColor Green
