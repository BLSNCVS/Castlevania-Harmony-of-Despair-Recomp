# Controls

## Controllers

Plug in and play; nothing to configure. Input goes through SDL3 with its native
(HIDAPI) drivers, which covers:

- Xbox 360 (wired, or wireless with the receiver), Xbox One and Series X|S, over
  USB or Bluetooth
- PlayStation 4 DualShock 4 and PlayStation 5 DualSense, over USB or Bluetooth
- Most other modern pads SDL recognizes (Switch Pro, 8BitDo, Steam Deck)

Buttons map by position, the way the console version expects an Xbox 360 pad:
Cross = A, Circle = B, Square = X, Triangle = Y, L1/R1 = LB/RB, L2/R2 = LT/RT,
Options = Start, Share/Create = Back.

Controller-specific button prompts in game (PlayStation icons) are planned;
see [ROADMAP.md](ROADMAP.md).

## Keyboard

On by default. Keyboard and controller can be used at the same time.

| Xbox 360 control | Default keys |
|---|---|
| Move (left stick) | Arrow keys |
| D-pad | Shift + arrow keys |
| A | Z, Space |
| X | X |
| B | C |
| Y | V |
| LB / RB | A / S |
| LT / RT | Q / W |
| Start | Enter, Esc |
| Back | Backspace, Tab |
| Right stick | I J K L |
| Left / right stick click | F / G |

The table is by controller button. What each button does in game is unchanged
from the console version.

### Rebinding

Press **F4** for the settings overlay. Under **Input → Keybinds → Controller**,
click a binding and press the new key. A binding can list several keys separated
by commas, with optional `Shift+`, `Ctrl+` or `Alt+` prefixes. Settings save to
the config file next to the executable. Pass `--mnk_mode=false` to turn the
keyboard off.

## Language

The game has text for English, Japanese, German, French, Spanish and Italian.
The default is English. Change it in the options menu (F1 → System → Language)
or with `--hod_language=<name>`, then restart. Japanese also switches the title logo to the
Japanese release's, as on a Japanese console.

### Other keys

| Key | Action |
|---|---|
| F1 (controller: Guide / PS button) | Options menu: System, Input, Audio, Video and Cheats. A hint shows it for a few seconds when the game starts. |
| Alt+Enter | Switch between windowed and borderless fullscreen |
| F3 | Performance overlay |
| F4 | Advanced settings (SDK overlay, key rebinding) |
| F7 | Achievements (also from the game's own Achievements menu and Options → System) |
| ` (backtick) | Log console |
