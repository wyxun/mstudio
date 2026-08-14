# pack.ps1 — build and package the mstudio-dsh deployment bundle.
# Usage (on the dev machine, with source + DSH checkout present):
#   powershell -ExecutionPolicy Bypass -File .\pack.ps1 [-KicadAuditorDir <path>] [-Output <zip path>]
# Produces one zip: compiled plugin libs + skills + install.ps1 + README.
# On the target machine, extract and run install.ps1 — no build needed.
param(
    [string]$KicadAuditorDir = "",
    [string]$Output = ""
)

$ErrorActionPreference = "Stop"
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$mstudioPkg = Split-Path -Parent $scriptDir        # mstudio/dsh-integration
$mstudioRoot = Split-Path -Parent $mstudioPkg      # mstudio/
if (-not $KicadAuditorDir) { $KicadAuditorDir = Join-Path (Split-Path -Parent $mstudioRoot) "kicad-auditor" }
$kicadPkg = Join-Path $KicadAuditorDir "dsh-integration"

# ── 1. locate DSH tsc ────────────────────────────────────────────────────────
$tsc = Join-Path $mstudioRoot "..\deepseek-harness\node_modules\.bin\tsc.cmd"
if (-not (Test-Path $tsc)) {
    Write-Host "[ERROR] DSH tsc not found: $tsc" -ForegroundColor Red
    Write-Host "        Need deepseek-harness as a sibling of mstudio with pnpm install done."
    exit 1
}

# ── 2. build both plugin packages ────────────────────────────────────────────
function Invoke-Build([string]$pkg, [string]$label) {
    Write-Host "[BUILD] $label ..." -ForegroundColor Cyan
    Push-Location $pkg
    Remove-Item lib -Recurse -Force -ErrorAction SilentlyContinue
    Remove-Item tsconfig.tsbuildinfo -Force -ErrorAction SilentlyContinue
    & $tsc -p tsconfig.json --incremental false
    if ($LASTEXITCODE -ne 0) { Write-Host "[ERROR] $label build failed" -ForegroundColor Red; Pop-Location; exit 1 }
    Pop-Location
    Write-Host "[OK] $label built" -ForegroundColor Green
}
Invoke-Build $mstudioPkg "mstudio-dsh"
Invoke-Build $kicadPkg   "kicad-auditor-dsh"

# ── 3. assemble the bundle ───────────────────────────────────────────────────
if (-not $Output) { $Output = Join-Path $mstudioRoot "mstudio-dsh-home.zip" }
$stage = Join-Path $env:TEMP "mstudio-dsh-pack_$([guid]::NewGuid().ToString('N'))"
New-Item -ItemType Directory -Path $stage -Force | Out-Null
try {
    Copy-Item "$mstudioPkg\lib" -Destination "$stage\mstudio-dsh\lib" -Recurse -Force
    Copy-Item "$mstudioPkg\package.json" -Destination "$stage\mstudio-dsh\" -Force
    Copy-Item "$kicadPkg\lib" -Destination "$stage\kicad-auditor-dsh\lib" -Recurse -Force
    Copy-Item "$kicadPkg\package.json" -Destination "$stage\kicad-auditor-dsh\" -Force
    if (Test-Path "$mstudioPkg\skills") { Copy-Item "$mstudioPkg\skills" -Destination "$stage\" -Recurse -Force }
    if (Test-Path "$kicadPkg\skills")   { Copy-Item "$kicadPkg\skills" -Destination "$stage\" -Recurse -Force }
    Copy-Item "$scriptDir\install.ps1" -Destination "$stage\" -Force
    Copy-Item "$scriptDir\README.md"   -Destination "$stage\" -Force -ErrorAction SilentlyContinue
    if (Test-Path $Output) { Remove-Item $Output -Force }
    Compress-Archive -Path "$stage\*" -DestinationPath $Output -Force
    Write-Host "[OK] bundle created: $Output" -ForegroundColor Green
    Write-Host "     On the target machine: extract, then run install.ps1 per README."
} finally {
    Remove-Item $stage -Recurse -Force -ErrorAction SilentlyContinue
}
