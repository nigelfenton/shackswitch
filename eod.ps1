# eod.ps1 — ShackSwitch end-of-session cleanup
# Usage:
#   .\eod.ps1                        — commit pending changes + push to GitHub
#   .\eod.ps1 -deploy                — also deploy to board after pushing
#   .\eod.ps1 "my commit message"    — use supplied message (skips prompt)
#   .\eod.ps1 "my message" -deploy   — both

param(
    [string]$msg    = "",
    [switch]$deploy = $false
)

$repo  = "C:\Users\nigel\Documents\shackswitch"
$v2    = "$repo\shackswitch-v2"
$board = "arduino@10.0.0.56"

Set-Location $repo

# ── 1. Show what's changed ────────────────────────────────────────────────────
Write-Host "`n── Git status ──" -ForegroundColor Cyan
git status --short

$dirty = git status --porcelain
if ($dirty) {

    # ── 2. Get commit message ─────────────────────────────────────────────────
    if (-not $msg) {
        Write-Host ""
        $msg = Read-Host "Commit message"
    }
    if (-not $msg) {
        Write-Host "No message — skipping commit." -ForegroundColor Yellow
    } else {
        # Stage tracked changed files (won't accidentally add secrets)
        git add shackswitch-v2/main.py `
                shackswitch-v2/nextion.py `
                shackswitch-v2/kpa1500.py `
                shackswitch-v2/acom600s.py `
                shackswitch-v2/wifi_scan_svc.py `
                "shackswitch-v2/templates/settings.html" `
                "shackswitch-v2/templates/index.html" `
                README.md 2>$null

        git commit -m "$msg

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
    }
}

# ── 3. Push to GitHub ─────────────────────────────────────────────────────────
Write-Host "`n── Pushing to GitHub ──" -ForegroundColor Cyan
git push origin main
Write-Host "GitHub up to date." -ForegroundColor Green

# ── 4. Deploy to board (optional) ────────────────────────────────────────────
if ($deploy) {
    Write-Host "`n── Deploying to board ──" -ForegroundColor Cyan

    scp "$v2\main.py"    "${board}:~/main.py"
    scp "$v2\nextion.py" "${board}:~/nextion.py"

    ssh $board @"
docker cp ~/main.py    first-app-main-1:/app/python/main.py
docker cp ~/nextion.py first-app-main-1:/app/python/nextion.py
docker restart first-app-main-1
"@
    Write-Host "Board restarting with latest code." -ForegroundColor Green
}

Write-Host "`nDone.`n" -ForegroundColor Green
