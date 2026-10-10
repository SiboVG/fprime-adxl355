#!/usr/bin/env python3
"""Check that docs/evidence was produced from the current sources.

docs/evidence/source-sha256.json records the SHA-256 of every source, test, and
build file the retained results were generated from. This script fails when any
recorded file changed or disappeared, or when a new file is not yet recorded.
Run with --update after regenerating the evidence files.
"""

import argparse
import hashlib
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MANIFEST = ROOT / "docs/evidence/source-sha256.json"
TRACKED_DIRECTORIES = ("Adxl355", "examples", "tests")
TRACKED_FILES = (
    "library.cmake",
    ".clang-tidy",
    "static-analysis.toml",
    "scripts/static_analysis.py",
    "scripts/install-cppcheck.sh",
    "scripts/clang-tidy",
    ".github/workflows/adxl355.yml",
)
SUFFIXES = {".cpp", ".hpp", ".fpp", ".txt", ".cmake", ".py", ".ini", ".yml"}


def tracked_files() -> list[Path]:
    files = [ROOT / name for name in TRACKED_FILES if (ROOT / name).is_file()]
    for directory in TRACKED_DIRECTORIES:
        for path in (ROOT / directory).rglob("*"):
            if path.is_file() and path.suffix in SUFFIXES and "__pycache__" not in path.parts:
                files.append(path)
    return sorted(files)


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def current_hashes() -> dict[str, str]:
    return {path.relative_to(ROOT).as_posix(): digest(path) for path in tracked_files()}


def problems() -> list[str]:
    if not MANIFEST.is_file():
        return [f"missing manifest: {MANIFEST.relative_to(ROOT)}"]
    recorded = json.loads(MANIFEST.read_text(encoding="utf-8"))
    current = current_hashes()
    found = []
    for name, expected in sorted(recorded.items()):
        if name not in current:
            found.append(f"recorded file missing: {name}")
        elif current[name] != expected:
            found.append(f"changed since evidence was recorded: {name}")
    for name in sorted(set(current) - set(recorded)):
        found.append(f"not recorded in evidence manifest: {name}")
    return found


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--update", action="store_true", help="rewrite the manifest from the current files")
    arguments = parser.parse_args()
    if arguments.update:
        MANIFEST.parent.mkdir(parents=True, exist_ok=True)
        MANIFEST.write_text(json.dumps(current_hashes(), indent=2) + "\n", encoding="utf-8")
        print(f"recorded {len(current_hashes())} files in {MANIFEST.relative_to(ROOT)}")
        return 0
    found = problems()
    for problem in found:
        print(problem, file=sys.stderr)
    if found:
        print("refresh docs/evidence and run check_evidence.py --update", file=sys.stderr)
        return 1
    print(f"evidence manifest matches {len(current_hashes())} files")
    return 0


if __name__ == "__main__":
    sys.exit(main())
