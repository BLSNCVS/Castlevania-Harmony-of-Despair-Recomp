# Getting started

## Requirements

- CMake 3.25+ and Ninja
- **Clang 20 or newer.** The SDK and the generated code need it; GCC and MSVC's
  `cl` are not supported.
- A Vulkan-capable GPU (Linux) or a D3D12-capable GPU (Windows)
- Your own dump of Castlevania: Harmony of Despair for Xbox 360 ([DUMPING.md](DUMPING.md))

### Linux (Debian/Ubuntu package names)

```sh
sudo apt install clang-20 lld-20 cmake ninja-build git python3 autoconf \
    libgtk-3-dev libx11-xcb-dev libxss-dev libvulkan-dev libwayland-dev \
    wayland-protocols libxkbcommon-dev libdecor-0-dev libasound2-dev \
    libpulse-dev libpipewire-0.3-dev
```

### Windows

For a one-click build, see [WINDOWS.md](WINDOWS.md): `build-windows.bat`
installs the tools and runs every step below. Doing it by hand takes:

- LLVM clang 20+ (`winget install LLVM.LLVM`), CMake 3.25+ and Ninja
- Visual Studio 2022 or its Build Tools with **Desktop development with C++**
  (MSVC libraries and the Windows SDK; clang links against them)
- A *Developer PowerShell for VS* (x64) for the commands below

## 1. Build the ReXGlue SDK

Linux:

```sh
./scripts/setup_sdk.sh
```

The script checks out the pinned SDK revision under `thirdparty/rexglue-sdk`,
builds it in Release, and installs it. The install registers itself in CMake's
package registry, so the project finds it without extra flags.

Windows:

```powershell
.\scripts\setup_sdk.ps1
```

It does the same with the `win-amd64` preset and LLVM's clang.

To use an SDK installed somewhere else, pass `-DREXSDK_DIR=<sdk source tree>`
(builds the SDK as part of this project) or put its install prefix on
`CMAKE_PREFIX_PATH`.

## 2. Build hodtool

```sh
cmake --preset linux-amd64-release        # or win-amd64-release
cmake --build out/build/linux-amd64-release --target hodtool
ctest --test-dir out/build/linux-amd64-release   # optional: tool self-tests
```

Until `game/default.xex` exists, configure only sets up the tools and prints a
note saying so. That's expected.

## 3. Install your game files

Put everything you copied off your console (game package, title update, DLC)
in one folder, in any layout, and run:

```sh
hodtool install <that folder> --game-dir game
```

It searches the folder, identifies each package by its header, then:

- extracts the game into `game/`
- overlays the newest title update on top. The SDK applies its
  `default.xexp` and `.dllp` patches automatically whenever a module loads,
  for both codegen and the game. It also keeps a full copy in `update/` beside
  `game/`, which the app mounts as `UPDATE:` (the game reads some
  title-update files through that drive).
- installs DLC into the runtime's user data folder (`~/.local/share/hodrecomp`
  on Linux), with the license records the game checks

It also prints the SHA-256 of `default.xex`. Compare it with
[SUPPORTED_DUMPS.md](SUPPORTED_DUMPS.md). `hodtool info <package>` shows what a
single file is. Players get the same steps as a first-run wizard in the app.

## 4. Generate and build

```sh
hodtool stage-modules --game game --user-data <user data> \
    --out modules --manifest hodrecomp_manifest.toml
cmake --preset linux-amd64-release
cmake --build out/build/linux-amd64-release
```

`stage-modules` copies `default.xex` and every code module into `modules/`:
the six base-game character DLLs, and the characters, enemies, projectiles
and bosses from installed DLC, each with its title-update patch. It also
writes `hodrecomp_manifest.toml` (git-ignored): the template in
`config/manifest_base.toml` plus one `[[modules]]` entry per DLL. Each DLL becomes
its own recompiled shared library, loaded when the game loads the DLL.

Some bosses share one address (`0x8BF00000`), and codegen can't load two
DLLs at the same address in one run. `stage-modules` keeps the first in the
main manifest and puts each other one in an extra codegen pass under
`passes/pass<N>/` (git-ignored). CMake builds every pass alongside the main
manifest ([cmake/codegen_passes.cmake](../cmake/codegen_passes.cmake)), and
the app registers their libraries at startup.

The first configure after staging runs `rexglue codegen` to produce
`generated/`. Later builds re-run codegen whenever its inputs change.

## 5. Run

```sh
out/build/linux-amd64-release/hodrecomp
```

The executable finds `game/` next to itself or in the source tree. Use
`--game_data_root <dir>` to point it elsewhere. Useful flags:

| Flag | Effect |
|---|---|
| `--log_level=debug` | Verbose log (also written to `logs/` next to the executable) |
| `--license_mask=<n>` | Override the XBLA license reported to the game (default 1 = full game) |
| `--hod_headless=true` | Skip GPU emulation (bring-up and CI only) |
| `F3` / `` ` `` / `F4` | Debug overlay / log console / settings |
