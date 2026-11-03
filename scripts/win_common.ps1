# Shared helpers for the Windows build scripts. Dot-source this file.
# Works in Windows PowerShell 5.1 (built into Windows 10/11) and PowerShell 7.

Set-StrictMode -Version 3
$ErrorActionPreference = 'Stop'

# Bump deliberately, together with REXGLUE_REV in scripts/setup_sdk.sh: the SDK
# API is still moving and codegen output changes between revisions.
$script:RexglueUrl = 'https://github.com/rexglue/rexglue-sdk'
$script:RexglueRev = 'c94f5ebdcb3c9d1a460ca48e04f9758448f8d518'
$script:MinClangMajor = 20
$script:MinCMake = [version]'3.25'

function Write-Step([string]$Message) {
    Write-Host ''
    Write-Host "==> $Message" -ForegroundColor Cyan
}

function Write-Note([string]$Message) {
    Write-Host "    $Message" -ForegroundColor DarkGray
}

function Write-Warn([string]$Message) {
    Write-Host "    $Message" -ForegroundColor Yellow
}

# When set (build_windows.ps1 does), every native command's output is also
# appended to this file. The error lines of a failed command are kept in
# HodrecompFailure for the final message. Globals, so they reach scripts
# called with & (setup_sdk.ps1).
if (-not (Get-Variable -Name HodrecompBuildLog -Scope Global -ErrorAction SilentlyContinue)) {
    $global:HodrecompBuildLog = $null
}
$global:HodrecompFailure = @()

# The part of a failed command's output that says what went wrong: each
# ninja "FAILED:" block or "CMake Error" message, else the last lines.
function Get-FailureExcerpt([string[]]$Lines) {
    $excerpt = @()
    for ($i = 0; $i -lt $Lines.Count -and $excerpt.Count -lt 60; $i++) {
        if ($Lines[$i] -notlike 'FAILED:*' -and $Lines[$i] -notlike 'CMake Error*') { continue }
        $excerpt += $Lines[$i]
        for ($j = $i + 1; $j -lt $Lines.Count -and $j -le $i + 30; $j++) {
            $next = $Lines[$j]
            if ($next -match '^\[\d+/\d+\]' -or $next -like 'ninja: build stopped*' -or $next -like 'FAILED:*') { break }
            if ($next.Length -gt 300) { $next = $next.Substring(0, 150) + ' ...' }  # compiler command
            $excerpt += $next
        }
    }
    if ($excerpt.Count -eq 0) { $excerpt = @($Lines | Select-Object -Last 15) }
    return $excerpt
}

# Runs a native program and stops the script if it fails. Output is shown
# live, logged, and kept for the error message.
function Invoke-Native {
    param(
        [Parameter(Mandatory = $true)][string]$FilePath,
        [string[]]$ArgumentList = @()
    )
    $output = New-Object System.Collections.Generic.List[string]
    # Stderr comes through as error records with 2>&1; don't let them stop
    # the script, the exit code decides.
    $savedPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        & $FilePath @ArgumentList 2>&1 | ForEach-Object {
            # Windows PowerShell turns stderr lines into error records; an
            # empty one would print as "System.Management.Automation.RemoteException".
            if ($_ -is [System.Management.Automation.ErrorRecord]) {
                $line = $_.Exception.Message
            } else {
                $line = "$_"
            }
            $output.Add($line)
            Write-Host $line
        }
        $exitCode = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $savedPreference
    }
    if ($global:HodrecompBuildLog) {
        $header = "> $FilePath $($ArgumentList -join ' ')"
        Add-Content -LiteralPath $global:HodrecompBuildLog -Value (@($header) + $output.ToArray()) -Encoding UTF8
    }
    if ($exitCode -ne 0) {
        $global:HodrecompFailure = @(Get-FailureExcerpt $output.ToArray())
        throw "'$FilePath $($ArgumentList -join ' ')' failed (exit code $exitCode)."
    }
}

