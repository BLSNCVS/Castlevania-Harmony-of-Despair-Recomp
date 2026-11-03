<#
.SYNOPSIS
    Fetches, builds and installs the ReXGlue SDK revision this project is
    developed against, under thirdparty\rexglue-sdk (Windows counterpart of
    setup_sdk.sh).

.DESCRIPTION
    The install registers itself with CMake, so the project's presets find it
    with no extra flags. Re-running is cheap: nothing is rebuilt when the
    installed SDK already matches the pinned revision and options.

.PARAMETER FidelityFX
    Build the SDK with AMD FidelityFX (FSR upscaling and CAS sharpening as
    present effects). Experimental in the SDK.

.PARAMETER InstallPrerequisites
    Install missing build tools with winget first.

.PARAMETER Force
    Rebuild even if the installed SDK looks up to date.
#>
param(
    [switch]$FidelityFX,
    [switch]$InstallPrerequisites,
    [switch]$Force
)

. (Join-Path $PSScriptRoot 'win_common.ps1')

$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$sdk = Join-Path $root 'thirdparty\rexglue-sdk'
$install = Join-Path $sdk 'out\install\win-amd64'
$stamp = Join-Path $install 'hodrecomp-sdk.stamp'
# Local fixes to the pinned SDK, applied in name order after checkout.
$patches = @(Get-ChildItem -Path (Join-Path $root 'patches\rexglue-sdk') -Filter '*.patch' | Sort-Object Name)
$patchHashes = ($patches | ForEach-Object { (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.Substring(0, 12) }) -join ','
$wanted = "$($script:RexglueRev) fidelityfx=$($FidelityFX.IsPresent) patches=$patchHashes"

if ($InstallPrerequisites) { Install-Prerequisites }

if (-not $Force -and (Test-Path -LiteralPath $stamp) -and ((Get-Content -LiteralPath $stamp -Raw).Trim() -eq $wanted)) {
    Write-Host "ReXGlue SDK is up to date ($install)."
    return
}

# Without Developer Mode, Git for Windows checks symlinks out as small text
# files holding the link target. libmspack's sources (used for XEX
# decompression) are symlinks, and clang would compile those one-line files.
# Replace each placeholder with a copy of what it points to.
function Resolve-GitSymlinks([string]$Repo) {
    $dirs = @($Repo)
    $submodules = @(& git -C $Repo submodule foreach --quiet --recursive 'echo $displaypath')
    if ($LASTEXITCODE -ne 0) { throw 'git submodule foreach failed.' }
    foreach ($submodule in $submodules) {
        if ($submodule) { $dirs += (Join-Path $Repo $submodule) }
    }
    $copied = 0
    foreach ($dir in $dirs) {
        $entries = @(& git -C $dir -c core.quotepath=off ls-files -s)
        if ($LASTEXITCODE -ne 0) { throw "git ls-files failed in $dir." }
        foreach ($entry in $entries) {
            if ($entry -notmatch '^120000 ([0-9a-f]+) \d+\t(.+)$') { continue }
            $blob = $Matches[1]
            $link = Join-Path $dir $Matches[2]
            $item = Get-Item -LiteralPath $link -Force -ErrorAction SilentlyContinue
            if ($item -and $item.LinkType) { continue }  # A real symlink: nothing to do.
            $target = (& git -C $dir cat-file -p $blob) -join ''
            $source = [IO.Path]::GetFullPath((Join-Path (Split-Path $link) $target))
            # Only links that stay inside the SDK checkout.
            $inside = [IO.Path]::GetFullPath($Repo).TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
            if (-not $source.StartsWith($inside, [StringComparison]::OrdinalIgnoreCase)) { continue }
            if (-not (Test-Path -LiteralPath $source)) { continue }  # e.g. macOS-only MoltenVK files
            if ($item) { Remove-Item -LiteralPath $link -Recurse -Force }
            Copy-Item -LiteralPath $source -Destination $link -Recurse -Force
            $copied++
        }
    }
    if ($copied -gt 0) { Write-Note "Replaced $copied symlink placeholder(s) with copies of their targets." }
}

Test-RootPathLength $root
Write-Step 'Checking build tools'
$toolchain = Initialize-BuildEnvironment

Write-Step "Fetching ReXGlue SDK $($script:RexglueRev.Substring(0, 10))"
# Some SDK submodules have paths close to Windows' 260-character limit.
$git = @('-c', 'core.longpaths=true', '-c', 'core.autocrlf=false')
function Initialize-SdkRepository {
    if (-not (Test-Path -LiteralPath (Join-Path $sdk '.git'))) {
        New-Item -ItemType Directory -Force -Path $sdk | Out-Null
        Invoke-Native git ($git + @('init', '-q', $sdk))
        Invoke-Native git ($git + @('-C', $sdk, 'remote', 'add', 'origin', $script:RexglueUrl))
    }
    # A run stopped half-way leaves git's lock files behind ("index.lock: File
    # exists"); nothing else uses this copy of the SDK, so they are stale.
    $gitDir = Join-Path $sdk '.git'
    if (Test-Path -LiteralPath $gitDir) {
        # Filter by name with Where-Object: Windows PowerShell ignores -Include
        # here and would list every file.
        $lockNames = @('index.lock', 'shallow.lock', 'HEAD.lock', 'config.lock', 'packed-refs.lock')
        Get-ChildItem -LiteralPath $gitDir -Recurse -Force -File -ErrorAction SilentlyContinue |
            Where-Object { $lockNames -contains $_.Name } |
            ForEach-Object {
                Write-Note "Removing a stale git lock left by an earlier run: $($_.Name)"
                Remove-Item -LiteralPath $_.FullName -Force
            }
    }
    # A run stopped half-way can leave a damaged .gitmodules, which git reads
    # even to fetch. The checkout below restores it.
    $gitmodules = Join-Path $sdk '.gitmodules'
    if (Test-Path -LiteralPath $gitmodules) {
        # git's complaint goes to stderr; Windows PowerShell would turn it
        # into a terminating error, so only the exit code is looked at.
        $savedPreference = $ErrorActionPreference
        $ErrorActionPreference = 'Continue'
        try {
            & git -c core.longpaths=true config -f $gitmodules --list *> $null
            $parsed = $LASTEXITCODE -eq 0
        } finally {
            $ErrorActionPreference = $savedPreference
        }
        if (-not $parsed) {
            Write-Note 'Removing a damaged .gitmodules left by an earlier run'
            Remove-Item -LiteralPath $gitmodules -Force
        }
    }
}
# Whether the SDK checkout is a working git repository.
function Test-SdkRepository {
    $savedPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        & git -C $sdk rev-parse --git-dir *> $null
        return $LASTEXITCODE -eq 0
    } finally {
        $ErrorActionPreference = $savedPreference
    }
}

