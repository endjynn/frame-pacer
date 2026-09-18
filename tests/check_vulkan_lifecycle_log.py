#!/usr/bin/env python3
"""Validate real Vulkan lifecycle diagnostics without changing the workload."""

import re
import sys
from pathlib import Path


def check(path):
    lines = Path(path).read_text().splitlines()
    previous = 0
    messages = []
    for line in lines:
        match = re.fullmatch(
            r"\[wall=(\d+)\.(\d{9}) mono=(\d+)\.(\d{9}) tid=(\d+)\] (.*)", line)
        assert match, f"missing event metadata: {line}"
        wall, wall_ns, mono, mono_ns, tid = map(int, match.groups()[:5])
        assert wall > 0 and tid > 0 and wall_ns < 1_000_000_000
        timestamp = mono * 1_000_000_000 + mono_ns
        assert timestamp >= previous
        previous = timestamp
        messages.append(match.group(6))

    def position(fragment):
        found = [i for i, message in enumerate(messages) if fragment in message]
        assert len(found) == 1, (fragment, found)
        return found[0]

    events = ["swapchain create begin", "swapchain create end",
              "HUD image resources ready", "HUD overlay submitted",
              "swapchain destroy begin", "HUD destroy begin",
              "HUD destroy stage=pipeline", "HUD destroy stage=texture",
              "HUD destroy stage=vertex-buffer", "HUD destroy stage=draw-resources",
              "HUD destroy stage=image-views", "HUD destroy end",
              "swapchain destroy end", "device destroy begin", "device destroy end"]
    positions = [position(event) for event in events]
    assert positions == sorted(positions), "lifecycle events out of order"
    create = messages[positions[0]]
    assert re.search(r"extent=[1-9]\d*x[1-9]\d* min_images=[1-9]\d*", create)
    assert " old=0 " in create
    assert " result=0 " in messages[positions[1]]
    handles = [re.search(r"swapchain=([0-9a-f]+)", messages[i]).group(1)
               for i in positions[1:-2]]
    assert len(set(handles)) == 1 and handles[0] != "0"
    assert "submitted=1 disabled=0" in messages[positions[5]]
    print("Vulkan lifecycle diagnostics passed")


if __name__ == "__main__":
    check(sys.argv[1])
