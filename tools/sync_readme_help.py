#!/usr/bin/env python3
"""Copies the text of `wp help` from src/WhichPlugin.cpp into README.md, so the two never drift.

    python tools/sync_readme_help.py [--check]

The block replaced is the code block right after the line "This is what `wp help` prints in the
console:". --check only reports whether the README is out of date (exit 1 if it is).
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def help_lines() -> list[str]:
    src = (ROOT / "src" / "WhichPlugin.cpp").read_text(encoding="utf-8")
    cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    version = re.search(r"project\(WhichPlugin VERSION ([0-9.]+)", cmake).group(1)
    body = src[src.index("void PrintHelp()"):]
    body = body[:body.index("\n\t\t}")]
    lines = []
    for m in re.finditer(r'Print\((.*)\);', body):
        # Each argument is C++ string literals, possibly with PLUGIN_VERSION_STRING between them.
        arg = m.group(1).replace("PLUGIN_VERSION_STRING", f'"{version}"')
        text = "".join(re.findall(r'"((?:[^"\\]|\\.)*)"', arg))
        lines.append(re.sub(r"\\(.)", r"\1", text))
    return lines


def main() -> int:
    readme_path = ROOT / "README.md"
    readme = readme_path.read_text(encoding="utf-8")
    marker = "This is what `wp help` prints in the console:\n\n```\n"
    start = readme.index(marker) + len(marker)
    end = readme.index("```", start)
    new = readme[:start] + "\n".join(help_lines()) + "\n" + readme[end:]
    if new == readme:
        print("README help is up to date")
        return 0
    if "--check" in sys.argv:
        print("README help is out of date: run python tools/sync_readme_help.py")
        return 1
    readme_path.write_text(new, encoding="utf-8", newline="\n")
    print("README help updated")
    return 0


if __name__ == "__main__":
    sys.exit(main())
