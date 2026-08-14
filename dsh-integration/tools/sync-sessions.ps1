# sync-sessions.ps1 — sync the DSH session library to a private git remote.
# Sessions live in ~/.dsh/sessions/<project-key>/session-<uuid>/session.jsonl.zstd.
# The repo's .gitignore whitelists the projects you chose to sync.
#
# Usage:
#   .\sync-sessions.ps1          # commit local changes and push (office)
#   .\sync-sessions.ps1 -Pull    # fetch and reset to remote (home; local uncommitted changes are discarded)
#   .\sync-sessions.ps1 -Status  # show sync state and session counts
#
# Rule: never edit the SAME session on two machines at once (append-only log,
# binary, cannot be merged). Push after finishing; pull before continuing.
param(
    [switch]$Pull,
    [switch]$Status
)

$ErrorActionPreference = "Stop"
$sessions = Join-Path $env:USERPROFILE ".dsh\sessions"
if (-not (Test-Path (Join-Path $sessions ".git"))) {
    Write-Host "[ERROR] $sessions is not a git repository yet." -ForegroundColor Red
    exit 1
}

function Get-SyncState {
    $branch = git -C $sessions rev-parse --abbrev-ref HEAD 2>$null
    $ahead = git -C $sessions rev-list --count "@{u}..HEAD" 2>$null
    $behind = git -C $sessions rev-list --count "HEAD..@{u}" 2>$null
    return @{ Branch = $branch; Ahead = $ahead; Behind = $behind }
}

if ($Status) {
    $state = Get-SyncState
    Write-Host "branch: $($state.Branch)  ahead: $($state.Ahead)  behind: $($state.Behind)" -ForegroundColor Cyan
    $dirty = git -C $sessions status --porcelain
    if ($dirty) { Write-Host "uncommitted changes:" -ForegroundColor Yellow; $dirty | ForEach-Object { Write-Host "  $_" } }
    else { Write-Host "working tree clean" -ForegroundColor Green }
    Write-Host "sessions tracked:" -ForegroundColor Cyan
    Get-ChildItem $sessions -Directory | Where-Object { $_.Name -ne ".git" } | ForEach-Object {
        $count = (Get-ChildItem $_.FullName -Directory -ErrorAction SilentlyContinue).Count
        Write-Host "  $($_.Name) ($count sessions)"
    }
    exit 0
}

if ($Pull) {
    $dirty = git -C $sessions status --porcelain
    if ($dirty) {
        Write-Host "[WARN] Local uncommitted changes will be DISCARDED by reset:" -ForegroundColor Yellow
        $dirty | ForEach-Object { Write-Host "  $_" }
        Write-Host "       Aborting. Commit/push them first, or delete them manually." -ForegroundColor Yellow
        exit 1
    }
    git -C $sessions fetch origin 2>&1 | Out-Null
    git -C $sessions reset --hard origin/master 2>&1 | Select-Object -First 2
    Write-Host "[OK] sessions synced from remote" -ForegroundColor Green
    exit 0
}

# default: push
Get-ChildItem (Join-Path $sessions ".git\refs\remotes\origin") -Filter "*.lock" -ErrorAction SilentlyContinue | Remove-Item -Force
git -C $sessions fetch origin 2>&1 | Out-Null
$state = Get-SyncState
if ([int]$state.Behind -gt 0) {
    Write-Host "[WARN] Remote has $($state.Behind) commit(s) not present locally (someone pushed from another machine?)." -ForegroundColor Yellow
    Write-Host "       Run with -Pull first; note it discards local uncommitted sessions." -ForegroundColor Yellow
    exit 1
}
git -C $sessions add -A
$changed = git -C $sessions status --porcelain
if (-not $changed) {
    Write-Host "nothing to sync" -ForegroundColor Green
    exit 0
}
$stamp = Get-Date -Format "yyyy-MM-dd HH:mm:ss"
git -C $sessions commit -m "sessions: sync $stamp" 2>&1 | Select-Object -First 1
git -C $sessions push 2>&1 | Select-Object -First 2
Write-Host "[OK] sessions pushed" -ForegroundColor Green
