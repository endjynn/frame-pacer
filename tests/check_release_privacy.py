#!/usr/bin/env python3
"""Reject identifying or credential-like data without echoing matched values."""

from __future__ import annotations

import os
from pathlib import Path
import re
import socket
import sys


def fail(category: str, path: Path, root: Path, count: int) -> None:
    relative = path.relative_to(root)
    print(f"privacy check: {category}: {relative}: {count} match(es)", file=sys.stderr)


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: check_release_privacy.py DIRECTORY", file=sys.stderr)
        return 2
    root = Path(sys.argv[1]).resolve()
    if not root.is_dir():
        print("privacy check: release root is not a directory", file=sys.stderr)
        return 2

    checkout = str(Path.cwd().resolve()).encode()
    home = str(Path.home().resolve()).encode()
    hostname = socket.gethostname().encode()
    username = os.environ.get("USER", "").encode()
    exact_patterns = [
        ("checkout path", checkout),
        ("home path", home + b"/"),
        ("host name", hostname),
    ]
    exact_patterns = [(name, value) for name, value in exact_patterns if len(value) > 3]
    username_pattern = None
    if len(username) > 2 and username.lower() not in {
        b"root",
        b"runner",
        b"ubuntu",
        b"github",
    }:
        username_pattern = re.compile(
            rb"(?<![A-Za-z0-9])" + re.escape(username) + rb"(?![A-Za-z0-9])"
        )
    home_pattern = re.compile(rb"/(?:home|Users)/[^/\s]+/")
    email_pattern = re.compile(
        rb"[A-Za-z0-9.!#$%&'*+/=?^_`{|}~-]+@"
        rb"[A-Za-z0-9-]+(?:\.[A-Za-z0-9-]+)*\.[A-Za-z]{2,63}"
    )
    approved_email = re.compile(
        rb"(?:endjynn@protonmail\.com|[0-9]+\+endjynn@users\.noreply\.github\.com)",
        re.IGNORECASE,
    )
    credential_patterns = {
        "GitHub token": re.compile(rb"(?:gh[pousr]_[A-Za-z0-9]{20,}|github_pat_[A-Za-z0-9_]{20,})"),
        "authorization header": re.compile(rb"Authorization:\s*(?:Bearer|token)\s+\S+", re.IGNORECASE),
        "private key": re.compile(rb"-----BEGIN (?:OPENSSH |RSA |EC )?PRIVATE KEY-----"),
    }

    failures = 0
    for path in sorted(item for item in root.rglob("*") if item.is_file()):
        data = path.read_bytes()
        for category, value in exact_patterns:
            count = data.count(value)
            if count:
                fail(category, path, root, count)
                failures += 1
        count = len(home_pattern.findall(data))
        if count:
            fail("absolute user path", path, root, count)
            failures += 1
        if username_pattern is not None:
            count = len(username_pattern.findall(data))
            if count:
                fail("user name", path, root, count)
                failures += 1
        emails = email_pattern.findall(data)
        rejected = sum(1 for email in emails if not approved_email.fullmatch(email))
        if rejected:
            fail("non-allowlisted email", path, root, rejected)
            failures += 1
        for category, pattern in credential_patterns.items():
            count = len(pattern.findall(data))
            if count:
                fail(category, path, root, count)
                failures += 1
    if failures:
        return 1
    print("Release privacy checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
