# Getting your game files

Harmony of Despair was sold as an Xbox Live Arcade title. On the console it is a
single **STFS package** file (magic `LIVE`) under:

```
Content/0000000000000000/<TitleID>/000D0000/<package file>
```

`000D0000` is the Arcade-title content type. DLC packages for the same title
live in the sibling `00000002` (Marketplace content) folder. A title update, if
installed, is a separate package.

## Copying it from your console

You need a console that has the game downloaded under your account.

1. In the Xbox 360 dashboard's storage settings, copy the game (and any DLC you
   own) to a USB drive configured for Xbox 360 use.
2. The 360 formats that drive with its own file system (FATX), so a PC can't read
   the package directly. Use an Xbox 360 storage tool that can browse FATX
   devices (for example Velocity) to save the package file to your PC. The file
   you want sits under the `Content` path shown above.
3. Check it:

   ```sh
   hodtool info <package>
   ```

   Look for `content type : 000D0000 - Arcade title (XBLA game)` and the game's
   name. The title ID shown is the folder name your package came from.

## What not to do

- Don't download packages, XEX files, or "pre-extracted" folders from the
  internet. Besides being piracy, they're often modified and won't match the
  supported hashes.
- Never commit anything from `game/` or `generated/`. `.gitignore` already
  excludes them; keep it that way in forks.

## Versions

The recompiled code is tied to one exact `default.xex`. Function addresses, jump
tables and every fix in `config/functions.toml` are only valid for that build.
`hodtool extract` prints the SHA-256 of `default.xex`. Compare it against
[SUPPORTED_DUMPS.md](SUPPORTED_DUMPS.md).
