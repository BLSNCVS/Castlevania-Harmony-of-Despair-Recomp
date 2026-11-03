# ReXGlue SDK patches

`scripts/setup_sdk.sh` and `scripts/setup_sdk.ps1` apply these to the pinned
SDK revision after checkout, in name order. The Windows script rebuilds the SDK
when a patch changes; the project's configure step then regenerates all code,
since patched codegen templates change the generated sources.

- `0001-host-function-tables.patch`: keeps each module's indirect-call
  lookup table in host memory instead of guest memory right after the module's
  image. Harmony of Despair loads its character, boss and shot DLLs at fixed
  addresses packed 0x80000 apart, often right after the main executable or another
  DLL. A guest-memory table after one image covered the next DLL's slot, so
  those DLLs failed with "Module image range ... overlaps existing" and the game
  crashed when a chapter loaded.
- `0002-achievements-ui-and-vibration.patch`: `XamShowAchievementsUI` calls
  an app-provided screen (`KernelState::SetAchievementsUIHandler`) instead of
  doing nothing, so the game's Achievements menu entry works; adds the
  `input_vibration` setting.
- `0003-retire-dlls-sharing-a-base-address.patch`: three DLC bosses
  (`DLL_FCdracula`, `DLL_Region`, `DLL_PSbeel`) share base address 8BF00000.
  The game loads the next chapter's DLLs before freeing the previous ones,
  which works on a console because it relocates a DLL whose base is taken.
  Recompiled code can't move, so the old DLL is retired when another loads
  over it; the game frees it right after anyway. Without this, starting
  Origins, The One Who Is Many or Lord of Flies after another of them crashed.
- `0004-one-sdl-and-module-identity.patch`:
  - Builds SDL3 as a shared library. With the static one, the SDK's DLL and
    the game executable each had their own SDL on Windows, and the game's
    copy was never initialized: the app's menus saw no controllers.
  - Remembers which recompiled code each loaded DLL is wired to. Unloading
    looked it up by the DLL's resolved device path, which never matched, so
    unloads left the code registered (and 0003's eviction could not clear
    it). A DLL requested again by another path (e.g. the title update's
    copy) now reuses the loaded module instead of failing the load.
- `0005-harden-stfs-parsing.patch`: the STFS reader treats a package's file
  table as untrusted, since the installer opens whatever packages the player
  points it at. A parent directory index must name an earlier directory, a
  name can't run past its 40-byte field, a block chain can't be longer than
  the entry's block count (a looping chain made one block stand for
  gigabytes), a damaged chain or unreadable hash table is an error instead
  of an assert or a null dereference, and files under 4 bytes aren't read
  for a magic number.
- `0006-retired-tables-and-ui-handler-lock.patch`: two lifetime fixes for
  the patches above. A retired module's host function table (0001, 0003) is
  kept until shutdown instead of freed, since a guest thread may still be
  finishing that module's code and look up an indirect call. The
  achievements UI handler (0002) is set and read under a lock, and readers
  get a copy: the UI thread clears it at shutdown while game threads read it.
