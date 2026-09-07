[CmdletBinding()]
param(
    [string]$ProjectPath = (Get-Location).Path,
    [string]$OutputPath
)

$ErrorActionPreference = "Continue"
$ProgressPreference = "SilentlyContinue"

try {
    $resolvedProjectPath = (Resolve-Path -LiteralPath $ProjectPath -ErrorAction Stop).Path
} catch {
    Write-Error "Project path does not exist: $ProjectPath"
    exit 1
}

if (-not $OutputPath) {
    $stamp = Get-Date -Format "yyyyMMdd-HHmmss"
    $safeComputerName = if ($env:COMPUTERNAME) { $env:COMPUTERNAME } else { "unknown-computer" }
    $OutputPath = Join-Path $resolvedProjectPath "platformio-diagnostics-$safeComputerName-$stamp.txt"
}

$report = [System.Collections.Generic.List[string]]::new()

function Add-Line {
    param([AllowEmptyString()][string]$Text = "")
    $script:report.Add($Text)
}

function Add-Section {
    param([string]$Title)
    Add-Line ""
    Add-Line ("=" * 78)
    Add-Line $Title
    Add-Line ("=" * 78)
}

function Add-CommandOutput {
    param(
        [string]$Label,
        [scriptblock]$Command
    )

    Add-Line ""
    Add-Line "--- $Label"
    try {
        $output = & $Command 2>&1 | Out-String -Width 4096
        if ([string]::IsNullOrWhiteSpace($output)) {
            Add-Line "<no output>"
        } else {
            Add-Line $output.TrimEnd()
        }
    } catch {
        Add-Line ("ERROR: " + $_.Exception.Message)
    }
}

function Add-FileDetails {
    param(
        [string]$Label,
        [string]$Path
    )

    Add-Line ""
    Add-Line "--- $Label"
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        Add-Line "Not found: $Path"
        return
    }

    try {
        $item = Get-Item -LiteralPath $Path
        $hash = Get-FileHash -LiteralPath $Path -Algorithm SHA256
        Add-Line ("Path: {0}" -f $item.FullName)
        Add-Line ("Size: {0}" -f $item.Length)
        Add-Line ("ModifiedUtc: {0:o}" -f $item.LastWriteTimeUtc)
        Add-Line ("SHA256: {0}" -f $hash.Hash)
    } catch {
        Add-Line ("ERROR: " + $_.Exception.Message)
    }
}

Add-Line "PlatformIO / PIOArduino environment diagnostics"
Add-Line ("GeneratedUtc: {0:o}" -f (Get-Date).ToUniversalTime())
Add-Line ("ProjectPath: {0}" -f $resolvedProjectPath)
Add-Line "This report is read-only apart from writing this text file."

Add-Section "Host and Windows configuration"
Add-Line ("ComputerName: {0}" -f $env:COMPUTERNAME)
Add-Line ("UserName: {0}" -f $env:USERNAME)
Add-Line ("UserProfile: {0}" -f $env:USERPROFILE)
Add-Line ("PowerShell: {0}" -f $PSVersionTable.PSVersion)
Add-Line ("PowerShellEdition: {0}" -f $PSVersionTable.PSEdition)
Add-Line ("ProcessArchitecture: {0}" -f [System.Runtime.InteropServices.RuntimeInformation]::ProcessArchitecture)
Add-Line ("OSArchitecture: {0}" -f [System.Runtime.InteropServices.RuntimeInformation]::OSArchitecture)

try {
    $os = Get-CimInstance Win32_OperatingSystem -ErrorAction Stop
    Add-Line ("Windows: {0} {1} build {2}" -f $os.Caption, $os.Version, $os.BuildNumber)
} catch {
    Add-Line ("Windows query failed: {0}" -f $_.Exception.Message)
}

try {
    $longPaths = Get-ItemPropertyValue `
        -LiteralPath "HKLM:\SYSTEM\CurrentControlSet\Control\FileSystem" `
        -Name "LongPathsEnabled"
    Add-Line ("LongPathsEnabled: {0}" -f $longPaths)
} catch {
    Add-Line ("LongPathsEnabled: unavailable ({0})" -f $_.Exception.Message)
}

Add-Line ("Process PLATFORMIO_CORE_DIR: {0}" -f $env:PLATFORMIO_CORE_DIR)
Add-Line ("User PLATFORMIO_CORE_DIR: {0}" -f [Environment]::GetEnvironmentVariable("PLATFORMIO_CORE_DIR", "User"))
Add-Line ("Machine PLATFORMIO_CORE_DIR: {0}" -f [Environment]::GetEnvironmentVariable("PLATFORMIO_CORE_DIR", "Machine"))
Add-Line ("PYTHONHOME: {0}" -f $env:PYTHONHOME)
Add-Line ("PYTHONPATH: {0}" -f $env:PYTHONPATH)

