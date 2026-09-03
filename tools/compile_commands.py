#!/usr/bin/env python3
"""Derive Clang's database from Make's real recipes, including test variants."""

import json
from pathlib import Path
import shlex
import subprocess


ROOT = Path(__file__).resolve().parent.parent
TARGETS = ["check", "benchmark-performance", "vulkan-present-probe",
           "glx-present-probe", "egl-present-probe", "metrics-probe", "pci-probe",
           "thread-cpu-quota-probe", "thread-cpu-quota-controller-integration",
           "thread-cpu-quota-controller-integration-i386", "winepath-probe",
           "dxgi-forward-probe"]


def commands(text):
    entries = []
    for line in text.replace("\\\n", " ").splitlines():
        if not line.startswith(("gcc ", "x86_64-w64-mingw32-gcc ",
                                "i686-w64-mingw32-gcc ")):
            continue
        args = shlex.split(line)
        files = [arg for arg in args[1:] if not arg.startswith("-") and arg.endswith(".c")]
        flags = []
        skip = False
        for arg in args[1:]:
            if skip:
                skip = False
            elif arg == "-o":
                skip = True
            elif arg in files or arg in {"-shared", "-pie", "-municode"}:
                continue
            elif arg.startswith(("-Wl,", "-l", "-L")):
                continue
            else:
                flags.append(arg)
        if "mingw32" in args[0]:
            target = args[0].removesuffix("-gcc")
            flags += ["--target=" + target, "-isystem", "/usr/share/mingw-w64/include"]
        for file in files:
            entries.append({"directory": str(ROOT), "file": file,
                            "arguments": ["clang", *flags, "-c", file]})
    return entries


def main():
    entries = []
    for compiler, targets in [("gcc", TARGETS), ("gcc -m32", ["check-unit"])]:
        result = subprocess.run(
            ["make", "--no-print-directory", "--dry-run", "--always-make",
             "CC=" + compiler, *targets], cwd=ROOT, check=True,
            capture_output=True, text=True)
        entries.extend(commands(result.stdout))
    unique = {json.dumps(entry, sort_keys=True): entry for entry in entries}
    entries = list(unique.values())
    expected = {str(path.relative_to(ROOT)) for directory in ["src", "tests"]
                for path in (ROOT / directory).rglob("*.c")}
    missing = expected - {entry["file"] for entry in entries}
    if missing:
        raise ValueError("C files missing from analysis recipes: " + ", ".join(sorted(missing)))
    destination = ROOT / "build" / "quality" / "compile_commands.json"
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(json.dumps(entries, indent=2) + "\n")
    print(f"Compilation database: {len(expected)} files, {len(entries)} configurations")


if __name__ == "__main__":
    main()
