param(
  [string]$Version = "1.0.1"
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$sourceDir = Join-Path $repoRoot "src\Solar_Prognose_Monitor"
$buildRoot = Join-Path $repoRoot "build\release-$Version"
$distDir = Join-Path $repoRoot "dist\v$Version"
$sketchName = "Solar_Prognose_Monitor"

$cliCommand = Get-Command arduino-cli -ErrorAction SilentlyContinue
if ($cliCommand) {
  $arduinoCli = $cliCommand.Source
} else {
  $arduinoCli = "C:\Program Files\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe"
}
if (-not (Test-Path -LiteralPath $arduinoCli)) {
  throw "arduino-cli wurde weder im PATH noch in der Arduino-IDE-Standardinstallation gefunden."
}

$fourMbOutput = Join-Path $buildRoot "4MB"
$eightMbOutput = Join-Path $buildRoot "8MB"
$eightMbSketch = Join-Path $buildRoot "8MB-sketch\$sketchName"
New-Item -ItemType Directory -Force -Path $fourMbOutput, $eightMbOutput, $eightMbSketch, $distDir | Out-Null

Write-Host "Kompiliere 4-MB-Variante ..."
& $arduinoCli compile `
  --fqbn "esp32:esp32:esp32:FlashSize=4M,PartitionScheme=default" `
  --build-property "upload.maximum_size=1835008" `
  --output-dir $fourMbOutput `
  $sourceDir
if ($LASTEXITCODE -ne 0) { throw "Der 4-MB-Build ist fehlgeschlagen." }

Copy-Item -LiteralPath (Join-Path $sourceDir "$sketchName.ino") -Destination $eightMbSketch -Force
Copy-Item -LiteralPath (Join-Path $sourceDir "SolarPrognoseMonitor.cpp") -Destination $eightMbSketch -Force
Copy-Item -LiteralPath (Join-Path $sourceDir "SolarPrognoseMonitor.h") -Destination $eightMbSketch -Force
Copy-Item -LiteralPath (Join-Path $sourceDir "partitions_8MB.csv") `
  -Destination (Join-Path $eightMbSketch "partitions.csv") -Force

Write-Host "Kompiliere 8-MB-Variante ..."
& $arduinoCli compile `
  --fqbn "esp32:esp32:esp32:FlashSize=8M,PartitionScheme=default_8MB" `
  --build-property "upload.maximum_size=3670016" `
  --output-dir $eightMbOutput `
  $eightMbSketch
if ($LASTEXITCODE -ne 0) { throw "Der 8-MB-Build ist fehlgeschlagen." }

$fourMbBase = Join-Path $fourMbOutput "$sketchName.ino"
$eightMbBase = Join-Path $eightMbOutput "$sketchName.ino"
Copy-Item -LiteralPath "$fourMbBase.bin" `
  -Destination (Join-Path $distDir "Solar_Prognose_Monitor_v${Version}_4MB_Update.bin") -Force
Copy-Item -LiteralPath "$fourMbBase.merged.bin" `
  -Destination (Join-Path $distDir "Solar_Prognose_Monitor_v${Version}_4MB_USB-Komplett.bin") -Force
Copy-Item -LiteralPath "$eightMbBase.bin" `
  -Destination (Join-Path $distDir "Solar_Prognose_Monitor_v${Version}_8MB_Update.bin") -Force
Copy-Item -LiteralPath "$eightMbBase.merged.bin" `
  -Destination (Join-Path $distDir "Solar_Prognose_Monitor_v${Version}_8MB_USB-Komplett.bin") -Force

Write-Host "Fertig. Release-Dateien: $distDir"
