<#
.SYNOPSIS
    Builds Harmony Recomp on Windows from your own game files, start to finish.

.DESCRIPTION
    1. Checks the build tools (and installs them with -InstallPrerequisites).
    2. Builds the ReXGlue SDK (first run only).
    3. Builds hodtool.
    4. Installs your game, title update and DLC into the output folder.
    5. Recompiles the game and every character/boss DLL, builds hodrecomp.exe.
    6. Copies the program into the output folder, ready to play.

    Run it again after adding DLC or pulling new source: finished steps are
    skipped and only what changed is rebuilt. -OutDir, -Config, -FidelityFX
    and -NotPortable are remembered (in out\build-windows.json), so later runs
    only need the options that change; turn a remembered switch off with
    e.g. -FidelityFX:$false.

.PARAMETER Dumps
    Folder with the files you copied off your console: the game (package or
    extracted folder), the title update and any DLC, in any layout. .zip
    archives in it are extracted automatically, .7z/.rar too if 7-Zip is
    installed. Asked for with a folder picker when the game isn't installed
    yet and this is not given.

.PARAMETER OutDir
    Where the playable game goes. Default: dist\HarmonyRecomp in the source
    folder.

.PARAMETER Config
    Release (default) or RelWithDebInfo (with debug info, for crash reports).

.PARAMETER InstallPrerequisites
    Install missing build tools with winget without asking (Git, LLVM, CMake,
    Ninja, Visual Studio Build Tools with the C++ workload). Tools that are
    already installed are not touched.

.PARAMETER FidelityFX
    Build the SDK with AMD FidelityFX for FSR upscaling / CAS sharpening
    (experimental in the SDK).

.PARAMETER Reinstall
    Install the game files again even if the output folder has them.

.PARAMETER NotPortable
    Keep saves and DLC in Documents\hodrecomp instead of the output folder.

.PARAMETER DesktopShortcut
    Put a shortcut to the game on the desktop.

.EXAMPLE
    .\scripts\build_windows.ps1 -InstallPrerequisites -Dumps D:\HoD
#>
param(
    [string]$Dumps,
    [string]$OutDir,
    [ValidateSet('Release', 'RelWithDebInfo')][string]$Config = 'Release',
    [switch]$InstallPrerequisites,
    [switch]$FidelityFX,
    [switch]$Reinstall,
    [switch]$NotPortable,
    [switch]$DesktopShortcut
)

. (Join-Path $PSScriptRoot 'win_common.ps1')

$started = Get-Date
$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path

# Options not given on the command line come from the previous run, so adding
# DLC later doesn't silently switch the output folder, configuration, SDK
# build or save location.
$settingsFile = Join-Path $root 'out\build-windows.json'
$remembered = @('OutDir', 'Config', 'FidelityFX', 'NotPortable')
if (Test-Path -LiteralPath $settingsFile) {
    try {
        $saved = Get-Content -LiteralPath $settingsFile -Raw | ConvertFrom-Json
    } catch {
        Write-Warn "Ignoring a damaged $settingsFile"
        $saved = [pscustomobject]@{}
    }
    foreach ($name in $remembered) {
        $property = $saved.PSObject.Properties[$name]
        if ($property -and -not $PSBoundParameters.ContainsKey($name)) {
            Set-Variable -Name $name -Value $property.Value
        }
    }
}
if (-not $OutDir) { $OutDir = Join-Path $root 'dist\HarmonyRecomp' }
$OutDir = Resolve-UserPath $OutDir
if ($Dumps) { $Dumps = Resolve-UserPath $Dumps }
$preset = 'win-amd64-' + $Config.ToLowerInvariant()
$buildDir = Join-Path $root "out\build\$preset"
$sdkInstall = Join-Path $root 'thirdparty\rexglue-sdk\out\install\win-amd64'
$gameDir = Join-Path $OutDir 'game'
$userDir = Join-Path $OutDir 'user'
if ($NotPortable) { $userDir = Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'hodrecomp' }