# A path the user typed, made absolute against the current PowerShell location
# (not the process directory, which [IO.Path]::GetFullPath would use).
function Resolve-UserPath([string]$Path) {
    return $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Path)
}

# Picks up tools installed since this window opened, keeping everything already
# on PATH (such as the Visual Studio environment).
function Update-SessionPath {
    $have = @($env:Path -split ';' | Where-Object { $_ })
    foreach ($scope in @('Machine', 'User')) {
        $entries = [Environment]::GetEnvironmentVariable('Path', $scope)
        if (-not $entries) { continue }
        foreach ($entry in ($entries -split ';' | Where-Object { $_ })) {
            $expanded = [Environment]::ExpandEnvironmentVariables($entry)
            if ($have -notcontains $expanded) {
                $env:Path = "$env:Path;$expanded"
                $have += $expanded
            }
        }
    }
}

# "clang version N" and the default target from `clang --version`.
function Get-ClangInfo([string]$Clang) {
    $info = @{ Major = 0; Target = '' }
    try {
        $lines = @(& $Clang --version 2>$null)
    } catch {
        return $info
    }
    foreach ($line in $lines) {
        if ($line -match 'clang version (\d+)') { $info.Major = [int]$Matches[1] }
        if ($line -match '^Target:\s*(\S+)') { $info.Target = $Matches[1] }
    }
    return $info
}

