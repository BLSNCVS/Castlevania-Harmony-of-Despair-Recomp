# Game notes

Facts about Harmony of Despair's Xbox 360 files that shape the recomp. These
come from inspecting real console packages with `hodtool`. No game data is
reproduced here, only names, sizes and addresses.

## Identity

| | |
|---|---|
| Title ID | `58410A7A` |
| Internal title name | "Castlevania HD" |
| Base game | Arcade title package (`000D0000`) |
| Title update | "Castlevania HD Title Update #1", for media ID `6480C08F`, version field 0.0.1.4 |

## Code is split across many DLLs

`default.xex` (base `0x82000000`, image size `0x980000` according to the title
update's patch header) is only the core engine. Characters, enemies,
projectiles and bosses each live in their own XEX DLL, loaded on demand at a
**fixed base address**:

| Address range | Contents | Examples |
|---|---|---|
| `0x83000000`+, one 512 KB slot each | Playable characters | `data/player_dll/dllAlucard.dll` (base game), `data/player/dllSimon.dll` (DLC) |
| `0x83800000`+ | Projectiles, souls, glyphs, sub-weapons | `dll/shot/dllshotSubFCaxe.dll` |
| `0x88080000`+ | Enemies | `dll/enemy/DLL_FCmedusa.dll` |
| `0x8BF00000` | Bosses, **one shared slot** | `DLL_FcDracula.dll`, `DLL_PsBeel.dll`, `DLL_Region.dll` |

Consequences:

- Every DLL is a `[[modules]]` entry in the manifest and becomes its own
  recompiled shared library. ReXGlue loads it when the game calls
  `XexLoadImage` and unregisters it on unload, so the shared boss slot works as
  long as the game unloads one boss before loading the next (it must on
  hardware too). Codegen can't load two DLLs at one address in a single run,
  so the shared-slot bosses after the first are recompiled in extra passes
  (`passes/pass<N>/`, see GETTING_STARTED.md step 4).
- Recompiled modules are matched by guest path with the device prefix removed,
  lowercased: `data/player/dllsimon.dll`. The 87 DLLs across the 12 DLC packs
  have no path collisions with each other. Collisions with base-game DLLs still
  need checking once the game package is in hand.
- **DLC carries code.** Supporting a DLC character or stage means recompiling
  its DLLs, so a build only supports the DLC it was generated with.
- The title update patches DLLs too: `data/player_dll/dll<Name>.dllp` for the
  six base characters. The SDK applies `<module>p` next to `<module>`
  automatically (for `default.xex` and for every DLL), so overlaying the TU onto
  the game folder is all the installer has to do.

## Title update contents

56 files: `default.xexp` (XEX2 delta patch for the `0x82000000` image), six
character `.dllp` delta patches, and replacement data (`data/text/textres_*.txrc`
for six languages, player equipment tables, a few gimmick/OPD assets,
`ArcadeInfo.xml`).

## DLC catalog

| Display name (from the package) | Package file | Size | DLLs | Character DLL |
|---|---|---|---|---|
| Add Character (Fuma) | `4409D8560E8F59DEDB448006A29C59A848CD5EE358` | 7 MB | 5 | `dllFuma.dll` |
| Add Character (Simon) | `2DFBA40020122B64483033DAAF9ABDCA3D88E1CB58` | 6 MB | 2 | `dllSimon.dll` |
| Add Character (Yoko) | `8DCC2DA228A8A90B6779B50577552677467E899A58` | 8 MB | 1 | `dllYoko.dll` |
| Julius Belmont Character Pack | `6B3288174B104C3F6DAB3745EFD926FAE1633C9158` | 8 MB | 1 | `dllJulius.dll` |
| Maria Rendard Character Pack | `7BCC4D1584357909C3590FD2CF556CB5C461841758` | 8 MB | 1 | `dllMaria.dll` |
| Richter Belmont Character Pack | `790C6B622EF79C9F915DE7B07A9689061C04592858` | 8 MB | 1 | `dllRichter.dll` |
| Extra Stage: Lord Of Flies | `8DB70085912C5829AC27A086CB48D8B39E2744FA58` | 14 MB | 12 | |
| Legion stage pack(EN) ("The One Who Is Many") | `9CF520072EB8FA1F1BFFAE7F2C4CC375043CDA9658` | 30 MB | 13 | |
| Origins stage pack(EN) | `B7ED140B15B0633AF9A9CDC03BA3FCDCAE40188D58` | 29 MB | 51 | |
| Pyramid stage pack(EN) ("Beauty Desire, Situation Dire") | `36DA9EF296B35F21C2470D71D9A21F5F8DFC6DDE58` | 5 MB | 0 | |
| BGM pack1(EN) | `F8DC444EBF6B95EF7B512EBEF08166CF19BE00C058` | 17 MB | 0 | |
| Music Pack 2 | `EFF0D697C13160626B286CDB8845F984F0E3A8C558` | 13 MB | 0 | |

The DLC packages report media ID `00000000` and version 0.0.0.0. Display names
are whatever Konami put in each package, so they're inconsistent.