# Asks in the console first: dragging a folder onto the window types its
# path. Enter on its own opens a folder browser. That one gets a visible
# (transparent) topmost owner: owned by a hidden window it can open behind
# the console, where it looks like the build hangs.
function Select-DumpsFolder {
    Write-Host ''
    Write-Host 'Where are your Harmony of Despair files (game, title update, DLC)?' -ForegroundColor Yellow
    Write-Host '  Drag their folder onto this window and press Enter,'
    Write-Host '  or just press Enter to choose it in a folder browser.'
    $answer = (Read-Host 'Folder').Trim().Trim('"')
    if ($answer) { return $answer }
    try {
        Add-Type -AssemblyName System.Windows.Forms
        $owner = New-Object System.Windows.Forms.Form -Property @{
            TopMost = $true; ShowInTaskbar = $false; Opacity = 0; Width = 1; Height = 1
            FormBorderStyle = 'None'; StartPosition = 'CenterScreen'
        }
        $owner.Show()
        $owner.Activate()
        $dialog = New-Object System.Windows.Forms.FolderBrowserDialog
        $dialog.Description = 'Select the folder with your Harmony of Despair files (game, title update, DLC)'
        $dialog.ShowNewFolderButton = $false
        Write-Note "A folder browser opened. If you don't see it, look for it in the taskbar."
        $result = $dialog.ShowDialog($owner)
        $owner.Close()
    } catch {
        throw "Couldn't open a folder browser. Run again with -Dumps <folder>."
    }
    if ($result -ne [System.Windows.Forms.DialogResult]::OK) { throw 'No folder selected.' }
    return $dialog.SelectedPath
}

# Extracts archives found under the dumps folder into a scratch folder and
# returns every folder hodtool should search. An archive that fails to
# extract is skipped with a warning; hodtool reports anything then missing.
function Expand-Dumps([string]$Folder) {
    $sources = @($Folder)
    $scratch = Join-Path $root 'out\dumps-extracted'
    # Unreadable subfolders (system or other users' folders) are skipped.
    $archives = @(Get-ChildItem -LiteralPath $Folder -Recurse -File -ErrorAction SilentlyContinue | Where-Object {
        $name = $_.Name.ToLowerInvariant()
        # First volume only for split archives (.7z.001, .part1.rar).
        if ($name -match '\.part0*(\d+)\.rar$') { return [int]$Matches[1] -eq 1 }
        ($_.Extension -in @('.zip', '.7z', '.rar')) -or ($name -match '\.(7z|zip)\.001$')
    })
    if ($archives.Count -eq 0) { return $sources }

    $sevenZip = Find-7Zip
    $sha = [System.Security.Cryptography.SHA256]::Create()
    foreach ($archive in $archives) {
        # Name plus a hash of the full path: same-named archives in different
        # folders get separate folders.
        $hash = [BitConverter]::ToString(
            $sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($archive.FullName))).Replace('-', '').Substring(0, 8)
        $base = $archive.Name -replace '(\.part0*1)?\.(zip|7z|rar)(\.001)?$', ''
        $dest = Join-Path $scratch "$base-$hash"
        if (Test-Path -LiteralPath (Join-Path $dest '.extracted')) {
            $sources += $dest
            continue
        }
        if ($archive.Extension -ne '.zip' -and -not $sevenZip) {
            Write-Warn "Skipping $($archive.Name): install 7-Zip or extract it yourself."
            continue
        }
        Write-Note "Extracting $($archive.Name)"
        try {
            if ($archive.Extension -eq '.zip') {
                Expand-ZipSafely $archive.FullName $dest
            } else {
                Invoke-Native $sevenZip @('x', '-y', '-bso0', '-bsp0', "-o$dest", $archive.FullName)
            }
        } catch {
            Write-Warn "Couldn't extract $($archive.Name): $($_.Exception.Message)"
            Remove-Item -LiteralPath $dest -Recurse -Force -ErrorAction SilentlyContinue
            continue
        }
        New-Item -ItemType File -Force -Path (Join-Path $dest '.extracted') | Out-Null
        $sources += $dest
    }
    return $sources
}

$logFile = Join-Path $root 'out\build-windows.log'
New-Item -ItemType Directory -Force -Path (Split-Path $logFile) | Out-Null
Set-Content -LiteralPath $logFile -Value "Harmony Recomp Windows build, $(Get-Date -Format s)" -Encoding UTF8
$global:HodrecompBuildLog = $logFile

