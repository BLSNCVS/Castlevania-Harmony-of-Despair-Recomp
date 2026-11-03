# Bring-up guide

How to get from "codegen runs" to "the game boots and plays". This is iterative,
hands-on work with a debugger. Every Xbox 360 recomp (Unleashed Recompiled,
re:Blue, the ReXGlue ports) went through these stages. The order below is the one
that usually works.

Everything here needs your own `game/default.xex` ([DUMPING.md](DUMPING.md)).

## 0. Pin the dump

```sh
hodtool extract <package> game          # prints default.xex sha256
```

Add the hash, the title ID and the version `hodtool info` prints to
[SUPPORTED_DUMPS.md](SUPPORTED_DUMPS.md). Every address in `config/` is tied to
that exact file. If you also use a title update (`game/default.xexp`), record
both.

## 1. Clean codegen

```sh
hodtool scan game --write config/jmp.toml
rexglue codegen --force hodrecomp_manifest.toml
```

`--force` writes output even when validation fails, so you can see everything
at once. Work through the errors it reports:

| Error | Usual fix in `config/functions.toml` |
|---|---|
| Unresolved call / branch target | `[functions]` entry with the right `size`/`end`, or a `parent` chunk for split functions |
| `bctr` without a resolved table | `[[switch_tables]]` with the index register and label list (read them off in a disassembler) |
| Code found inside data (strings, float tables) | `[[invalid_instructions]]`, or a `[functions]` size that stops before the data |
| UnimplementedInsn | Rare. Report it upstream to ReXGlue with the opcode |

Check the `hodtool scan` result in a disassembler before trusting it. The
scanner is a heuristic: it ranks functions that spill r13-r31/f14-f31 into a
buffer, and pairs a setjmp with a longjmp that use the same buffer layout. A
wrong `setjmp_address` usually shows up as the game taking its error path right
at boot. (The NHL Legacy port documents exactly that failure.)

Done when `rexglue codegen hodrecomp_manifest.toml` passes **without** `--force`.
The regular build runs codegen without `--force` too.

Useful tools: Ghidra with an XEX loader, or IDA with the SDK's
`scripts/ida` helpers. For a function at `0x82xxxxxx`, the generated C++ in
`generated/default/` has the original disassembly inline as comments.

### Status for the supported dump

Done: `rexglue codegen` passes with no errors on the title-update-patched
`default.xex`, which gives 34,491 functions in 132 C++ files (137 MB) and
211 kernel/XAM imports. What it took and what is still open:

- **Two adjustor thunks** (`0x826609B0`, `0x8266EB68`) are reached only by
  tail branches from other thunks. They're declared in `config/functions.toml`.
- **No setjmp/longjmp.** The game doesn't use them; see `config/jmp.toml` for
  the one longjmp-shaped routine, which is the C++ exception unwinder's context
  restore.
- **`vpkd3d128` float16_4 with pack mode 3** (7 sites around `0x82771164`): the
  SDK warns because it only expects pack mode 2. In Xenia's implementation pack
  modes 2 and 3 behave the same for shifts 0–2, and every site here uses
  shift 0, so the output should be right. *Verify* if vertex data or half-float
  textures look wrong.
- **One very large function** (`0x828A1BA0`, 25 KB of VMX code in `.embsec_`,
  probably the video decoder) produces 1.3 MB of C++ in a file of its own.
  That's harmless, but it's the slowest file to compile.
- **C++ exceptions:** the game links the C++ exception runtime. If gameplay
  ever relies on a `throw` being caught, regenerate with
  `--enable_exception_handlers`.

### First boots

With `default.xex` + `default.xexp` alone (no data files), headless:

1. The XEX loads, imports resolve (all 211 are implemented by the SDK) and the
   main thread runs recompiled code.
2. The first abort was an indirect call into a vcall thunk with no `.pdata`.
   `tools/analysis/find_missing_functions.py` now finds all such pointer-only
   functions (60 in this dump) and writes `config/functions_nopdata.toml`.
3. The game then initializes and starts loading data: `D:\data\arc\shader.arc`,
   `D:\data\arc\font.arc`, and title-update files through `UPDATE:\...`. The
   installer extracts the TU into `update/` beside `game/` and the app mounts
   it, so those resolve.
4. With the full game installed it boots through the Konami logo and ESRB
   screen to the title screen. Start (from the keyboard) is accepted and the
   save-device prompt appears. The title logo was the Japanese one until
   `src/locale.cpp` answered `XGetGameRegion` with a real region instead of
   the SDK's 0xFFFF.
5. **Next: past the save prompt to character select and a stage.** The
   development container renders with Mesa lavapipe (software Vulkan), which
   takes minutes per screen, so this needs real hardware.