Add-CommandOutput "PATH entries related to PIO, PlatformIO, or Python" {
    $env:Path -split ";" |
        Where-Object { $_ -match "(?i)(pio|platformio|python)" } |
        ForEach-Object { $_ }
}

Add-Section "Command resolution"
foreach ($commandName in @("pio", "platformio", "python", "py", "code")) {
    Add-CommandOutput "Get-Command $commandName -All" {
        Get-Command $commandName -All -ErrorAction SilentlyContinue |
            Select-Object CommandType, Name, Version, Source
    }.GetNewClosure()
}

Add-CommandOutput "where.exe pio" { where.exe pio }
Add-CommandOutput "where.exe platformio" { where.exe platformio }
Add-CommandOutput "where.exe python" { where.exe python }

Add-Section "Active PlatformIO command"
$activePio = Get-Command pio -CommandType Application -ErrorAction SilentlyContinue |
    Select-Object -First 1

if ($activePio) {
    Add-Line ("ActivePio: {0}" -f $activePio.Source)
    Add-FileDetails "Active pio executable" $activePio.Source
    Add-CommandOutput "pio --version" { & $activePio.Source --version }.GetNewClosure()
    Add-CommandOutput "pio system info" { & $activePio.Source system info }.GetNewClosure()
    Add-CommandOutput "pio pkg list --global" { & $activePio.Source pkg list --global }.GetNewClosure()
} else {
    Add-Line "No active pio application found on PATH."
}

Add-Section "VS Code extensions"
$codeCommand = Get-Command code -CommandType Application -ErrorAction SilentlyContinue |
    Select-Object -First 1
if ($codeCommand) {
    Add-CommandOutput "Relevant extensions from code --list-extensions --show-versions" {
        & $codeCommand.Source --list-extensions --show-versions |
            Where-Object { $_ -match "(?i)(platformio|pioarduino|cpptools|clangd)" }
    }.GetNewClosure()
} else {
    Add-Line "VS Code command-line tool 'code' was not found on PATH."
}

$extensionRoots = @(
    (Join-Path $env:USERPROFILE ".vscode\extensions"),
    (Join-Path $env:USERPROFILE ".vscode-insiders\extensions")
) | Select-Object -Unique

foreach ($extensionRoot in $extensionRoots) {
    Add-CommandOutput "Relevant extension directories under $extensionRoot" {
        if (Test-Path -LiteralPath $extensionRoot) {
            Get-ChildItem -LiteralPath $extensionRoot -Directory -ErrorAction SilentlyContinue |
                Where-Object { $_.Name -match "(?i)(platformio|pioarduino|cpptools|clangd)" } |
                Select-Object Name, FullName, LastWriteTimeUtc
        } else {
            "Directory not found"
        }
    }.GetNewClosure()
}

Add-Section "Known PlatformIO roots and installations"
$candidateRoots = [System.Collections.Generic.List[string]]::new()
foreach ($candidate in @(
    $env:PLATFORMIO_CORE_DIR,
    (Join-Path $env:USERPROFILE ".platformio"),
    "C:\pio",
    "C:\pio-backup"
)) {
    if ($candidate -and -not $candidateRoots.Contains($candidate)) {
        $candidateRoots.Add($candidate)
    }
}

$pioExecutables = [System.Collections.Generic.List[string]]::new()
$compilers = [System.Collections.Generic.List[string]]::new()