Initialize-SdkRepository
try {
    Invoke-Native git ($git + @('-C', $sdk, 'fetch', '--depth', '1', 'origin', $script:RexglueRev))
} catch {
    # A network problem: report it, keep the SDK (and its long build).
    if (Test-SdkRepository) { throw }
    # The checkout itself is broken: download it again from scratch.
    Write-Note "The SDK copy is damaged ($($_.Exception.Message)); downloading it again"
    Remove-Item -LiteralPath $sdk -Recurse -Force
    Initialize-SdkRepository
    Invoke-Native git ($git + @('-C', $sdk, 'fetch', '--depth', '1', 'origin', $script:RexglueRev))
}
Invoke-Native git ($git + @('-C', $sdk, 'checkout', '-q', '--force', 'FETCH_HEAD'))
# Remove files added by previously applied patches (the build output in out/
# is git-ignored and kept), so the patches below apply to a clean tree.
Invoke-Native git ($git + @('-C', $sdk, 'clean', '-fdq'))
# --force: drop the symlink copies made below, so the checkout never stops on them.
Invoke-Native git ($git + @('-C', $sdk, 'submodule', 'update', '--init', '--recursive', '--force',
    '--depth', '1', '--jobs', '8'))
Resolve-GitSymlinks $sdk
foreach ($patch in $patches) {
    Write-Note "Applying $($patch.Name)"
    Invoke-Native git ($git + @('-C', $sdk, 'apply', '--whitespace=nowarn', $patch.FullName))
}

Write-Step 'Building ReXGlue SDK (Release; 20-40 minutes the first time)'
$fx = 'OFF'
if ($FidelityFX) { $fx = 'ON' }
Push-Location -LiteralPath $sdk
try {
    Invoke-Native cmake @('--preset', 'win-amd64',
        "-DCMAKE_C_COMPILER=$($toolchain.Clang)",
        "-DCMAKE_CXX_COMPILER=$($toolchain.ClangXX)",
        "-DREXGLUE_ENABLE_FIDELITYFX=$fx")
    Invoke-Native cmake @('--build', 'out/build/win-amd64', '--config', 'Release', '--target', 'install', '--parallel')
} finally {
    Pop-Location
}

Set-Content -Path $stamp -Value $wanted -Encoding ASCII
Write-Host ''
Write-Host "ReXGlue SDK installed to $install" -ForegroundColor Green
