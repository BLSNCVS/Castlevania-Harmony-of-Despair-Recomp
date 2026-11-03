# Harmony Recomp

Castlevania: Harmony of Despair (Xbox 360, XBLA) running natively on PC.
Same idea as [Unleashed Recompiled](https://github.com/hedge-dev/UnleashedRecomp).

> You need your own copy of the game. There is no game code, art or executable
> in this repo and there never will be; the game-specific parts are generated
> on your PC from your own console dump. Please don't ask for game files or
> post links to them. Not affiliated with Konami or Microsoft.

## What's in it

- An installer that takes the game, title update and DLC straight from your
  console's files and checks them
- Xbox, PlayStation controllers and keyboard support
- An options menu on F1 or the Guide button: language, controls, volume,
  window mode, 1x–3x internal resolution, V-Sync, aspect ratio, filtering
- All 12 achievements, with icons and a popup, saved locally
- Cheats: unlock every chapter and Hard mode, all items, gold. (You press twice to confirm, so you can't trigger one by accident)

## Status

It plays on Windows. Menus, character select and chapters work. 
On Linux it should build and reach the title screen, but nobody has played it yet, so try it if you dare!

No online play. The farthest I've gone is opening ports and faking Xbox Live to find open lobbies that way, it worked, but I couldn't go much further than that. 
So if someone with real abilities wants to give it a try, feel free to!

Found a bug? Open an issue and attach the newest log from the game's `logs`
folder. The log has your user name in its folder paths; blank it out if you
care.

## Building on Windows

1. Put the source somewhere with a short path, like `C:\hodrecomp`.
2. Run `build-windows.bat`. It offers to install whatever's missing (LLVM,
   CMake, Ninja, VS Build Tools) through winget, asks where your game files
   are, then builds and drops the game in `dist\HarmonyRecomp`.
3. Start `dist\HarmonyRecomp\hodrecomp.exe`.

***You need the game files from Xbox, not PKG from Sony. The XBLA files!***

Needs a DirectX 12 GPU. Options and troubleshooting: [docs/WINDOWS.md](docs/WINDOWS.md).

## Building on Linux

Clang 20 or newer, CMake 3.25+, Ninja and a Vulkan driver. The Debian/Ubuntu
package list is in [docs/GETTING_STARTED.md](docs/GETTING_STARTED.md).

```sh
# 1. Build and install the ReXGlue SDK (once)
./scripts/setup_sdk.sh

# 2. Build the tools
cmake --preset linux-amd64-release
cmake --build out/build/linux-amd64-release --target hodtool
hodtool=out/build/linux-amd64-release/tools/hodtool/hodtool

# 3. Install YOUR game, title update and DLC (docs/DUMPING.md: getting them off your console)
$hodtool install /path/to/your/files --game-dir game

# 4. Collect default.xex and every DLL for the recompiler
$hodtool stage-modules --game game --user-data ~/.local/share/hodrecomp --out modules \
    --manifest hodrecomp_manifest.toml

# 5. Recompile and build. Configure runs codegen.
cmake --preset linux-amd64-release
cmake --build out/build/linux-amd64-release

# 6. Run, or package a self-contained folder
out/build/linux-amd64-release/hodrecomp
cmake --install out/build/linux-amd64-release --prefix dist/HarmonyRecomp
```

[docs/GETTING_STARTED.md](docs/GETTING_STARTED.md) explains each step.
[docs/CONTROLS.md](docs/CONTROLS.md) lists the controls.

## Getting the game files

Copy the game package off your Xbox 360, plus the title update and any DLC if
you have them. [docs/DUMPING.md](docs/DUMPING.md) shows how;
[docs/SUPPORTED_DUMPS.md](docs/SUPPORTED_DUMPS.md) lists the versions that work.

## AI Note

AI was used to help with the coding. Still, the build was tested on different machines, and the code was reviewed. 
Sorry if there are still problems. I do it for the love of the game.

## Repository layout

```
config/                   Codegen manifest template and per-DLL fixes (function bounds, jump tables)
src/                      The PC side: app, hooks, menus, installer
tools/hodtool/            Command-line installer and dump tools, with tests
generated/                Recompiled code goes here (git-ignored, except rexglue.cmake)
game/                     Your extracted game files (git-ignored)
modules/                  default.xex and DLLs gathered for the recompiler (git-ignored)
scripts/                  SDK setup (Linux, Windows) and the one-click Windows build
patches/rexglue-sdk/      Our fixes to the SDK, applied by the setup scripts
assets/                   Fonts, icon, files that ship with the game folder
docs/                     Build guides, dumping, dev notes
```

## Credits

- [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk) (BSD-3-Clause), which does the heavy lifting
- [Xenia](https://github.com/xenia-project/xenia), whose code ReXGlue's runtime is built from
- [XenonRecomp](https://github.com/hedge-dev/XenonRecomp) and
  [Unleashed Recompiled](https://github.com/hedge-dev/UnleashedRecomp), who were a great inspiration for this project
- Free60, for documenting the STFS and XEX formats