try {
    Write-Host 'Harmony Recomp - Windows build' -ForegroundColor White
    Write-Note "Source: $root"
    Write-Note "Output: $OutDir"
    Test-RootPathLength $root

    if (-not $InstallPrerequisites) {
        $missing = @(Get-MissingPrerequisites)
        if ($missing.Count -gt 0) {
            Write-Host ''
            Write-Host 'These build tools are missing:' -ForegroundColor Yellow
            Write-MissingPrerequisites $missing 'Yellow'
            $answer = Read-Host 'Install them now with winget? [Y/n]'
            if ($answer -eq '' -or $answer -match '^[Yy]') { $InstallPrerequisites = $true }
        }
    }
    if ($InstallPrerequisites) { Install-Prerequisites }

    Write-Step 'Checking build tools'
    $toolchain = Initialize-BuildEnvironment

    New-Item -ItemType Directory -Force -Path (Split-Path $settingsFile) | Out-Null
    [ordered]@{ OutDir = $OutDir; Config = $Config; FidelityFX = [bool]$FidelityFX; NotPortable = [bool]$NotPortable } |
        ConvertTo-Json | Set-Content -LiteralPath $settingsFile -Encoding UTF8

    # --- ReXGlue SDK -----------------------------------------------------------
    Write-Step 'ReXGlue SDK'
    & (Join-Path $PSScriptRoot 'setup_sdk.ps1') -FidelityFX:$FidelityFX

    $configureArgs = @('--preset', $preset,
        "-DCMAKE_CXX_COMPILER=$($toolchain.ClangXX)",
        "-DCMAKE_PREFIX_PATH=$($sdkInstall -replace '\\', '/')",
        "-DHODRECOMP_PORTABLE=$(if ($NotPortable) { 'OFF' } else { 'ON' })")

    Push-Location -LiteralPath $root
    try {
        # --- hodtool ---------------------------------------------------------------
        Write-Step 'Building hodtool'
        Invoke-Native cmake $configureArgs
        Invoke-Native cmake @('--build', $buildDir, '--target', 'hodtool')
        $hodtool = Join-Path $buildDir 'tools\hodtool\hodtool.exe'

        # --- Game files ------------------------------------------------------------
        $installed = Test-Path -LiteralPath (Join-Path $gameDir 'default.xex')
        if (-not $installed -or $Reinstall) {
            Write-Step 'Installing your game files'
            if (-not $Dumps) { $Dumps = Resolve-UserPath (Select-DumpsFolder) }
            if (-not $Dumps -or -not (Test-Path -LiteralPath $Dumps -PathType Container)) {
                throw "Not a folder: '$Dumps'"
            }
            $sources = Expand-Dumps $Dumps
            Invoke-Native $hodtool (@('install') + $sources + @('--game-dir', $gameDir, '--user-data', $userDir))
        } elseif ($Dumps) {
            # Game already there: add whatever DLC the folder has.
            Write-Step 'Adding DLC'
            $sources = Expand-Dumps $Dumps
            Invoke-Native $hodtool (@('install-dlc') + $sources + @('--user-data', $userDir))
        } else {
            Write-Step 'Game files already installed'
            Write-Note "$gameDir (pass -Reinstall to install again, -Dumps to add DLC)"
        }

        # --- Recompile and build -------------------------------------------------
        Write-Step 'Collecting the game executable and DLLs for the recompiler'
        Invoke-Native $hodtool @('stage-modules', '--game', $gameDir, '--user-data', $userDir,
            '--out', (Join-Path $root 'modules'), '--manifest', (Join-Path $root 'hodrecomp_manifest.toml'))

        Write-Step 'Recompiling (the first build takes a long time: there are ~90 DLLs)'
        # Configure (runs the first codegen), regenerate if the module list
        # changed, then configure again so new DLL targets exist before building.
        Invoke-Native cmake $configureArgs
        Invoke-Native cmake @('--build', $buildDir, '--target', 'hodrecomp_codegen_all')
        Invoke-Native cmake $configureArgs
        Invoke-Native cmake @('--build', $buildDir)

        # --- Package ---------------------------------------------------------------
        Write-Step "Copying the game to $OutDir"
        # A running copy locks its files, and Windows refuses to replace them.
        $outFull = [IO.Path]::GetFullPath($OutDir).TrimEnd('\') + '\'
        while ($true) {
            $running = @(Get-Process -Name 'hodrecomp', 'hodtool' -ErrorAction SilentlyContinue |
                Where-Object { $_.Path -and $_.Path.StartsWith($outFull, [StringComparison]::OrdinalIgnoreCase) })
            if ($running.Count -eq 0) { break }
            Write-Host ''
            Write-Host 'Harmony Recomp is still running from the game folder (maybe in the background).' -ForegroundColor Yellow
            $answer = Read-Host 'Close the game and press Enter, or type K and Enter to end it now'
            if ($answer -match '^[Kk]') { $running | Stop-Process -Force -ErrorAction SilentlyContinue; Start-Sleep -Seconds 1 }
        }
        # Something else (antivirus, a stuck process) can still hold a file.
        # Windows lets a locked file be renamed, so move those aside; the copy
        # then writes fresh ones. Leftovers from earlier runs are removed when free.
        # Only files this script made or is about to write are touched: the
        # output folder may be one the player also uses for other things.
        if (Test-Path -LiteralPath $OutDir) {
            Get-ChildItem -LiteralPath $OutDir -File -ErrorAction SilentlyContinue |
                Where-Object { $_.Name -match '^[^\\]+\.(exe|dll)\.\d{14}\.old$' } |
                ForEach-Object { Remove-Item -LiteralPath $_.FullName -Force -ErrorAction SilentlyContinue }
            $toWrite = @(Get-ChildItem -LiteralPath $buildDir -File -ErrorAction SilentlyContinue |
                Where-Object { $_.Extension -in '.exe', '.dll' } | ForEach-Object { $_.Name }) + 'hodtool.exe'
            Get-ChildItem -LiteralPath $OutDir -File -ErrorAction SilentlyContinue |
                Where-Object { $toWrite -contains $_.Name } | ForEach-Object {
                    try {
                        $stream = [IO.File]::Open($_.FullName, 'Open', 'ReadWrite', 'None')
                        $stream.Close()
                    } catch {
                        $aside = '{0}.{1}.old' -f $_.Name, (Get-Date -Format 'yyyyMMddHHmmss')
                        try {
                            Rename-Item -LiteralPath $_.FullName -NewName $aside -ErrorAction Stop
                            Write-Note "$($_.Name) was in use; moved it aside as $aside"
                        } catch {
                            Write-Note "$($_.Name) is in use and could not be moved aside"
                        }
                    }
                }
        }
        Invoke-Native cmake @('--install', $buildDir, '--prefix', $OutDir)
        if ($NotPortable) {
            Remove-Item -LiteralPath (Join-Path $OutDir 'portable.txt') -ErrorAction SilentlyContinue
        }
        Copy-VCRuntime $OutDir
    } finally {
        Pop-Location
    }

    if ($DesktopShortcut) {
        $shell = New-Object -ComObject WScript.Shell
        $link = $shell.CreateShortcut((Join-Path ([Environment]::GetFolderPath('Desktop')) 'Harmony Recomp.lnk'))
        $link.TargetPath = Join-Path $OutDir 'hodrecomp.exe'
        $link.WorkingDirectory = $OutDir
        $link.Save()
        Write-Note 'Desktop shortcut created.'
    }

    $elapsed = (Get-Date) - $started
    Write-Host ''
    Write-Host ('Done in {0:hh\:mm\:ss}. Play: {1}' -f $elapsed, (Join-Path $OutDir 'hodrecomp.exe')) -ForegroundColor Green
} catch {
    Write-Host ''
    Write-Host "Build failed: $($_.Exception.Message)" -ForegroundColor Red
    # Also in the log, for failures outside a native command.
    try {
        Add-Content -LiteralPath $logFile -Encoding UTF8 -Value @(
            "Build failed: $($_.Exception.Message)",
            "At: $($_.InvocationInfo.PositionMessage)")
    } catch {}
    if ($global:HodrecompFailure.Count -gt 0) {
        Write-Host ''
        Write-Host 'The error:' -ForegroundColor Red
        foreach ($line in $global:HodrecompFailure) { Write-Host "  $line" }
    }
    Write-Host ''
    Write-Host "Full log: $logFile"
    Write-Host 'Send that file (or the error above) when reporting a problem; see docs\WINDOWS.md.'
    exit 1
}
