[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"

function Stop-WithMessage {
    param([string]$Message)

    Write-Error $Message
    exit 1
}

function Repair-NestedToolchain {
    param(
        [string]$Root,
        [string]$Label
    )

    if (-not (Test-Path -LiteralPath $Root -PathType Container)) {
        return $false
    }

    $flatCompiler = Join-Path $Root "bin\riscv32-esp-elf-g++.exe"
    if (Test-Path -LiteralPath $flatCompiler -PathType Leaf) {
        return $true
    }

    $nestedRoot = Join-Path $Root "riscv32-esp-elf"
    $nestedCompiler = Join-Path $nestedRoot "bin\riscv32-esp-elf-g++.exe"
    if (-not (Test-Path -LiteralPath $nestedCompiler -PathType Leaf)) {
        return $false
    }

    Write-Host "Detected an incorrectly nested $Label toolchain."
    Write-Host "Copying its contents to the layout expected by PlatformIO..."
    Get-ChildItem -LiteralPath $nestedRoot -Force | ForEach-Object {
        Copy-Item -LiteralPath $_.FullName -Destination $Root -Recurse -Force
    }

    return (Test-Path -LiteralPath $flatCompiler -PathType Leaf)
}

$pioCommand = Get-Command pio -CommandType Application -ErrorAction SilentlyContinue |
    Select-Object -First 1

if (-not $pioCommand) {
    Stop-WithMessage "No active pio.exe was found. Start a PIOArduino terminal and try again."
}

$pioExecutable = $pioCommand.Source
$scriptsDirectory = Split-Path -Parent $pioExecutable
$penvDirectory = Split-Path -Parent $scriptsDirectory
$coreDirectory = Split-Path -Parent $penvDirectory
$projectRoot = Split-Path -Parent $PSScriptRoot
$packageRoot = Join-Path $coreDirectory "packages\toolchain-riscv32-esp"
$toolSourceRoot = Join-Path $coreDirectory "tools\toolchain-riscv32-esp"
$flatCompiler = Join-Path $packageRoot "bin\riscv32-esp-elf-g++.exe"

Write-Host "PlatformIO executable: $pioExecutable"
Write-Host "PlatformIO Core:       $coreDirectory"
Write-Host "Toolchain package:     $packageRoot"
Write-Host "Toolchain source:      $toolSourceRoot"

# PIOArduino can cache the downloaded toolchain below .platformio\tools and
# then install packages from that local source. Repair that source first so a
# later package reinstall cannot restore the broken nested Windows layout.
if (Test-Path -LiteralPath $toolSourceRoot -PathType Container) {
    if (-not (Repair-NestedToolchain -Root $toolSourceRoot -Label "cached source")) {
        Stop-WithMessage "The cached RISC-V toolchain source exists but no usable compiler was found: $toolSourceRoot"
    }
}

# A clean environment may not have the package installed yet. Let PlatformIO
# resolve/install the environment before attempting any package-level repair.
if (-not (Test-Path -LiteralPath $packageRoot -PathType Container)) {
    Write-Host "RISC-V toolchain package is missing; installing project dependencies..."
    Push-Location $projectRoot
    try {
        & $pioExecutable pkg install -e esp32_c3_lowmem
        if ($LASTEXITCODE -ne 0) {
            Stop-WithMessage "PlatformIO could not install dependencies for esp32_c3_lowmem."
        }
    } finally {
        Pop-Location
    }
}

if (-not (Repair-NestedToolchain -Root $packageRoot -Label "installed package")) {
    $foundCompilers = @()
    if (Test-Path -LiteralPath $packageRoot -PathType Container) {
        $foundCompilers = @(
            Get-ChildItem -LiteralPath $packageRoot -Recurse -File `
                -Filter "riscv32-esp-elf-g++.exe" -ErrorAction SilentlyContinue
        )
    }
    $foundText = if ($foundCompilers.Count) {
        ($foundCompilers.FullName -join [Environment]::NewLine)
    } else {
        "<none>"
    }
    Stop-WithMessage "The RISC-V compiler is unavailable after installation/repair. Found:`n$foundText"
}

if (-not (Test-Path -LiteralPath $flatCompiler -PathType Leaf)) {
    Stop-WithMessage "Repair did not create the expected compiler: $flatCompiler"
}

$toolBin = Join-Path $packageRoot "bin"
$env:Path = "$toolBin;$env:Path"

Write-Host ""
Write-Host "Validating the repaired compiler..."
& $flatCompiler --version
if ($LASTEXITCODE -ne 0) {
    Stop-WithMessage "The compiler exists but could not be executed. Exit code: $LASTEXITCODE"
}

$resolvedCompiler = Get-Command "riscv32-esp-elf-g++" -CommandType Application `
    -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $resolvedCompiler) {
    Stop-WithMessage "The compiler works by absolute path but is still unavailable on PATH."
}

# Verify that GCC resolves the ESP32-C3-specific C++ multilib, not the generic
# fallback library. A malformed directory layout can compile/link successfully
# yet produce firmware that immediately traps with an illegal instruction.
$selectedStdCpp = (& $flatCompiler `
    -march=rv32imc_zicsr_zifencei `
    -mabi=ilp32 `
    '-print-file-name=libstdc++.a' 2>$null | Select-Object -First 1).Trim()
$expectedMultilibFragment = "rv32imc_zicsr_zifencei\ilp32\libstdc++.a"
if (-not $selectedStdCpp -or
    -not (Test-Path -LiteralPath $selectedStdCpp -PathType Leaf) -or
    $selectedStdCpp -notlike "*$expectedMultilibFragment") {
    Stop-WithMessage "ESP32-C3 multilib validation failed. GCC selected '$selectedStdCpp' instead of the rv32imc/ilp32 libstdc++.a. Remove both '$packageRoot' and '$toolSourceRoot', then rerun this script to force a clean download."
}

Write-Host "Selected C3 libstdc++: $selectedStdCpp"
Write-Host ""
Write-Host "Toolchain repair successful."
Write-Host "Resolved compiler: $($resolvedCompiler.Source)"
Write-Host "No project was built or flashed by this script."
