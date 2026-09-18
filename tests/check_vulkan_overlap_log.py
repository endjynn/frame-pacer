#!/usr/bin/env python3
"""Require isolated HUD cleanup while real Vulkan swapchains overlap."""

import re
import sys
from pathlib import Path


def check(path):
    active = set()
    submitted = set()
    target = None
    hud_destroyed = False
    creates = destroys = 0
    peak = 0
    for line in Path(path).read_text().splitlines():
        match = re.search(r"swapchain=([0-9a-f]+)", line)
        handle = match.group(1) if match else None
        if "swapchain create end" in line:
            assert "result=0" in line and handle not in active
            active.add(handle)
            creates += 1
            peak = max(peak, len(active))
        elif "HUD overlay submitted" in line:
            assert handle in active
            submitted.add(handle)
        elif "swapchain destroy begin" in line:
            assert target is None and handle in active
            target = handle
            hud_destroyed = False
        elif "HUD destroy begin" in line:
            assert handle == target, "destroyed another live swapchain's HUD"
            assert handle in submitted and not hud_destroyed
            assert "submitted=1 disabled=0" in line
            hud_destroyed = True
        elif "swapchain destroy end" in line:
            assert handle == target and hud_destroyed
            active.remove(handle)
            submitted.remove(handle)
            target = None
            destroys += 1
        assert "fail-open" not in line and "present failed" not in line
    assert not active and not submitted and target is None
    assert creates == destroys == 9 and peak == 2
    print("Overlapping Vulkan swapchain lifetimes passed")


if __name__ == "__main__":
    check(sys.argv[1])