# A 64-bit clang new enough for the SDK: LLVM's own install first (the Visual
# Studio-bundled clang is usually older), then whatever is on PATH.
function Find-Clang {
    $candidates = @()
    if ($env:ProgramFiles) { $candidates += (Join-Path $env:ProgramFiles 'LLVM\bin\clang.exe') }
    $onPath = Get-Command clang.exe -ErrorAction SilentlyContinue
    if ($onPath) { $candidates += $onPath.Source }
    foreach ($candidate in $candidates) {
        if (-not (Test-Path -LiteralPath $candidate)) { continue }
        $info = Get-ClangInfo $candidate
        if ($info.Major -ge $script:MinClangMajor -and $info.Target -like 'x86_64-*') {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }
    return $null
}

function Find-VisualStudio {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhere)) { return $null }
    $path = & $vswhere -latest -products * -prerelease `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ($LASTEXITCODE -ne 0 -or -not $path) { return $null }
    return @($path)[0]
}

function Get-CMakeVersion([string]$CMake) {
    try {
        $first = @(& $CMake --version 2>$null)[0]
    } catch {
        return [version]'0.0'
    }
    if ($first -match 'cmake version (\d+\.\d+\.\d+)') { return [version]$Matches[1] }
    return [version]'0.0'
}

# A CMake new enough for the presets. The Visual Studio dev shell puts VS's
# bundled CMake first on PATH, and older VS versions bundle one that's too
# old, so the standalone install is preferred.
function Find-CMake([string]$VsPath) {
    $candidates = @()
    if ($env:ProgramFiles) { $candidates += (Join-Path $env:ProgramFiles 'CMake\bin\cmake.exe') }
    foreach ($cmd in @(Get-Command cmake.exe -All -ErrorAction SilentlyContinue)) { $candidates += $cmd.Source }
    if ($VsPath) {
        $candidates += (Join-Path $VsPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe')
    }
    foreach ($candidate in $candidates) {
        if ((Test-Path -LiteralPath $candidate) -and (Get-CMakeVersion $candidate) -ge $script:MinCMake) {
            return $candidate
        }
    }
    return $null
}

function Find-Ninja([string]$VsPath) {
    $onPath = Get-Command ninja.exe -ErrorAction SilentlyContinue
    if ($onPath) { return $onPath.Source }
    if ($VsPath) {
        $bundled = Join-Path $VsPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'
        if (Test-Path -LiteralPath $bundled) { return $bundled }
    }
    return $null
}

function Find-7Zip {
    $onPath = Get-Command 7z.exe -ErrorAction SilentlyContinue
    if ($onPath) { return $onPath.Source }
    foreach ($base in @($env:ProgramFiles, ${env:ProgramFiles(x86)})) {
        if ($base) {
            $candidate = Join-Path $base '7-Zip\7z.exe'
            if (Test-Path -LiteralPath $candidate) { return $candidate }
        }
    }
    return $null
}

# The build tools that are missing, each with its winget package.
function Get-MissingPrerequisites {
    $vs = Find-VisualStudio
    $missing = @()
    if (-not (Get-Command git.exe -ErrorAction SilentlyContinue)) {
        $missing += @{ Id = 'Git.Git'; Text = 'Git' }
    }
    if (-not (Find-Clang)) {
        $missing += @{ Id = 'LLVM.LLVM'; Text = "LLVM clang $($script:MinClangMajor) or newer, 64-bit" }
    }
    if (-not (Find-CMake $vs)) {
        $missing += @{ Id = 'Kitware.CMake'; Text = "CMake $($script:MinCMake) or newer" }
    }
    if (-not (Find-Ninja $vs)) {
        $missing += @{ Id = 'Ninja-build.Ninja'; Text = 'Ninja' }
    }
    if (-not $vs) {
        $missing += @{
            Id = 'Microsoft.VisualStudio.2022.BuildTools'
            Text = 'Visual Studio 2022 (or newer) or its Build Tools with "Desktop development with C++"'
        }
    }
    return $missing
}

function Write-MissingPrerequisites([object[]]$Missing, [string]$Color) {
    foreach ($item in $Missing) {
        Write-Host "  - $($item.Text) (winget install $($item.Id))" -ForegroundColor $Color
    }
}

# Installs the missing build tools with winget. Tools that are already there
# are left alone: upgrading clang under an existing build breaks its
# precompiled headers.
function Install-Prerequisites {
    $missing = @(Get-MissingPrerequisites)
    if ($missing.Count -eq 0) {
        Write-Note 'All build tools are installed.'
        return
    }
    if (-not (Get-Command winget.exe -ErrorAction SilentlyContinue)) {
        throw ('winget is not available. Install "App Installer" from the Microsoft Store, ' +
               'or install the tools by hand (see docs\WINDOWS.md).')
    }
    $common = @('-e', '--accept-source-agreements', '--accept-package-agreements', '--disable-interactivity')
    foreach ($item in $missing) {
        Write-Step "Installing $($item.Text)"
        if ($item.Id -like 'Microsoft.VisualStudio.*') {
            Write-Note 'This takes a while. The installer window shows its progress.'
            & winget install --id $item.Id @common --override `
                '--wait --passive --norestart --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended' | Out-Host
        } else {
            & winget install --id $item.Id @common | Out-Host
        }
        # Some non-zero codes only mean "already installed"; the tool check
        # that follows decides. Say it here so a real failure has a reason.
        if ($LASTEXITCODE -ne 0) {
            Write-Warn ("winget finished with code 0x{0:X8} for {1}" -f $LASTEXITCODE, $item.Text)
        }
    }
    Update-SessionPath
}

