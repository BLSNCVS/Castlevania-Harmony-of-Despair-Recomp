#!/usr/bin/env python3
"""Runs find_missing_functions.py over every DLL module in the codegen manifests. (Python 3.11+)

For each [[modules]] entry of hodrecomp_manifest.toml and passes/pass*/:
dumps the loaded, patched DLL with 'hodtool dump-image --module', looks for
code reached only through pointers, and writes config/modules/<name>.toml
when it finds any. 'hodtool stage-modules' adds those files to the module's
includes; re-run it and codegen afterwards.

Needs codegen output for the modules (generated/<name>), since functions the
analyzer already registered are not reported.

Builds don't need this: 'hodtool stage-modules' runs the same scan (a C++ port
in tools/hodtool/func_scan.cpp) for every DLL without a reviewed config here,
writing generated/autoconfig/<name>.toml. Use this to review lists before
committing them.

Usage: find_missing_module_functions.py --hodtool <hodtool> [--root <repo>] [--work <dir>]
"""

import argparse
import subprocess
import sys
import tempfile
import tomllib
from pathlib import Path


def manifest_modules(root):
    """(module path under modules/, output name) for every [[modules]] entry."""
    manifests = [root / "hodrecomp_manifest.toml"] + sorted(root.glob("passes/pass*/hodrecomp_manifest.toml"))
    for manifest in manifests:
        data = tomllib.loads(manifest.read_text())
        for module in data.get("modules", []):
            file_path = (manifest.parent / module["file_path"]).resolve()
            name = Path(module["out_directory_path"]).name
            yield file_path.relative_to((root / "modules").resolve()).as_posix(), name


def main():
    p = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    p.add_argument("--hodtool", required=True)
    p.add_argument("--root", default=Path(__file__).resolve().parents[2], type=Path)
    p.add_argument("--work", type=Path, help="folder for the dumped images (default: a temp dir)")
    args = p.parse_args()
    root = args.root.resolve()
    finder = Path(__file__).with_name("find_missing_functions.py")
    out_dir = root / "config" / "modules"
    out_dir.mkdir(parents=True, exist_ok=True)

    with tempfile.TemporaryDirectory() as tmp:
        work = args.work or Path(tmp)
        work.mkdir(parents=True, exist_ok=True)
        total = 0
        for module, name in manifest_modules(root):
            image, layout = work / f"{name}.bin", work / f"{name}.json"
            dump = subprocess.run([args.hodtool, "dump-image", str(root / "modules"), str(image),
                                   "--module", module, "--layout", str(layout)],
                                  capture_output=True, text=True)
            if dump.returncode != 0:
                print(f"{name}: dump failed\n{dump.stderr}", file=sys.stderr)
                return 1
            out = out_dir / f"{name}.toml"
            found = subprocess.run([sys.executable, "-I", str(finder), "--image", str(image),
                                    "--layout", str(layout), "--generated", str(root / "generated" / name),
                                    "--out", str(out), "--quiet"],
                                   capture_output=True, text=True, check=True)
            line = found.stdout.strip()
            if line:
                total += int(line.split()[0])
                print(f"{name}: {line}")
        print(f"{total} unregistered function(s) across all modules")
    return 0


if __name__ == "__main__":
    sys.exit(main())