6. **First Windows run** (RTX 4070, D3D12): title screen, save creation,
   keyboard and a DualShock 4 all work. Starting a game crashed with
   `Call to invalid or unregistered function at 0x83CA1488`, right after the
   first DLL (`dllshotFCvkshot.dll`) registered. That address is the DLL's
   entry point, which the loader calls and codegen doesn't register; more
   pointer-only functions hide in the DLLs, as they did in `default.xex`.
   `tools/analysis/find_missing_module_functions.py` dumps every module with
   `hodtool dump-image --module ... --layout ...`, finds them (104 across 84
   DLLs, entry points always included) and writes `config/modules/<name>.toml`,
   which `stage-modules` adds to each module's includes.

Character and boss code lives in DLLs the game loads at character select
(`XexLoadImage`). The SDK then loads the recompiled library by the bare
target name codegen gives it (`hodrecomp_dllAlucard`). Windows' loader adds
`.dll` and searches the exe folder, so this works there. On Linux `dlopen`
looks for exactly that file name next to `librexruntime.so` (its RUNPATH is
`$ORIGIN`), so CMakeLists.txt builds the module libraries without the `lib`
prefix and `.so` suffix. If a character fails to load, the log shows
`Failed to load shared library for module`.

Run boots with a short timeout and a large log limit so a fault loop doesn't
rotate the start of the log away:

```sh
timeout -s KILL 10 ./hodrecomp --hod_headless=true --log_level=debug --log_max_file_size_mb=2000
```

## 2. Kernel imports

The game calls into `xboxkrnl.exe` and `xam.xex`. ReXGlue implements most of
what retail games use (it's Xenia's kernel). Codegen resolves each import's
ordinal to a name through the SDK's export table and calls `__imp__<Name>`.
Missing pieces show up in three ways:

- **Codegen error `Cannot resolve ordinal N from <lib>`**: the SDK's export
  table doesn't know that ordinal. Look it up (Xenia's export tables list
  them), then implement it as below.
- **Link errors** for `__imp__<ExportName>`: the export is known but nothing
  implements it. Add one in a new source file under `src/` with
  `REX_EXPORT(name, fn)`, or `REX_EXPORT_STUB_RETURN(name, value)` to start with.
- **`[STUB]` warnings** in the log at runtime: the import exists but does
  nothing. Decide whether the game needs real behavior.

Run with `--log_level=debug` and keep `logs/` from each run.

XBLA titles care about three areas in particular:

- **Licensing**: `XamContentGetLicenseMask`. The app reports the full-game
  license (`--license_mask=1`) by default. If the game still acts like the
  trial (title screen offers "Unlock Full Game", stages locked), find the bits it
  tests and adjust.
- **Profiles/sign-in**: the game expects a signed-in profile for saves.
  Check `XamUserGetSigninState` / `XamUserGetXUID` behaviour in the log.
- **Online**: Harmony of Despair is built around Xbox Live co-op. Make every
  Live/XNet/session call fail cleanly ("not connected") so the game falls back to
  offline play. Never try to reach Microsoft services.

## 3. First frames

The GPU path is the Xenos emulation plugin (`xenos`, staged next to the
executable): Vulkan on Linux, D3D12 on Windows. Recomps rarely look right
immediately:

- Use RenderDoc on the host process to capture frames.
- `--hod_headless=true` skips the GPU entirely. It's useful to check that game
  logic gets past boot before you chase rendering.
- Harmony of Despair is mostly 2D sprites over 3D-ish backgrounds. Watch for
  texture format, EDRAM resolve and blending issues first.

Native rendering (replacing the emulated GPU with hooks on the game's own D3D
calls, like Unleashed Recompiled and re:Blue) is a later, optional milestone.

## 4. Function-level fixes

When recompiled code itself misbehaves (timing loops, busy-waits on hardware
registers, or code that assumed 30 fps), override it:

```cpp
#include <rex/hook.h>

// Replace a function outright (r3/r4 → args, return → r3).
uint32_t MyFix(uint32_t a, uint32_t b) { return a + b; }
REX_HOOK(sub_82123450, MyFix)

// Wrap the original with full register access.
REX_EXTERN(__imp__sub_82123460);
REX_HOOK_RAW(sub_82123460) {
  __imp__sub_82123460(ctx, base);
}
```

Prefer mid-function hooks (`[[midasm_hook]]` in `config/functions.toml`) for
one-instruction patches. Add new source files to `HODRECOMP_SOURCES` in
`CMakeLists.txt`.

## 5. Audio, input, saves

- **Audio** is XMA, decoded by the SDK's FFmpeg-based XMA decoder.
- **Input** goes through SDL3 gamepads. Map keyboard input in the app if wanted.
- **Saves** go under the user data folder (`~/.local/share/hodrecomp` or the
  Windows equivalent). Check that they round-trip across restarts.

## 6. Ship-quality work (later)

Widescreen and resolution options, frame pacing, a first-run installer that
runs `hodtool` for you (like Unleashed Recompiled's), Windows packaging, and
local co-op input.