# Finds the tools, enters the Visual Studio x64 developer environment (MSVC
# libraries, Windows SDK, rc.exe) and puts the chosen CMake first on PATH.
# Safe to call more than once. Returns compiler paths in CMake's
# forward-slash form.
function Initialize-BuildEnvironment {
    $missing = @(Get-MissingPrerequisites)
    if ($missing.Count -gt 0) {
        Write-Host ''
        Write-Host 'Missing build tools:' -ForegroundColor Red
        Write-MissingPrerequisites $missing 'Red'
        Write-Host ''
        Write-Host 'Run again with -InstallPrerequisites to install them with winget, or see docs\WINDOWS.md.'
        throw 'Build tools missing.'
    }
    $clang = Find-Clang
    $vs = Find-VisualStudio

    if ($env:VSCMD_ARG_TGT_ARCH -ne 'x64') {
        $devShell = Join-Path $vs 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll'
        Import-Module $devShell
        Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation `
            -DevCmdArguments '-arch=x64 -host_arch=x64 -no_logo' | Out-Null
    }
    if (-not $env:WindowsSdkDir) {
        throw ('The Windows SDK was not found. Add it in the Visual Studio Installer ' +
               '(Modify > Individual components > Windows 11 SDK).')
    }

    $cmake = Find-CMake $vs
    $ninja = Find-Ninja $vs
    if (-not $cmake -or -not $ninja) { throw 'CMake or Ninja not found after setting up Visual Studio.' }
    $env:Path = "$(Split-Path $cmake);$(Split-Path $ninja);$env:Path"

    $clangxx = Join-Path (Split-Path $clang) 'clang++.exe'
    Write-Note "clang:  $clang (version $((Get-ClangInfo $clang).Major))"
    Write-Note "cmake:  $cmake ($(Get-CMakeVersion $cmake))"
    Write-Note "MSVC:   $vs"

    return @{
        Clang   = $clang -replace '\\', '/'
        ClangXX = $clangxx -replace '\\', '/'
    }
}

# The game's DLLs are built against the Visual C++ runtime. Ship it in the
# game folder so it runs on PCs without the redistributable installed.
function Copy-VCRuntime([string]$Destination) {
    $copied = $false
    if ($env:VCToolsRedistDir) {
        $crt = Get-ChildItem -LiteralPath (Join-Path $env:VCToolsRedistDir 'x64') -Directory `
            -Filter 'Microsoft.VC*.CRT' -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($crt) {
            Copy-Item -Path (Join-Path $crt.FullName '*.dll') -Destination $Destination -Force
            $copied = $true
        }
    }
    if (-not $copied) {
        Write-Warn ('Visual C++ runtime DLLs not found; the game needs the Microsoft Visual C++ ' +
                    'Redistributable (x64) on PCs without Visual Studio.')
    }
}

# Deep paths break parts of the SDK checkout and build (Windows' 260-character
# limit), so warn early.
function Test-RootPathLength([string]$Root) {
    if ($Root.Length -gt 60) {
        Write-Host ''
        Write-Host "Warning: the source folder path is long ($($Root.Length) characters):" -ForegroundColor Yellow
        Write-Host "  $Root" -ForegroundColor Yellow
        Write-Host 'Some SDK files may hit the Windows path length limit. If the build fails with' -ForegroundColor Yellow
        Write-Host '"file name too long" or "cannot open", move the folder somewhere short like C:\hodrecomp.' -ForegroundColor Yellow
    }
}

# Extracts a zip, refusing entries that would land outside $Destination
# (absolute paths, '..'), which Expand-Archive doesn't check everywhere.
function Expand-ZipSafely([string]$Zip, [string]$Destination) {
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $separator = [IO.Path]::DirectorySeparatorChar
    $root = [IO.Path]::GetFullPath($Destination).TrimEnd('\', '/') + $separator
    $archive = [IO.Compression.ZipFile]::OpenRead($Zip)
    try {
        foreach ($entry in $archive.Entries) {
            $target = [IO.Path]::GetFullPath((Join-Path $root $entry.FullName))
            if (-not $target.StartsWith($root, [StringComparison]::OrdinalIgnoreCase)) {
                throw "$([IO.Path]::GetFileName($Zip)) contains an unsafe path: $($entry.FullName)"
            }
            if ($entry.FullName.EndsWith('/') -or $entry.FullName.EndsWith('\')) {
                New-Item -ItemType Directory -Force -Path $target | Out-Null
                continue
            }
            New-Item -ItemType Directory -Force -Path ([IO.Path]::GetDirectoryName($target)) | Out-Null
            [IO.Compression.ZipFileExtensions]::ExtractToFile($entry, $target, $true)
        }
    } finally {
        $archive.Dispose()
    }
}
