Harmony Recomp
==============

An unofficial PC port of Castlevania: Harmony of Despair (Xbox 360), built
from your own copy of the game. Not affiliated with Konami or Microsoft.
This folder contains code generated from your game files: don't share it.

Playing
-------
Run hodrecomp.exe. If the game isn't installed in the "game" folder yet, an
installer opens first: point it at the files you dumped from your console
(game, title update, DLC) and press Install.

  F1 (or the Guide / PS button)   Options: display mode, resolution,
                                  V-Sync, language, audio, input
  F4                              Advanced settings and key rebinding
  F3                              Performance overlay (frame rate)
  Alt+Enter                       Toggle fullscreen

Controllers (Xbox 360, Xbox One/Series, DualShock 4, DualSense and most
others) work out of the box. The keyboard works too; the default keys are in
CONTROLS.md.

Adding DLC later
----------------
Run "hodrecomp.exe --hod_installer=true" and add the DLC packages, or from a
command prompt in this folder:

  hodtool install-dlc <package file or folder>

DLC characters need their recompiled code, which is built from the DLC's own
files. After adding new character DLC, rebuild with scripts\build_windows.ps1
from the source folder (it reuses everything already built).

Where things are
----------------
  game\          the game's files
  update\        the title update
  user\          saves, DLC and caches (portable mode, see portable.txt)
  hodrecomp.toml settings (created when you change something)
  logs\          log files, useful for bug reports

Troubleshooting
---------------
  - Black screen or crash at start: update your graphics driver. A
    Direct3D 12 capable GPU is required.
  - Stutter the first time an area is seen: shaders are being translated;
    it goes away on later visits.
  - Settings broken: delete hodrecomp.toml to go back to the defaults.
