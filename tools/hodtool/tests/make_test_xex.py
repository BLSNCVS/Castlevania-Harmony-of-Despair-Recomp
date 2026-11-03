#!/usr/bin/env python3
"""Builds a tiny synthetic XEX2 around jmp_scan_fixture.bin for testing.

The XEX is unencrypted and uncompressed, with a single PE image containing one
.text section (the fixture) at 0x82001000. It only has to satisfy the SDK's
XexModule loader so 'hodtool scan' can run through
the same code path the real game takes. It contains no game code.

Usage: make_test_xex.py <fixture.bin> <out default.xex> [title_id_hex]
"""

import struct
import sys

IMAGE_BASE = 0x82000000
IMAGE_SIZE = 0x10000            # One 64 KiB page in the 0x82000000 heap.
TEXT_RVA = 0x1000
PDATA_RVA = 0x2000

# (offset, length) of each function in jmp_scan_fixture.bin, mirroring the
# symbol table of jmp_scan_fixture.s. The codegen discovers functions from
# the .pdata exception table, as it does for retail executables.
FIXTURE_FUNCTIONS = [
    (0x000, 0x00C),   # filler
    (0x00C, 0x050),   # savegprlr_14
    (0x05C, 0x0A8),   # setjmp
    (0x104, 0x04C),   # ctx_save
    (0x150, 0x0B4),   # longjmp
    (0x204, 0x04C),   # ctx_restore
]

XEX_HEADER_FILE_FORMAT_INFO = 0x000003FF
XEX_HEADER_ENTRY_POINT = 0x00010100
XEX_HEADER_IMAGE_BASE_ADDRESS = 0x00010201
XEX_HEADER_EXECUTION_INFO = 0x00040006
XEX_SECTION_CODE = 1


def build_pdata():
    pdata = bytearray()
    for offset, length in FIXTURE_FUNCTIONS:
        # PrologLength:8 | FunctionLength:22 (in words) | ThirtyTwoBit:1 | ExceptionFlag:1
        packed = ((length // 4) << 8) | (1 << 30)
        pdata += struct.pack(">II", IMAGE_BASE + TEXT_RVA + offset, packed)
    return bytes(pdata)


def build_pe(code):
    pdata = build_pdata()
    image = bytearray(IMAGE_SIZE)
    # DOS header: 'MZ', e_cblp = 0x90 (the loader checks "MZ\x90\0"), e_lfanew.
    struct.pack_into("<HH", image, 0x00, 0x5A4D, 0x0090)
    struct.pack_into("<I", image, 0x3C, 0x80)
    nt = 0x80
    image[nt:nt + 4] = b"PE\0\0"
    # IMAGE_FILE_HEADER: PowerPC BE, two sections, 32-bit executable.
    struct.pack_into("<HHIIIHH", image, nt + 4, 0x01F2, 2, 0, 0, 0, 0xE0, 0x0102)
    opt = nt + 0x18
    struct.pack_into("<H", image, opt + 0x00, 0x10B)               # PE32 magic
    struct.pack_into("<I", image, opt + 0x10, TEXT_RVA)            # AddressOfEntryPoint
    struct.pack_into("<I", image, opt + 0x1C, IMAGE_BASE)          # ImageBase
    struct.pack_into("<II", image, opt + 0x20, 0x1000, 0x1000)     # Section/File alignment
    struct.pack_into("<II", image, opt + 0x38, IMAGE_SIZE, 0x1000)  # SizeOfImage/Headers
    struct.pack_into("<H", image, opt + 0x44, 14)                  # IMAGE_SUBSYSTEM_XBOX
    struct.pack_into("<I", image, opt + 0x5C, 16)                  # NumberOfRvaAndSizes
    # DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION]
    struct.pack_into("<II", image, opt + 0x60 + 3 * 8, PDATA_RVA, len(pdata))
    sec = opt + 0xE0
    image[sec:sec + 8] = b".text\0\0\0"
    # VirtualSize, VirtualAddress, SizeOfRawData, PointerToRawData, ..., Characteristics
    struct.pack_into("<IIIIIIHHI", image, sec + 8, len(code), TEXT_RVA, len(code), TEXT_RVA,
                     0, 0, 0, 0, 0x60000020)
    sec += 0x28
    image[sec:sec + 8] = b".pdata\0\0"
    struct.pack_into("<IIIIIIHHI", image, sec + 8, len(pdata), PDATA_RVA, len(pdata), PDATA_RVA,
                     0, 0, 0, 0, 0x40000040)
    image[TEXT_RVA:TEXT_RVA + len(code)] = code
    image[PDATA_RVA:PDATA_RVA + len(pdata)] = pdata
    return bytes(image)


def build_xex(code, title_id):
    opt_headers = [
        (XEX_HEADER_FILE_FORMAT_INFO, None),
        (XEX_HEADER_ENTRY_POINT, IMAGE_BASE + TEXT_RVA),
        (XEX_HEADER_IMAGE_BASE_ADDRESS, IMAGE_BASE),
        (XEX_HEADER_EXECUTION_INFO, None),
    ]
    header_len = 0x18 + 8 * len(opt_headers)
    file_format_offset = header_len
    file_format = struct.pack(">IHH", 8, 0, 0)                     # no encryption/compression
    exec_info_offset = file_format_offset + len(file_format)
    # media_id, version, base_version, title_id, platform, exec_table, disc, discs, savegame
    exec_info = struct.pack(">IIIIBBBBI", 0x12345678, 0x10000100, 0, title_id, 0, 0, 1, 1, 0)
    security_offset = exec_info_offset + len(exec_info)
    security = bytearray(0x184 + 0x18)
    struct.pack_into(">II", security, 0x0, len(security), IMAGE_SIZE)
    struct.pack_into(">I", security, 0x110, IMAGE_BASE)            # load_address
    struct.pack_into(">I", security, 0x180, 1)                     # page_descriptor_count
    struct.pack_into(">I", security, 0x184, (1 << 4) | XEX_SECTION_CODE)  # 1 page of code
    header_size = (security_offset + len(security) + 0xFFF) & ~0xFFF

    header = bytearray(header_size)
    struct.pack_into(">4sIIIII", header, 0, b"XEX2", 1, header_size, 0, security_offset,
                     len(opt_headers))
    for i, (key, value) in enumerate(opt_headers):
        if key == XEX_HEADER_FILE_FORMAT_INFO:
            value = file_format_offset
        elif key == XEX_HEADER_EXECUTION_INFO:
            value = exec_info_offset
        struct.pack_into(">II", header, 0x18 + i * 8, key, value)
    header[file_format_offset:file_format_offset + len(file_format)] = file_format
    header[exec_info_offset:exec_info_offset + len(exec_info)] = exec_info
    header[security_offset:security_offset + len(security)] = security
    return bytes(header) + build_pe(code)


def main():
    if len(sys.argv) not in (3, 4):
        print(__doc__)
        return 2
    with open(sys.argv[1], "rb") as f:
        code = f.read()
    title_id = int(sys.argv[3], 16) if len(sys.argv) == 4 else 0x0000CAFE
    with open(sys.argv[2], "wb") as f:
        f.write(build_xex(code, title_id))
    return 0


if __name__ == "__main__":
    sys.exit(main())
