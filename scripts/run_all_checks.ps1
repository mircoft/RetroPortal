# Spusti z korena repozitara: .\scripts\run_all_checks.ps1
# Kontrola Android buildu + Python agentov.
# JAVA_HOME sa skusi doplnit automaticky (Android Studio JBR alebo java v PATH).

$ErrorActionPreference = "Stop"
$Root = if ($PSScriptRoot) { Split-Path -Parent $PSScriptRoot } else { Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path) }
Set-Location $Root

Write-Host "== RetroPortal: koren $Root ==" -ForegroundColor Cyan

function Set-JavaHomeFromStudio {
    if ($env:JAVA_HOME -and (Test-Path (Join-Path $env:JAVA_HOME "bin\java.exe"))) {
        Write-Host "JAVA_HOME uz nastavene: $($env:JAVA_HOME)" -ForegroundColor DarkGray
        return $true
    }
    $candidates = @(
        "$env:ProgramFiles\Android\Android Studio\jbr"
        "${env:ProgramFiles(x86)}\Android\Android Studio\jbr"
        "$env:LOCALAPPDATA\Programs\Android\Android Studio\jbr"
        "$env:ProgramFiles\JetBrains\Android Studio\jbr"
    )
    foreach ($p in $candidates) {
        if (-not $p) { continue }
        $javaExe = Join-Path $p "bin\java.exe"
        if (Test-Path $javaExe) {
            $env:JAVA_HOME = $p
            Write-Host "JAVA_HOME doplnene z Android Studio: $p" -ForegroundColor Green
            return $true
        }
    }
    try {
        $java = Get-Command java -ErrorAction Stop | Select-Object -First 1
        if ($java.Source) {
            $bin = Split-Path -Parent $java.Source
            $home = Split-Path -Parent $bin
            if (Test-Path (Join-Path $home "bin\java.exe")) {
                $env:JAVA_HOME = $home
                Write-Host "JAVA_HOME doplnene z java v PATH: $home" -ForegroundColor Green
                return $true
            }
        }
    } catch {
        # ignore
    }
    return $false
}

# --- Volitelne: git pull ---
if ($args -contains "--pull") {
    Write-Host "`n>> git pull" -ForegroundColor Yellow
    git pull
}

# --- Android: Gradle ---
$gradleBat = Join-Path $Root "gradlew.bat"
if (-not (Test-Path $gradleBat)) {
    Write-Warning "gradlew.bat nenasiel — preskakujem Android build."
} else {
    $null = Set-JavaHomeFromStudio
    if (-not $env:JAVA_HOME -or -not (Test-Path (Join-Path $env:JAVA_HOME "bin\java.exe"))) {
        Write-Warning @"
JAVA_HOME nie je nastavene a nepodarilo sa najst Android Studio JBR ani java v PATH.
V PowerShelli (docasne v tejto session):
  `$env:JAVA_HOME = 'C:\Program Files\Android\Android Studio\jbr'
Potom znova spusti tento skript.
"@
    } else {
        Write-Host "`n>> gradlew.bat assembleDebug" -ForegroundColor Yellow
        & $gradleBat assembleDebug --no-daemon
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    }
}

# --- Python agenti ---
Write-Host "`n>> pytest agents" -ForegroundColor Yellow
python -m pytest agents/tests -v --tb=short
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "`n== Hotovo bez chyby ==" -ForegroundColor Green
