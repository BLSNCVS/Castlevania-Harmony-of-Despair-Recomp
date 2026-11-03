# Building and playing on Windows

Windows is the main target. One script does the whole build: it installs the
tools, builds the SDK, installs your game files, recompiles the game and
copies the result to a folder you can play from.

## What you need

- Windows 10 or 11, 64-bit
- A GPU with Direct3D 12 and a current driver. Any NVIDIA GTX 900 / AMD
  RX 400 / Intel Xe or newer card will do.
- About 15 GB of free disk space: the SDK and build files take about 10 GB,
  and the finished game about 2 GB.
- Your own copy of the game, dumped from your console ([DUMPING.md](DUMPING.md)):
  - the game itself. This can be the package file (a long hex name like
    `652844FE…`) or the extracted folder with `default.xex` in it.
  - optionally, title update 2 and any DLC packages.

  Put them all in one folder, in any layout. `.zip` files in that folder are
  extracted automatically. `.7z` and `.rar` files are extracted too if
  [7-Zip](https://www.7-zip.org/) is installed.

## Build

1. Get the source and put it in a **short path** such as `C:\hodrecomp`. Some
   SDK files have very long names, and deep folders hit Windows' 260-character
   path limit.
2. Double-click **`build-windows.bat`**.
   - If build tools are missing, it lists them and offers to install them with
     `winget`. Answer **Y**. Windows asks for permission (UAC) a few times.
     The tools are Git, LLVM/clang, CMake, Ninja and the Visual Studio Build
     Tools with the C++ workload.
   - When it asks where your game files are, drag their folder onto the
     window and press Enter (or press Enter alone to pick it in a folder
     browser).
3. Wait. Typical times on a modern CPU:
   - SDK build: 20–40 minutes, first run only.
   - Game recompile and build: about 25 minutes on 8 cores, 50 on 4. The game
     and its ~90 character and boss DLLs are a few hundred thousand functions
     of generated C++.
4. Play: **`dist\HarmonyRecomp\hodrecomp.exe`**.

The script stops at the first error and says which step failed. Running it
again resumes: it skips work that is already done and only rebuilds what
changed. It also remembers `-OutDir`, `-Config`, `-FidelityFX` and
`-NotPortable` from the last run, so you only pass what changes. To switch a
remembered option off, pass it as `-FidelityFX:$false`.

### Options

From a command prompt in the source folder, for example
`build-windows.bat -Dumps D:\HoD -DesktopShortcut`:

| Option | What it does |
|---|---|
| `-Dumps <folder>` | Folder with your game files. Without it, the first build asks for it. |
| `-InstallPrerequisites` | Install missing tools with winget without asking. Tools you already have are left alone. |
| `-OutDir <folder>` | Put the game somewhere other than `dist\HarmonyRecomp`. |
| `-DesktopShortcut` | Create a desktop shortcut. |
| `-NotPortable` | Keep saves and DLC in `Documents\hodrecomp` instead of the game folder. |
| `-FidelityFX` | Build with AMD FidelityFX for FSR upscaling and CAS sharpening, which then show up under Options → Video. Experimental in the SDK. |
| `-Config RelWithDebInfo` | Include debug info. Use this for crash reports. |
| `-Reinstall` | Install the game files again. |

### Adding DLC later

DLC characters and bosses come with their own code, which has to be
recompiled too. Put the new packages in a folder and run:

```bat
build-windows.bat -Dumps D:\NewDLC
```

The script installs the DLC, recompiles the new DLLs and updates the game
folder.

## The game folder

```
dist\HarmonyRecomp\
  hodrecomp.exe        the game
  *.dll                runtime, GPU backend, recompiled game DLLs
  hodtool.exe          command-line installer (DLC, diagnostics)
  game\  update\       your game files and title update
  user\                saves, DLC, shader cache (portable mode)
  hodrecomp.toml       settings, written when you change something
  logs\                log files
```

The folder is self-contained, including the Visual C++ runtime, so you can
move it anywhere. It contains code generated from your game, so don't share
it.

## Settings

Press **F1** (or the Guide / PS button) in game for the options menu:

- **Window Mode**: windowed or borderless fullscreen. **Alt+Enter** also
  switches.
- **Window Size**: 1280×720 up to 3840×2160.
- **Internal Resolution**: 1x (720p, as on the console) up to 3x.
- **V-Sync** and **Texture Filtering**. There's also an **Upscaling
  Filter** setting in builds made with `-FidelityFX`.
- **Sound** on or muted, **Keyboard Controls** on or off, and **Language**
  (English, Japanese, German, French, Spanish, Italian).

Press **F3** for the frame-rate overlay. If it drops below 60, lower
Internal Resolution first. **F4** opens the SDK's advanced settings,
including key rebinding ([CONTROLS.md](CONTROLS.md)).

## Building by hand (developers)

From a PowerShell window opened inside *x64 Native Tools Command Prompt for
VS 2022* (Start menu; type `powershell` in it). The plain *Developer
PowerShell* is 32-bit by default and links the wrong libraries:

```powershell
.\scripts\setup_sdk.ps1                     # once; -FidelityFX optional
cmake --preset win-amd64-release -DCMAKE_C_COMPILER="C:/Program Files/LLVM/bin/clang.exe" `
      -DCMAKE_CXX_COMPILER="C:/Program Files/LLVM/bin/clang++.exe"
cmake --build out\build\win-amd64-release
```

The game target appears once `modules\default.xex` exists. See
[GETTING_STARTED.md](GETTING_STARTED.md) for `hodtool install` and
`hodtool stage-modules`. `cmake --install out\build\win-amd64-release --prefix <folder>`
lays out a playable folder without game data.

## Troubleshooting

**"winget is not available".** Install *App Installer* from the Microsoft
Store, or install the tools yourself: [Git](https://git-scm.com/download/win),
[LLVM](https://github.com/llvm/llvm-project/releases) (the `LLVM-*-win64.exe`
installer, version 20 or newer), and
[Visual Studio 2022 Build Tools](https://visualstudio.microsoft.com/downloads/)
with **Desktop development with C++**. CMake and Ninja come with the Build
Tools.

**Visual Studio is installed but reported missing.** It lacks the C++
workload. Open the *Visual Studio Installer*, choose **Modify**, tick
**Desktop development with C++**, and run the build again.

**Errors about long paths, "file name too long", or "cannot open" files under
`thirdparty`.** Move the source folder to something short like
`C:\hodrecomp`, delete `thirdparty\rexglue-sdk` and `out`, and run again.
Turning on Windows long path support also helps:
`reg add HKLM\SYSTEM\CurrentControlSet\Control\FileSystem /v LongPathsEnabled /t REG_DWORD /d 1 /f`
in an administrator prompt.

**The build is very slow, or files vanish mid-build.** Antivirus scanning
every generated file can slow the build severalfold and sometimes
quarantines fresh `.dll`/`.exe` files. Add an exclusion for the source folder
under Windows Security → Virus & threat protection → Exclusions.

**"No game package found" when installing.** Check that the folder has the
game itself, not only DLC or the title update. `hodtool install <folder>`
lists what it recognized.

**The game starts with a black screen or closes right away.** Update the GPU
driver. Then look at the newest file in `logs\`. It names the failing step.
Include it when reporting a bug.

**Before sharing a log.** The game's logs and `out\build-windows.log` contain
folder paths, which include your Windows user name (`C:\Users\<name>\...`).
Replace it before posting a log publicly if you'd rather not share it.

**Stutter the first time an area or effect appears.** The GPU shaders are
being translated. It stops once each one has been seen; the cache in `user\`
keeps them for the next session.
