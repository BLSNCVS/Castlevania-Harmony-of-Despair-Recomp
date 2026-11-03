# Contributing

Bug reports, fixes and testing on other hardware all help.

**Don't post game content.** No files from your dump, nothing from
`generated/` or `dist/`, no download links. Anything like that gets deleted.

**Bugs:** use the bug report form and attach your newest log from the game's
`logs` folder (blank out your user name if you want).

**Pull requests:** one change per PR, and say how you tested it. If you touched
`tools/hodtool` or `src/install`, run `ctest --test-dir out/build/<preset>`.
SDK changes go in `patches/rexglue-sdk/` as a new numbered patch. Follow the
style of the code around you.
