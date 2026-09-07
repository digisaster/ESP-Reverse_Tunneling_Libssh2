[CmdletBinding()]
param(
    [switch]$Clean
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"

function Stop-WithMessage {
    param([string]$Message)

    Write-Error $Message
    exit 1
}

$projectRoot = Split-Path -Parent $PSScriptRoot
$expectedCore = Join-Path $env:USERPROFILE ".platformio"
$expectedPio = Join-Path $expectedCore "penv\Scripts\pio.exe"
$pioCommand = Get-Command pio -CommandType Application -ErrorAction SilentlyContinue |
    Select-Object -First 1
if (-not $pioCommand) {
    if (-not (Test-Path -LiteralPath $expectedPio -PathType Leaf)) {
        Stop-WithMessage "pio.exe was not found on PATH or at '$expectedPio'. Install PIOArduino IDE and restart VS Code."
    }
    $pioExecutable = $expectedPio
} else {
    $pioExecutable = $pioCommand.Source
}

$scriptsDirectory = Split-Path -Parent $pioExecutable
$penvDirectory = Split-Path -Parent $scriptsDirectory
$coreDirectory = Split-Path -Parent $penvDirectory
if ($coreDirectory -ine $expectedCore) {
    Stop-WithMessage "Unexpected PlatformIO Core '$coreDirectory'. Expected '$expectedCore'. Do not use C:\pio; open a PIOArduino terminal."
}
$env:Path = "$scriptsDirectory;$env:Path"

$codeCommand = Get-Command code -CommandType Application -ErrorAction SilentlyContinue |
    Select-Object -First 1
if ($codeCommand) {
    $extensions = @(& $codeCommand.Source --list-extensions 2>$null)
    if ($extensions -contains "platformio.platformio-ide") {
        Write-Warning "The old PlatformIO IDE extension is installed. Remove it with: code --uninstall-extension platformio.platformio-ide"
    }
    if ($extensions -notcontains "pioarduino.pioarduino-ide") {
        Stop-WithMessage "PIOArduino IDE is not installed in VS Code."
    }
}

Write-Host "Project:         $projectRoot"
Write-Host "PlatformIO:      $pioExecutable"
Write-Host "PlatformIO Core: $coreDirectory"
& $pioExecutable --version
if ($LASTEXITCODE -ne 0) {
    Stop-WithMessage "The active PlatformIO executable could not start."
}

$repairScript = Join-Path $PSScriptRoot "repair-platformio-toolchain.ps1"
& $repairScript
if ($LASTEXITCODE -ne 0) {
    Stop-WithMessage "The RISC-V toolchain check failed."
}

$toolBin = Join-Path $coreDirectory "packages\toolchain-riscv32-esp\bin"
$compiler = Join-Path $toolBin "riscv32-esp-elf-g++.exe"
if (-not (Test-Path -LiteralPath $compiler -PathType Leaf)) {
    Stop-WithMessage "The compiler is still missing after repair: $compiler"
}
$env:Path = "$toolBin;$env:Path"

Push-Location $projectRoot
try {
    if (Get-Command git -ErrorAction SilentlyContinue) {
        Write-Host "Git revision:    $(git rev-parse --short HEAD)"
    }
    if ($Clean) {
        & $pioExecutable run -e esp32_c3_lowmem -t clean
        if ($LASTEXITCODE -ne 0) {
            Stop-WithMessage "The clean step failed."
        }
    }
    & $pioExecutable run -e esp32_c3_lowmem
    if ($LASTEXITCODE -ne 0) {
        Stop-WithMessage "The ESP32-C3 build failed."
    }
} finally {
    Pop-Location
}

Write-Host ""
Write-Host "ESP32-C3 build completed successfully. Nothing was flashed."
