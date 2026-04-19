# Run from repo root: .\scripts\run_all_checks.ps1
# UTF-8 BOM recommended for Windows PowerShell 5.1

$ErrorActionPreference = "Stop"
$Root = if ($PSScriptRoot) { Split-Path -Parent $PSScriptRoot } else { Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path) }
Set-Location $Root

Write-Host ('== RetroPortal root: ' + $Root + ' ==') -ForegroundColor Cyan

function Set-JavaHomeFromStudio {
    if ($env:JAVA_HOME -and (Test-Path (Join-Path $env:JAVA_HOME 'bin\java.exe'))) {
        Write-Host ('JAVA_HOME already set: ' + $env:JAVA_HOME) -ForegroundColor DarkGray
        return $true
    }
    $candidates = @(
        ($env:ProgramFiles + '\Android\Android Studio\jbr')
        (${env:ProgramFiles(x86)} + '\Android\Android Studio\jbr')
        ($env:LOCALAPPDATA + '\Programs\Android\Android Studio\jbr')
        ($env:ProgramFiles + '\JetBrains\Android Studio\jbr')
    )
    foreach ($p in $candidates) {
        if (-not $p) { continue }
        $javaExe = Join-Path $p 'bin\java.exe'
        if (Test-Path $javaExe) {
            $env:JAVA_HOME = $p
            Write-Host ('JAVA_HOME set from Android Studio: ' + $p) -ForegroundColor Green
            return $true
        }
    }
    try {
        $java = Get-Command java -ErrorAction Stop | Select-Object -First 1
        if ($java.Source) {
            $bin = Split-Path -Parent $java.Source
            $home = Split-Path -Parent $bin
            if (Test-Path (Join-Path $home 'bin\java.exe')) {
                $env:JAVA_HOME = $home
                Write-Host ('JAVA_HOME set from PATH java: ' + $home) -ForegroundColor Green
                return $true
            }
        }
    } catch {
    }
    return $false
}

if ($args -contains '--pull') {
    Write-Host ''
    Write-Host '>> git pull' -ForegroundColor Yellow
    git pull
}

$gradleBat = Join-Path $Root 'gradlew.bat'
if (-not (Test-Path $gradleBat)) {
    Write-Warning 'gradlew.bat not found, skipping Android build.'
} else {
    $null = Set-JavaHomeFromStudio
    if (-not $env:JAVA_HOME -or -not (Test-Path (Join-Path $env:JAVA_HOME 'bin\java.exe'))) {
        Write-Warning 'JAVA_HOME not set and JBR/java not found.'
        Write-Host 'Try: $env:JAVA_HOME = ''C:\Program Files\Android\Android Studio\jbr''' -ForegroundColor Yellow
    } else {
        Write-Host ''
        Write-Host '>> gradlew.bat assembleDebug' -ForegroundColor Yellow
        & $gradleBat assembleDebug --no-daemon
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    }
}

Write-Host ''
Write-Host '>> pytest agents' -ForegroundColor Yellow
python -m pytest agents/tests -v --tb=short
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host ''
Write-Host 'Done.' -ForegroundColor Green
