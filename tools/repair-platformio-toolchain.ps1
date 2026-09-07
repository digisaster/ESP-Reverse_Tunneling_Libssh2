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

$pioCommand = Get-Command pio -CommandType Application -ErrorAction SilentlyContinue |
    Select-Object -First 1

if (-not $pioCommand) {
    Stop-WithMessage "No active pio.exe was found. Start a PIOArduino terminal and try again."
}

$pioExecutable = $pioCommand.Source
$scriptsDirectory = Split-Path -Parent $pioExecutable
$penvDirectory = Split-Path -Parent $scriptsDirectory
$coreDirectory = Split-Path -Parent $penvDirectory
$packageRoot = Join-Path $coreDirectory "packages\toolchain-riscv32-esp"
$flatCompiler = Join-Path $packageRoot "bin\riscv32-esp-elf-g++.exe"
$nestedRoot = Join-Path $packageRoot "riscv32-esp-elf"
$nestedCompiler = Join-Path $nestedRoot "bin\riscv32-esp-elf-g++.exe"

Write-Host "PlatformIO executable: $pioExecutable"
Write-Host "PlatformIO Core:       $coreDirectory"
Write-Host "Toolchain package:     $packageRoot"

if (-not (Test-Path -LiteralPath $packageRoot -PathType Container)) {
    Stop-WithMessage "The RISC-V toolchain package is missing: $packageRoot"
}

if (-not (Test-Path -LiteralPath $flatCompiler -PathType Leaf)) {
    if (-not (Test-Path -LiteralPath $nestedCompiler -PathType Leaf)) {
        $foundCompilers = @(
            Get-ChildItem -LiteralPath $packageRoot -Recurse -File `
                -Filter "riscv32-esp-elf-g++.exe" -ErrorAction SilentlyContinue
        )
        $foundText = if ($foundCompilers.Count) {
            ($foundCompilers.FullName -join [Environment]::NewLine)
        } else {
            "<none>"
        }
        Stop-WithMessage "Neither the normal nor nested compiler was found. Found:`n$foundText"
    }

    Write-Host "Detected an incorrectly nested Windows toolchain."
    Write-Host "Copying its contents to the layout expected by PlatformIO..."

    Get-ChildItem -LiteralPath $nestedRoot -Force | ForEach-Object {
        Copy-Item -LiteralPath $_.FullName -Destination $packageRoot -Recurse -Force
    }
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

Write-Host ""
Write-Host "Toolchain repair successful."
Write-Host "Resolved compiler: $($resolvedCompiler.Source)"
Write-Host "No project was built or flashed by this script."
