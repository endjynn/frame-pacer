#!/usr/bin/env python3
"""Repository-local, pinned development tools. No system or pip installation."""

from concurrent.futures import ThreadPoolExecutor
import hashlib
import io
import json
import os
from pathlib import Path, PurePosixPath
import platform
import re
import subprocess
import sys
import tarfile
import tempfile
from urllib.request import urlopen
import zipfile


ROOT = Path(__file__).resolve().parent.parent
CACHE = ROOT / ".cache" / "dev-tools"
VERSIONS = json.loads((ROOT / "tools" / "versions.json").read_text())


def checked_path(root, name):
    path = PurePosixPath(name)
    if path.is_absolute() or ".." in path.parts:
        raise ValueError("unsafe tool archive path")
    return root / path


def unpack(data, url, destination, binary):
    # Only regular files are extracted; no links, devices, or archive ownership.
    if url.endswith(".whl"):
        with zipfile.ZipFile(io.BytesIO(data)) as archive:
            for entry in archive.infolist():
                path = checked_path(destination, entry.filename)
                if not entry.is_dir():
                    path.parent.mkdir(parents=True, exist_ok=True)
                    path.write_bytes(archive.read(entry))
    elif url.endswith((".tar.gz", ".tar.xz")):
        with tarfile.open(fileobj=io.BytesIO(data)) as archive:
            for entry in archive:
                path = checked_path(destination, entry.name)
                if entry.isfile():
                    path.parent.mkdir(parents=True, exist_ok=True)
                    with archive.extractfile(entry) as source:
                        path.write_bytes(source.read())
                elif not entry.isdir():
                    raise ValueError("non-regular tool archive member")
    else:
        (destination / binary).write_bytes(data)
    (destination / binary).chmod(0o755)


def install():
    if platform.system() != "Linux" or platform.machine() != "x86_64":
        raise ValueError("pinned tool bootstrap requires Linux x86_64")
    CACHE.mkdir(parents=True, exist_ok=True)
    for name, spec in VERSIONS.items():
        destination = CACHE / (name + "-" + spec["sha256"])
        if not destination.exists():
            print("Downloading " + name + " " + spec["version"], flush=True)
            with urlopen(spec["url"], timeout=120) as response:
                data = response.read()
            if hashlib.sha256(data).hexdigest() != spec["sha256"]:
                raise ValueError("checksum mismatch: " + name)
            with tempfile.TemporaryDirectory(dir=CACHE) as temporary:
                staging = Path(temporary) / "payload"
                staging.mkdir()
                unpack(data, spec["url"], staging, spec["binary"])
                staging.rename(destination)
        executable(name)


def executable(name):
    spec = VERSIONS[name]
    path = CACHE / (name + "-" + spec["sha256"]) / spec["binary"]
    if not path.is_file():
        raise ValueError("missing " + name + "; run make setup-tools first")
    result = subprocess.run([str(path), "--version"], check=True,
                            capture_output=True, text=True)
    if not re.search(r"(?<![\d.])" + re.escape(spec["version"]) + r"(?![\d.])",
                     result.stdout):
        raise ValueError("unexpected " + name + " version")
    return str(path)


def sources(suffixes, directories):
    return sorted(str(path.relative_to(ROOT)) for directory in directories
                  for path in (ROOT / directory).rglob("*")
                  if path.is_file() and path.suffix in suffixes)


def run(arguments, **kwargs):
    subprocess.run(arguments, cwd=ROOT, check=True, **kwargs)


def formatting(write):
    c_files = sources({".c", ".h"}, ["src", "tests"])
    shell_files = sources({".sh"}, ["packaging", "tests", ".github/scripts", "tools"])
    options = ["-i"] if write else ["--dry-run", "--Werror"]
    run([executable("clang-format"), *options, *c_files])
    run([executable("shfmt"), "-ln", "posix", "-i", "4", "-ci",
         "-w" if write else "-d", *shell_files])


def lint():
    shellcheck = executable("shellcheck")
    run([shellcheck, "--severity=style", "--external-sources",
         *sources({".sh"}, ["packaging", "tests", ".github/scripts", "tools"])])
    # actionlint also checks embedded run: shell scripts using this pinned tool.
    run([executable("actionlint"), "-shellcheck", shellcheck,
         *sources({".yml", ".yaml"}, [".github/workflows"])])
    run([sys.executable, "tests/check_workflows.py"])


def tidy():
    tool = executable("clang-tidy")
    run([tool, "--verify-config"])
    database = ROOT / "build" / "quality" / "compile_commands.json"
    entries = json.loads(database.read_text())
    files = sorted({entry["file"] for entry in entries})
    jobs = int(os.environ.get("QUALITY_JOBS", "2"))
    if not 1 <= jobs <= 32:
        raise ValueError("QUALITY_JOBS must be between 1 and 32")

    def analyze(path):
        result = subprocess.run([tool, "-p", str(database.parent), path],
                                cwd=ROOT, capture_output=True, text=True)
        return path, result

    failed = False
    with ThreadPoolExecutor(max_workers=jobs) as pool:
        for path, result in pool.map(analyze, files):
            print("clang-tidy: " + path, flush=True)
            if result.returncode:
                print(result.stdout + result.stderr, flush=True)
                failed = True
    if failed:
        raise ValueError("clang-tidy reported errors")
    print(f"clang-tidy passed: {len(files)} files, {len(entries)} build configurations")


def main():
    os.chdir(ROOT)
    commands = {"setup": install, "format": lambda: formatting(True),
                "check-format": lambda: formatting(False), "lint": lint,
                "tidy": tidy}
    if len(sys.argv) != 2 or sys.argv[1] not in commands:
        raise ValueError("usage: quality.py " + "|".join(commands))
    commands[sys.argv[1]]()


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        sys.exit("quality: " + str(error))
