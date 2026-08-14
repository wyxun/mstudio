# install.ps1 — install mstudio-dsh / kicad-auditor-dsh plugins on this machine.
# Two source modes:
#   A. Deployment bundle (pack.ps1 output, lib prebuilt): extract the zip and
#      run this script — default mode, no build toolchain required.
#   B. Source (after git pull): add -Build; needs a deepseek-harness checkout.
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File .\install.ps1 `
#       [-MStudioDir <mstudio repo path>] [-KicadAuditorDir <kicad-auditor path>] `
#       [-ModusTemplateDir <firmware project path>] [-Build] [-SkipKicad]
#
# Path rules: omitted paths are auto-detected as siblings of this script.
param(
    [string]$MStudioDir = "",
    [string]$KicadAuditorDir = "",
    [string]$ModusTemplateDir = "",
    [string]$ProfileDir = "",
    [switch]$Build,
    [switch]$SkipKicad
)

$ErrorActionPreference = "Stop"
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path

# ── 0. detect mode and repo locations ───────────────────────────────────────
$packMode = Test-Path (Join-Path $scriptDir "mstudio-dsh")
# Source layout: this script lives in <repo>/dsh-integration/tools/install.ps1,
# so package.json sits two levels up.
$sourceMode = -not $packMode -and (Test-Path (Join-Path $scriptDir "..\package.json"))

if ($packMode) {
    Write-Host "[MODE] bundle install (lib prebuilt, no build)" -ForegroundColor Cyan
} elseif ($sourceMode) {
    Write-Host "[MODE] source install" -ForegroundColor Cyan
} else {
    Write-Host "[ERROR] Cannot recognize run location: need bundle content (mstudio-dsh/) or source (package.json)." -ForegroundColor Red
    exit 1
}

if ($sourceMode) {
    $mstudioPkg = Split-Path -Parent $scriptDir   # <repo>/dsh-integration
    if (-not $MStudioDir) { $MStudioDir = Split-Path -Parent $mstudioPkg }
}
# probe kicad-auditor as a sibling of mstudio (or of this script in bundle mode)
if (-not $KicadAuditorDir) {
    $base = if ($MStudioDir) { Split-Path -Parent $MStudioDir } else { Split-Path -Parent $scriptDir }
    $cand = Join-Path $base "kicad-auditor"
    if (Test-Path $cand) { $KicadAuditorDir = $cand }
}
if (-not $KicadAuditorDir -and -not $SkipKicad) {
    Write-Host "[WARN] kicad-auditor repo not found; kicad plugin will NOT be mounted (or pass -KicadAuditorDir)" -ForegroundColor Yellow
    $SkipKicad = $true
}

# ── 1. build (only with -Build) ─────────────────────────────────────────────
if ($Build) {
    $tsc = Join-Path $MStudioDir "..\deepseek-harness\node_modules\.bin\tsc.cmd"
    if (-not (Test-Path $tsc)) {
        Write-Host "[ERROR] -Build needs deepseek-harness source as sibling of mstudio with pnpm install: $tsc" -ForegroundColor Red
        exit 1
    }
    function Invoke-Build { param([string]$pkg, [string]$label)
        Write-Host "[BUILD] $label ..." -ForegroundColor Cyan
        Push-Location $pkg
        Remove-Item lib -Recurse -Force -ErrorAction SilentlyContinue
        Remove-Item tsconfig.tsbuildinfo -Force -ErrorAction SilentlyContinue
        & $tsc -p tsconfig.json --incremental false
        if ($LASTEXITCODE -ne 0) { Write-Host "[ERROR] $label build failed" -ForegroundColor Red; Pop-Location; exit 1 }
        Pop-Location
    }
    Invoke-Build $mstudioPkg "mstudio-dsh"
    if (-not $SkipKicad) { Invoke-Build (Join-Path $KicadAuditorDir "dsh-integration") "kicad-auditor-dsh" }
}

# ── 2. locate DSH profile ───────────────────────────────────────────────────
if (-not $ProfileDir) { $ProfileDir = Join-Path $env:USERPROFILE ".dsh\profiles\web" }
$wxDir = Join-Path $ProfileDir "node_modules\@wx"
if (-not (Test-Path $ProfileDir)) {
    Write-Host "[ERROR] DSH profile not found: $ProfileDir (run DSH web once first)" -ForegroundColor Red
    exit 1
}
New-Item -ItemType Directory -Path $wxDir -Force | Out-Null

# ── 3. deploy plugins ────────────────────────────────────────────────────────
# Source mode builds into <pkg>/lib and keeps package.json at the package root;
# bundle mode ships <stage>/<name>/{lib,package.json}. The deployed package must
# carry BOTH lib/ and package.json (Node needs the package entry to resolve the
# plugin), plus its @deepseek-ai/* runtime deps linked into the profile.
$mstudioPkgDir = if ($sourceMode) { $mstudioPkg } else { Join-Path $scriptDir "mstudio-dsh" }
$kicadPkgDir = Join-Path $KicadAuditorDir "dsh-integration"
$kicadStageDir = if ($sourceMode) { $kicadPkgDir } else { Join-Path $scriptDir "kicad-auditor-dsh" }

