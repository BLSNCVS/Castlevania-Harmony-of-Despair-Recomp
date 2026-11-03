# Supported dumps

The recompiled code, `config/jmp.toml` and `config/functions.toml` only fit one
exact executable. Like Unleashed Recompiled, this port targets the game **with
its title update applied**: the SDK patches `default.xex` with `default.xexp`
at load time, and codegen runs on the patched image.

| Release | Title ID | Media ID | `default.xex` (base) | Title update `default.xexp` | Patched version | Status |
|---|---|---|---|---|---|---|
| World (XBLA) | `58410A7A` | `6480C08F` | 0.0.1.4, SHA-256 `0d625b8143e6f25aca7fa483fbd4b649cd01bcd446bb26b78a0ee25b46e66dd8` | "Castlevania HD Title Update #1" (the "TU 2" package), SHA-256 `4228a979f7ff7542f56b337e6606e7b4d2ff6bd228ce480034a54cf7dbd0bd27` | 0.0.2.4 | Codegen clean |

Patched image layout: base `0x82000000`, size `0x980000`, `.text`
`0x82220000`–`0x8287368C`, plus eight executable `.embsec_` sections up to
`0x828CC8C8`. The original PE name is `dcHD_Master.exe`, built with XDK 2.0.9328.

Get these values with:

```sh
hodtool info <package>            # title ID, media ID, version
hodtool extract <package> game    # default.xex / default.xexp SHA-256
hodtool dump-image game out.bin   # patched image for disassemblers
```
