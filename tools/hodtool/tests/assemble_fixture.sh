#!/usr/bin/env sh
# Re-assembles jmp_scan_fixture.bin from jmp_scan_fixture.s using the
# PowerPC binutils that ship in the ReXGlue SDK source tree
# (thirdparty/rexglue-sdk/tools/binutils or $REXGLUE_BINUTILS).
set -eu
here=$(cd "$(dirname "$0")" && pwd)
bin=${REXGLUE_BINUTILS:-$here/../../../thirdparty/rexglue-sdk/tools/binutils}
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
"$bin/powerpc-none-elf-as" -mregnames -mppc64 -o "$tmp/fixture.o" "$here/jmp_scan_fixture.s"
"$bin/powerpc-none-elf-objcopy" -O binary -j .text "$tmp/fixture.o" "$here/jmp_scan_fixture.bin"
echo "wrote $here/jmp_scan_fixture.bin"