function Install-Pkg { param([string]$name, [string]$pkgSrc)
    $dest = Join-Path $wxDir $name
    Remove-Item $dest -Recurse -Force -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Path $dest -Force | Out-Null
    Copy-Item (Join-Path $pkgSrc "lib") -Destination "$dest\lib" -Recurse -Force
    $pkgJson = Join-Path $pkgSrc "package.json"
    if (Test-Path $pkgJson) {
        Copy-Item $pkgJson -Destination $dest -Force
    } else {
        Write-Host "[WARN] ${name}: no package.json at ${pkgSrc} (Node cannot resolve the package entry)" -ForegroundColor Yellow
    }
    Write-Host "[OK] deployed $name" -ForegroundColor Green
}
Install-Pkg "mstudio-dsh" $mstudioPkgDir
if (-not $SkipKicad) { Install-Pkg "kicad-auditor-dsh" $kicadStageDir }

# ── 3b. link @deepseek-ai/* runtime deps into the profile ────────────────────
# The plugin bundles import @deepseek-ai/schemastery and @deepseek-ai/dsh-tools
# at runtime; the profile's own node_modules only holds @wx. Link the built
# workspace packages from a deepseek-harness checkout (sibling of mstudio in
# source mode; auto-detected otherwise) so Node resolves them.
function Invoke-EnsureDepLink { param([string]$dep, [string]$src)
    $dest = Join-Path $ProfileDir "node_modules\@deepseek-ai"
    New-Item -ItemType Directory -Path $dest -Force | Out-Null
    $link = Join-Path $dest $dep
    if (Test-Path $link) {
        $item = Get-Item $link
        if ($item.LinkType) { Write-Host "[OK] dep linked: @deepseek-ai/$dep ($($item.Target))" -ForegroundColor Green; return }
        Remove-Item $link -Recurse -Force
    }
    if (-not (Test-Path $src)) {
        Write-Host "[WARN] @deepseek-ai/$dep source not found at $src — plugin will fail at load" -ForegroundColor Yellow
        return
    }
    New-Item -ItemType Junction -Path $link -Target $src | Out-Null
    Write-Host "[OK] dep linked: @deepseek-ai/$dep -> $src" -ForegroundColor Green
}
$dshRoot = if ($sourceMode) { Join-Path $MStudioDir "..\deepseek-harness" } else { Join-Path $ProfileDir "..\..\..\deepseek-harness" }
$dshRoot = [System.IO.Path]::GetFullPath($dshRoot)
Invoke-EnsureDepLink "schemastery" (Join-Path $dshRoot "vendor\schemastery")
Invoke-EnsureDepLink "dsh-tools" (Join-Path $dshRoot "packages\core\tools")

# ── 4. generate/update profile patch with machine-local paths ───────────────
if (-not $ModusTemplateDir -and $MStudioDir) {
    $cand = Join-Path (Split-Path -Parent $MStudioDir) "modus_template"
    if (Test-Path $cand) { $ModusTemplateDir = $cand }
}
$patchFile = Join-Path $ProfileDir "cordis.patch.yml"
$patchBegin = "# >>> mstudio-dsh managed block >>>"
$patchEnd = "# <<< mstudio-dsh managed block <<<"

function Get-ManagedBlock {
    $lines = @(
        "# >>> mstudio-dsh managed block >>>",
        "# Auto-generated by install.ps1; replaced wholesale on reinstall. Move manual edits outside this block.",
        "- insert:",
        "    - id: mstudio-aitrace",
        "      name: '@wx/mstudio-dsh'",
        "      config:",
        "        workDir: $($ModusTemplateDir -replace '\\','/')",
        "        aitracePath: tools/aitrace.exe",
        "        elfPath: build/template.elf",
        "        mapPath: build/template.map",
        "        captureDir: captures",
        "        approval: true"
    )
    if (-not $SkipKicad) {
        $lines += @(
            "    - id: kicad-auditor-dsh",
            "      name: '@wx/kicad-auditor-dsh'",
            "      config:",
            "        workDir: $($KicadAuditorDir -replace '\\','/')",
            "        auditorPath: $((Join-Path $KicadAuditorDir 'kicad-auditor.exe') -replace '\\','/')"
        )
    }
    $lines += "# <<< mstudio-dsh managed block <<<"
    return ($lines -join "`r`n")
}

$block = Get-ManagedBlock
$utf8Bom = New-Object System.Text.UTF8Encoding($true)

