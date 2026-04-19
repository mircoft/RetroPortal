# Spusti z korena repozitara: .\scripts\run_all_checks.ps1
# Kontrola Android buildu + Python agentov. JAVA_HOME a adb volitelne.

$ErrorActionPreference = "Stop"
$Root = if ($PSScriptRoot) { Split-Path -Parent $PSScriptRoot } else { Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path) }
Set-Location $Root

Write-Host "== RetroPortal: koren $Root ==" -ForegroundColor Cyan

# --- Volitelne: git pull ---
if ($args -contains "--pull") {
    Write-Host "`n>> git pull" -ForegroundColor Yellow
    git pull
}

# --- Android: Gradle ---
$gradleBat = Join-Path $Root "gradlew.bat"
if (-not (Test-Path $gradleBat)) {
    Write-Warning "gradlew.bat nenasiel — preskakujem Android build."
} elseif (-not $env:JAVA_HOME) {
    Write-Warning "JAVA_HOME nie je nastavene — preskakujem gradlew (nastav napr. na Android Studio jbr)."
} else {
    Write-Host "`n>> gradlew.bat assembleDebug" -ForegroundColor Yellow
    & $gradleBat assembleDebug --no-daemon
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

# --- Python agenti ---
Write-Host "`n>> pytest agents" -ForegroundColor Yellow
python -m pytest agents/tests -v --tb=short
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "`n== Hotovo bez chyby ==" -ForegroundColor Green
