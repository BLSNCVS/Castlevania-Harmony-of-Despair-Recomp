# Roadmap

| # | Milestone | Done when | Status |
|---|---|---|---|
| 0 | Tooling and scaffold | SDK builds; `hodtool` tested; codegen→build→launch proven on a synthetic XEX | **Done** |
| 0b | Installer | First-run wizard installs game, title update and DLC from console packages | **Done** (tested on real TU and DLC) |
| 0c | Input | Keyboard on by default with a 2D layout; Xbox and PlayStation pads via SDL3 | **Done** |
| 1 | Dump pinned | `default.xex` hash and version recorded in SUPPORTED_DUMPS.md | **Done** (base 0.0.1.4 + TU, patched 0.0.2.4) |
| 2 | Clean codegen | `rexglue codegen` passes without `--force`; `config/jmp.toml` verified | **Done**: `default.xex` + all 93 DLLs (overlapping bosses in extra codegen passes) |
| 3 | Boots headless | `--hod_headless=true` reaches the title screen logic without crashing | **Done** |
| 4 | First frame | Konami logo / title screen renders through the xenos plugin | **Done**: Konami logo, title screen, Start accepted from the keyboard, save prompt shown |
| 5 | Playable single player | Castle stages load and play; audio; gamepad input | **Done** (played on Windows) |
| 6 | Saves and DLC | Progress persists; installed DLC characters/chapters show up | |
| 7 | Polish | Resolution/widescreen, frame pacing, in-game options menu, PlayStation button prompts | Options menu, borderless fullscreen, resolution scale and Alt+Enter done. Frame pacing is open, and so are PS prompts: the icons are textures in the game's `.arc` archives, so this needs that format decoded. |
| 8 | Local co-op (stretch) | Multiple local controllers mapped to players | |
| W | Windows | One-click build from source to a playable, portable folder | **Done** (built and played on two Windows PCs) |

Online play over Xbox Live is out of scope.

See [BRINGUP.md](BRINGUP.md) for how each step is approached.
