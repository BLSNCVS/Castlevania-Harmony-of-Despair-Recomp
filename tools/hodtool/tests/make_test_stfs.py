#!/usr/bin/env python3
"""Builds a small, structurally valid STFS (LIVE) package for testing hodtool.

The package contains only synthetic bytes. It mimics the layout of an Xbox
Live Arcade title package closely enough for the SDK's StfsContainerDevice:
read-only format, a single level-0 hash table, one file-table block.

Usage: make_test_stfs.py <out_package> <out_expected_dir>
           [--kind game|dlc|tu] [--title HEX] [--media HEX] [--xex FILE]

--kind game (the default) builds an Arcade title package, dlc a Marketplace
content package and tu a title update carrying default.xexp plus one data
file that overrides the game's. --title defaults to 0000CAFE, --media to
12345678. --xex puts FILE in the package as default.xex instead of filler.

<out_expected_dir> receives the same files as plain host files so the
extracted output can be compared byte-for-byte.
"""

import argparse
import hashlib
import os
import struct
import sys

BLOCK = 0x1000
HEADER_SIZE = 0x971A                    # sizeof(StfsHeader)
DATA_START = (HEADER_SIZE + BLOCK - 1) // BLOCK * BLOCK
END_OF_CHAIN = 0xFFFFFF
TITLE_ID = 0x0000CAFE                   # Deliberately not a real title ID.
CONTENT_TYPE_ARCADE = 0x000D0000
CONTENT_TYPE_DLC = 0x00000002
CONTENT_TYPE_TU = 0x000B0000


def u24le(value):
    return bytes((value & 0xFF, (value >> 8) & 0xFF, (value >> 16) & 0xFF))


def utf16be(text, size):
    raw = text.encode("utf-16-be")
    return raw + b"\0" * (size - len(raw))


def pattern(seed, length):
    out = bytearray()
    counter = 0
    while len(out) < length:
        out += hashlib.sha256(f"{seed}:{counter}".encode()).digest()
        counter += 1
    return bytes(out[:length])


# (path, is_directory, contents). Parents must precede children.
GAME_ENTRIES = [
    ("default.xex", False, b"XEX2" + pattern("xex", 3 * BLOCK + 123)),
    ("data", True, None),
    ("data/stage00.bin", False, pattern("stage", 5000)),
    ("data/empty.bin", False, b""),
    ("readme.txt", False, b"synthetic test package\n"),
]

TU_ENTRIES = [
    ("default.xexp", False, b"XEX2" + pattern("xexp", BLOCK + 7)),
    ("readme.txt", False, b"patched by the synthetic title update\n"),
]

KINDS = {
    "game": (CONTENT_TYPE_ARCADE, "HoD Test Package", GAME_ENTRIES),
    "dlc": (CONTENT_TYPE_DLC, "HoD Test DLC", GAME_ENTRIES),
    "tu": (CONTENT_TYPE_TU, "HoD Test Title Update", TU_ENTRIES),
}


def block_offset(data_block):
    # Read-only packages have one hash table per level; below 170 blocks only
    # the level-0 table at physical block 0 precedes the data.
    assert data_block < 170
    return DATA_START + (data_block + 1) * BLOCK


def build(content_type, display_name, entries, title_id, media_id):
    next_block = 1                      # Data block 0 is the file table.
    hash_entries = {0: END_OF_CHAIN}    # data block -> next block in chain
    blocks = {}
    dir_entries = []
    index_of = {}

    for path, is_dir, contents in entries:
        parent, _, name = path.rpartition("/")
        parent_index = index_of[parent] if parent else 0xFFFF
        index_of[path] = len(dir_entries)
        start = 0
        count = 0
        length = 0
        if not is_dir:
            length = len(contents)
            count = (length + BLOCK - 1) // BLOCK
            start = next_block if count else 0
            for i in range(count):
                block = next_block + i
                blocks[block] = contents[i * BLOCK:(i + 1) * BLOCK]
                hash_entries[block] = block + 1 if i + 1 < count else END_OF_CHAIN
            next_block += count
        flags = len(name) | (0x40 if not is_dir else 0) | (0x80 if is_dir else 0)
        entry = name.encode("ascii").ljust(40, b"\0")
        entry += bytes((flags,))
        entry += u24le(count) + u24le(count) + u24le(start)
        entry += struct.pack(">HI", parent_index, length)
        entry += struct.pack(">HHHH", 0x5A21, 0x6000, 0x5A21, 0x6000)
        assert len(entry) == 0x40
        dir_entries.append(entry)

    file_table = b"".join(dir_entries).ljust(BLOCK, b"\0")
    blocks[0] = file_table
    total_blocks = next_block

    hash_table = bytearray(BLOCK)
    for block, nxt in hash_entries.items():
        info = (2 << 30) | (nxt & 0xFFFFFF)    # state: in use
        digest = hashlib.sha1(blocks[block].ljust(BLOCK, b"\0")).digest()
        hash_table[block * 0x18:block * 0x18 + 0x18] = digest + struct.pack(">I", info)
    struct.pack_into(">I", hash_table, 170 * 0x18, total_blocks)

    header = bytearray(DATA_START)
    header[0:4] = b"LIVE"
    struct.pack_into(">I", header, 0x340, HEADER_SIZE)
    struct.pack_into(">II", header, 0x344, content_type, 2)
    struct.pack_into(">Q", header, 0x34C, total_blocks * BLOCK)
    # xex2_opt_execution_info: media_id, version, base_version, title_id, ...
    struct.pack_into(">IIII", header, 0x354, media_id, 0x10000100, 0, title_id)
    volume = bytes((0x24, 0, 0x01))                         # read-only format
    volume += struct.pack("<H", 1) + u24le(0)               # file table: 1 block @ 0
    volume += hashlib.sha1(bytes(hash_table)).digest()
    volume += struct.pack(">II", total_blocks, 0)
    assert len(volume) == 0x24
    header[0x379:0x379 + 0x24] = volume
    struct.pack_into(">I", header, 0x3A9, 0)                # volume type: STFS
    header[0x411:0x411 + 0x100] = utf16be(display_name, 0x100)
    header[0x1691:0x1691 + 0x80] = utf16be("Synthetic Title", 0x80)

    out = bytearray(header)
    out += hash_table
    for block in range(total_blocks):
        assert len(out) == block_offset(block)
        out += blocks[block].ljust(BLOCK, b"\0")
    return bytes(out)


def main():
    parser = argparse.ArgumentParser(usage=__doc__)
    parser.add_argument("package")
    parser.add_argument("expected_dir")
    parser.add_argument("--kind", choices=sorted(KINDS), default="game")
    parser.add_argument("--title", type=lambda v: int(v, 16), default=TITLE_ID)
    parser.add_argument("--media", type=lambda v: int(v, 16), default=0x12345678)
    parser.add_argument("--xex")
    args = parser.parse_args()

    content_type, display_name, entries = KINDS[args.kind]
    if args.xex:
        with open(args.xex, "rb") as f:
            xex = f.read()
        entries = [(p, d, xex if p == "default.xex" else c) for p, d, c in entries]
    with open(args.package, "wb") as f:
        f.write(build(content_type, display_name, entries, args.title, args.media))
    for path, is_dir, contents in entries:
        dest = os.path.join(args.expected_dir, *path.split("/"))
        if is_dir:
            os.makedirs(dest, exist_ok=True)
        else:
            os.makedirs(os.path.dirname(dest), exist_ok=True)
            with open(dest, "wb") as f:
                f.write(contents)
    return 0


if __name__ == "__main__":
    sys.exit(main())