foreach ($root in $candidateRoots) {
    Add-Line ""
    Add-Line ("Root: {0} (exists={1})" -f $root, (Test-Path -LiteralPath $root))
    if (-not (Test-Path -LiteralPath $root -PathType Container)) {
        continue
    }

    foreach ($relativePio in @("penv\Scripts\pio.exe", "penv\Scripts\platformio.exe")) {
        $pioPath = Join-Path $root $relativePio
        if ((Test-Path -LiteralPath $pioPath -PathType Leaf) -and -not $pioExecutables.Contains($pioPath)) {
            $pioExecutables.Add($pioPath)
        }
    }

    $toolchainRoot = Join-Path $root "packages\toolchain-riscv32-esp"
    if (Test-Path -LiteralPath $toolchainRoot -PathType Container) {
        Get-ChildItem -LiteralPath $toolchainRoot -Recurse -File `
            -Filter "riscv32-esp-elf-g++.exe" -ErrorAction SilentlyContinue |
            ForEach-Object {
                if (-not $compilers.Contains($_.FullName)) {
                    $compilers.Add($_.FullName)
                }
            }
    }

    foreach ($packageName in @(
        "toolchain-riscv32-esp",
        "framework-arduinoespressif32",
        "framework-arduinoespressif32-libs",
        "tool-esptoolpy",
        "tool-scons"
    )) {
        $manifest = Join-Path $root "packages\$packageName\package.json"
        if (Test-Path -LiteralPath $manifest -PathType Leaf) {
            Add-FileDetails "$root package manifest: $packageName" $manifest
            Add-CommandOutput "$packageName manifest summary" {
                $json = Get-Content -LiteralPath $manifest -Raw | ConvertFrom-Json
                [pscustomobject]@{
                    Name = $json.name
                    Version = $json.version
                    Description = $json.description
                    System = ($json.system -join ",")
                }
            }.GetNewClosure()
        }
    }
}

foreach ($pioExecutable in $pioExecutables) {
    Add-Section "Discovered PIO executable"
    Add-FileDetails "Executable" $pioExecutable
    Add-CommandOutput "$pioExecutable --version" { & $pioExecutable --version }.GetNewClosure()
    Add-CommandOutput "$pioExecutable system info" { & $pioExecutable system info }.GetNewClosure()
}

Add-Section "RISC-V compilers and standard libraries"
if ($compilers.Count -eq 0) {
    Add-Line "No riscv32-esp-elf-g++.exe was found in the known PlatformIO roots."
}

foreach ($compiler in $compilers) {
    Add-FileDetails "Compiler" $compiler
    Add-CommandOutput "$compiler --version" { & $compiler --version }.GetNewClosure()
    Add-CommandOutput "$compiler -dumpmachine" { & $compiler -dumpmachine }.GetNewClosure()
    Add-CommandOutput "$compiler -dumpfullversion" { & $compiler -dumpfullversion }.GetNewClosure()
    Add-CommandOutput "$compiler -print-multi-lib" { & $compiler -print-multi-lib }.GetNewClosure()

    try {
        $standardLibrary = (& $compiler -print-file-name=libstdc++.a 2>$null | Select-Object -First 1).Trim()
        if ($standardLibrary) {
            Add-FileDetails "Default libstdc++.a selected by compiler" $standardLibrary
        }

        $c3StandardLibrary = (& $compiler `
            -march=rv32imc_zicsr_zifencei `
            -mabi=ilp32 `
            -print-file-name=libstdc++.a 2>$null |
            Select-Object -First 1).Trim()
        if ($c3StandardLibrary) {
            Add-FileDetails "ESP32-C3 libstdc++.a selected by compiler" $c3StandardLibrary
        }
    } catch {
        Add-Line ("Unable to resolve libstdc++.a: {0}" -f $_.Exception.Message)
    }
}

Add-Section "Project and Git state"
Add-CommandOutput "platformio.ini" {
    $iniPath = Join-Path $resolvedProjectPath "platformio.ini"
    if (Test-Path -LiteralPath $iniPath) {
        Get-Content -LiteralPath $iniPath
    } else {
        "No platformio.ini in project root"
    }
}.GetNewClosure()

if (Get-Command git -ErrorAction SilentlyContinue) {
    Add-CommandOutput "git status" { git -C $resolvedProjectPath status --short --branch }.GetNewClosure()
    Add-CommandOutput "git HEAD" { git -C $resolvedProjectPath log -1 --format="%H%n%h %s" }.GetNewClosure()
    Add-CommandOutput "git remotes" { git -C $resolvedProjectPath remote -v }.GetNewClosure()
}

if ($activePio -and (Test-Path -LiteralPath (Join-Path $resolvedProjectPath "platformio.ini"))) {
    Add-CommandOutput "Project package list for esp32_c3_lowmem" {
        Push-Location $resolvedProjectPath
        try {
            & $activePio.Source pkg list --environment esp32_c3_lowmem
        } finally {
            Pop-Location
        }
    }.GetNewClosure()
}

Add-Section "Relevant project build artifacts"
$buildRoot = Join-Path $resolvedProjectPath ".pio\build\esp32_c3_lowmem"
foreach ($artifactName in @("firmware.elf", "firmware.bin", "partitions.bin", "bootloader.bin")) {
    Add-FileDetails $artifactName (Join-Path $buildRoot $artifactName)
}

try {
    $outputDirectory = Split-Path -Parent $OutputPath
    if ($outputDirectory -and -not (Test-Path -LiteralPath $outputDirectory)) {
        New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
    }
    $report | Set-Content -LiteralPath $OutputPath -Encoding utf8
    Write-Host "Diagnostics written to: $OutputPath"
} catch {
    Write-Error ("Unable to write diagnostics: {0}" -f $_.Exception.Message)
    exit 1
}
