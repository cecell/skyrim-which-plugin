#!/usr/bin/env python3
"""Compiles papyrus/Source/Scripts/WhichPlugin.psc into papyrus/Scripts/WhichPlugin.pex.

    python tools/build_papyrus.py [--game "D:/SteamLibrary/steamapps/common/Skyrim Special Edition"]

Needs the Creation Kit's Papyrus compiler and the game's script sources (Data/Source/Scripts),
which only exist on a machine with the Creation Kit, so the .pex is committed rather than built
in CI. The compiler writes the user and computer name into the .pex header; this script replaces
them (and any folder in the source file name) before the file is kept.
"""
import argparse
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT / "papyrus" / "Source" / "Scripts" / "WhichPlugin.psc"
OUTPUT = ROOT / "papyrus" / "Scripts" / "WhichPlugin.pex"
DEFAULT_GAME = Path("D:/SteamLibrary/steamapps/common/Skyrim Special Edition")
MAGIC = 0xFA57C0DE


def read_string(data: bytes, pos: int) -> tuple[str, int]:
    (length,) = struct.unpack_from(">H", data, pos)
    pos += 2
    return data[pos:pos + length].decode("latin-1"), pos + length


def write_string(text: str) -> bytes:
    raw = text.encode("latin-1")
    return struct.pack(">H", len(raw)) + raw


def scrub(pex: bytes) -> tuple[bytes, list[str]]:
    """Returns the .pex with a neutral header, and the header's old strings."""
    magic, major, minor, game, stamp = struct.unpack_from(">IBBHQ", pex, 0)
    if magic != MAGIC:
        raise SystemExit("not a Skyrim .pex (big-endian FA57C0DE header)")
    pos = struct.calcsize(">IBBHQ")
    source, pos = read_string(pex, pos)
    user, pos = read_string(pex, pos)
    machine, pos = read_string(pex, pos)
    header = struct.pack(">IBBHQ", magic, major, minor, game, stamp)
    header += write_string(Path(source.replace("\\", "/")).name) + write_string("user") + write_string("pc")
    return header + pex[pos:], [source, user, machine]


def check() -> int:
    """Without a compiler (CI): the committed .pex declares every function the .psc does, and its
    header carries no user or computer name."""
    import re
    psc = SOURCE.read_text(encoding="utf-8")
    pex = OUTPUT.read_bytes()
    _, old = scrub(pex)
    problems = [f"header has '{s}', not '{w}' (run tools/build_papyrus.py)" for s, w in zip(old[1:], ("user", "pc")) if s != w]
    for name in re.findall(r"^\s*\S+\s+Function\s+(\w+)\s*\(", psc, re.MULTILINE | re.IGNORECASE):
        if name.encode("latin-1") not in pex:
            problems.append(f"{name} is in the .psc but not the .pex: run tools/build_papyrus.py and commit the .pex")
    for p in problems:
        print(p)
    if not problems:
        print("WhichPlugin.pex matches WhichPlugin.psc")
    return 1 if problems else 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--game", type=Path, default=DEFAULT_GAME, help="the Skyrim Special Edition folder")
    ap.add_argument("--check", action="store_true", help="only check the committed .pex (no compiler needed)")
    a = ap.parse_args()
    if a.check:
        return check()

    compiler = a.game / "Papyrus Compiler" / "PapyrusCompiler.exe"
    scripts = a.game / "Data" / "Source" / "Scripts"
    flags = scripts / "TESV_Papyrus_Flags.flg"
    for needed in (compiler, scripts, flags):
        if not needed.exists():
            raise SystemExit(f"missing {needed} (needs the Creation Kit's Papyrus compiler and script sources)")

    with tempfile.TemporaryDirectory() as tmp:
        cmd = [str(compiler), str(SOURCE), f"-f={flags}", f"-i={SOURCE.parent};{scripts}", f"-o={tmp}"]
        result = subprocess.run(cmd, capture_output=True, text=True)
        print(result.stdout.strip())
        built = Path(tmp) / "WhichPlugin.pex"
        if result.returncode != 0 or not built.exists():
            print(result.stderr.strip(), file=sys.stderr)
            raise SystemExit("compile failed")
        pex, old = scrub(built.read_bytes())

    for text in old[1:]:
        if text and text.encode("latin-1") in pex:
            raise SystemExit(f"'{text}' is still in the .pex after scrubbing its header")
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    OUTPUT.write_bytes(pex)
    print(f"wrote {OUTPUT.relative_to(ROOT)} ({len(pex)} bytes); header names replaced")
    return 0


if __name__ == "__main__":
    sys.exit(main())