if (Test-Path $patchFile) {
    $content = [System.IO.File]::ReadAllText($patchFile, $utf8Bom)
    if ($content.Contains($patchBegin)) {
        $pattern = "(?s)$([regex]::Escape($patchBegin)).*?$([regex]::Escape($patchEnd))"
        $content = [regex]::Replace($content, $pattern, $block)
    } else {
        # A lone `[]` is the stock empty patch (or a file that is only comments).
        # Appending a second top-level YAML document breaks parsing, so replace
        # the whole file with the managed block instead.
        $trimmed = $content.Trim()
        if ($trimmed -eq '[]') {
            $content = $block
        } else {
            $content = $content.TrimEnd() + "`r`n`r`n" + $block
        }
    }
    [System.IO.File]::WriteAllText($patchFile, $content, $utf8Bom)
} else {
    [System.IO.File]::WriteAllText($patchFile, $block, $utf8Bom)
}
Write-Host "[OK] profile patch updated: $patchFile" -ForegroundColor Green

# ── 5. install skills from repo copies ───────────────────────────────────────
# Source mode: each repo ships its own skill (mstudio: aitrace, kicad-auditor:
# kicad-auditor). Bundle mode: both skills are staged under one skills/ dir.
$agentsSkills = Join-Path $env:USERPROFILE ".agents\skills"
New-Item -ItemType Directory -Path $agentsSkills -Force | Out-Null
$skillCandidates = @()
if ($sourceMode) {
    $skillCandidates += @(
        (Join-Path $mstudioPkg "skills\aitrace"),
        (Join-Path $kicadPkgDir "skills\kicad-auditor")
    )
} else {
    $skillCandidates += @(
        (Join-Path $scriptDir "skills\aitrace"),
        (Join-Path $scriptDir "skills\kicad-auditor")
    )
}
foreach ($skillDir in $skillCandidates) {
    if (Test-Path $skillDir) {
        $name = Split-Path -Leaf $skillDir
        $dest = Join-Path $agentsSkills $name
        Remove-Item $dest -Recurse -Force -ErrorAction SilentlyContinue
        Copy-Item $skillDir -Destination $dest -Recurse -Force
        Write-Host "[OK] skill installed: $name" -ForegroundColor Green
    }
}

# ── 6. toolchain check ──────────────────────────────────────────────────────
Write-Host ""
Write-Host "===== toolchain check =====" -ForegroundColor Cyan
$checks = @()
if (Test-Path "$wxDir\mstudio-dsh\lib\index.js") { $checks += "[OK] mstudio-dsh plugin in place" } else { $checks += "[!!] mstudio-dsh plugin MISSING" }
if (Test-Path "$wxDir\mstudio-dsh\package.json") { $checks += "[OK] mstudio-dsh package.json" } else { $checks += "[!!] mstudio-dsh package.json MISSING (Node cannot resolve the plugin)" }
if (-not $SkipKicad) {
    if (Test-Path "$wxDir\kicad-auditor-dsh\lib\index.js") { $checks += "[OK] kicad-auditor-dsh plugin in place" } else { $checks += "[!!] kicad-auditor-dsh plugin MISSING" }
    if (Test-Path "$wxDir\kicad-auditor-dsh\package.json") { $checks += "[OK] kicad-auditor-dsh package.json" } else { $checks += "[!!] kicad-auditor-dsh package.json MISSING (Node cannot resolve the plugin)" }
}
foreach ($dep in @("schemastery", "dsh-tools")) {
    $link = Join-Path $ProfileDir "node_modules\@deepseek-ai\$dep"
    if (Test-Path $link) { $checks += "[OK] dep linked: @deepseek-ai/$dep" } else { $checks += "[!!] dep @deepseek-ai/$dep MISSING (plugin will fail at load)" }
}
if (Test-Path "$env:USERPROFILE\.agents\skills\kicad-auditor\SKILL.md") { $checks += "[OK] skill: kicad-auditor" } else { $checks += "[!!] skill: kicad-auditor MISSING" }
if (Test-Path "$env:USERPROFILE\.agents\skills\aitrace\SKILL.md") { $checks += "[OK] skill: aitrace" } else { $checks += "[!!] skill: aitrace MISSING" }
$gpp = Get-Command g++ -ErrorAction SilentlyContinue
if ($gpp) { $checks += "[OK] g++ ($($gpp.Source))" } else { $checks += "[!!] g++ not on PATH (MSYS2 needed to build kicad-auditor.exe)" }
if (-not $SkipKicad -and $KicadAuditorDir) {
    if (Test-Path (Join-Path $KicadAuditorDir "kicad-auditor.exe")) { $checks += "[OK] kicad-auditor.exe built" } else { $checks += "[!!] kicad-auditor.exe MISSING (run make.bat in kicad-auditor)" }
}
$checks | ForEach-Object { Write-Host $_ }
Write-Host ""
Write-Host "[NOTE] aitrace.exe: build from mstudio source (build.bat) or copy the binary."
Write-Host "       KiCad Huaqiu, OpenOCD and probe drivers: install as needed."
Write-Host "       Restart DSH Web GUI to load the plugins."
