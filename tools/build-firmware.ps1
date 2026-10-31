# Baut die Firmware und legt das fertige Image in firmware/ ab.
# Aufruf im ESP-IDF-Terminal (VS Code: F1 -> "ESP-IDF: Open ESP-IDF Terminal"):
#   powershell -ExecutionPolicy Bypass -File .\tools\build-firmware.ps1

$ErrorActionPreference = "Stop"
Set-Location (Split-Path -Parent $PSScriptRoot)      # Projektwurzel

Write-Host "==> Build" -ForegroundColor Cyan
idf.py build
if ($LASTEXITCODE -ne 0) { throw "Build fehlgeschlagen" }

Write-Host "==> Image zusammenfuehren" -ForegroundColor Cyan
$merged = "build/merged-binary.bin"
if (Test-Path $merged) { Remove-Item $merged }
idf.py merge-bin
if ($LASTEXITCODE -ne 0 -or -not (Test-Path $merged)) {
    Write-Host "idf.py merge-bin nicht verfuegbar, nutze esptool" -ForegroundColor Yellow
    Push-Location build
    python -m esptool --chip esp32s3 merge_bin -o merged-binary.bin "@flash_args"
    $rc = $LASTEXITCODE
    Pop-Location
    if ($rc -ne 0 -or -not (Test-Path $merged)) { throw "merge_bin fehlgeschlagen" }
}

Write-Host "==> Nach firmware/ kopieren" -ForegroundColor Cyan
New-Item -ItemType Directory -Force firmware | Out-Null
Copy-Item $merged "firmware/g502-ble-merged.bin" -Force
if (Test-Path "build/maus.bin") { Copy-Item "build/maus.bin" "firmware/g502-ble-app.bin" -Force }

$f = Get-Item "firmware/g502-ble-merged.bin"
$hash = (Get-FileHash $f.FullName -Algorithm SHA256).Hash
Write-Host ""
Write-Host "Fertig: firmware/g502-ble-merged.bin ($([math]::Round($f.Length/1KB)) KB)" -ForegroundColor Green
Write-Host "SHA256: $hash"
Write-Host "Flashen an Adresse 0x0:"
Write-Host "  python -m esptool --chip esp32s3 -p COM3 -b 460800 write_flash 0x0 firmware/g502-ble-merged.bin"