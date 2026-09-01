#!/usr/bin/env python3
"""Parse workflows and enforce frame-pacer's CI/release security contract."""

from __future__ import annotations

from pathlib import Path
import re
import sys

import yaml


ROOT = Path(__file__).resolve().parent.parent
WORKFLOWS = ROOT / ".github" / "workflows"
PINNED_ACTION = re.compile(r"^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+@[0-9a-f]{40}$")
PINNED_CONTAINER = re.compile(r"^[^@\s]+@sha256:[0-9a-f]{64}$")


def load(name: str) -> dict:
    path = WORKFLOWS / name
    try:
        value = yaml.load(path.read_text(encoding="utf-8"), Loader=yaml.BaseLoader)
    except (OSError, yaml.YAMLError) as error:
        raise AssertionError(f"{name} is not valid YAML") from error
    assert isinstance(value, dict), f"{name} must contain a mapping"
    return value


def steps(workflow: dict):
    for job_name, job in workflow["jobs"].items():
        for step in job.get("steps", []):
            yield job_name, step


def validate_common(name: str, workflow: dict) -> None:
    text = (WORKFLOWS / name).read_text(encoding="utf-8")
    assert "pull_request_target" not in text, f"{name} uses pull_request_target"
    assert "${{ secrets." not in text, f"{name} references a repository secret"
    assert workflow.get("permissions") == {"contents": "read"}, (
        f"{name} must default to contents: read"
    )
    jobs = workflow.get("jobs")
    assert isinstance(jobs, dict) and jobs, f"{name} has no jobs"
    for job_name, job in jobs.items():
        assert job.get("runs-on") == "ubuntu-22.04", (
            f"{name}:{job_name} must use the fixed runner"
        )
        timeout = job.get("timeout-minutes", "")
        assert timeout.isdigit() and 0 < int(timeout) <= 60, (
            f"{name}:{job_name} needs a finite timeout"
        )
        container = job.get("container")
        if container:
            image = container.get("image", "") if isinstance(container, dict) else container
            assert PINNED_CONTAINER.fullmatch(image), (
                f"{name}:{job_name} container is not digest-pinned"
            )
    for job_name, step in steps(workflow):
        assert step.get("continue-on-error") not in {"true", True}, (
            f"{name}:{job_name} ignores a failing step"
        )
        action = step.get("uses")
        if action:
            unannotated = action.split(" ", 1)[0]
            assert PINNED_ACTION.fullmatch(unannotated), (
                f"{name}:{job_name} action is not commit-pinned"
            )
        if action and action.startswith("actions/checkout@"):
            assert step.get("with", {}).get("persist-credentials") == "false", (
                f"{name}:{job_name} checkout persists credentials"
            )


def validate_ci(workflow: dict) -> None:
    triggers = workflow.get("on")
    assert isinstance(triggers, dict), "CI triggers must be a mapping"
    assert set(triggers) == {"pull_request", "push", "workflow_dispatch"}, (
        "CI has an unexpected trigger"
    )
    assert triggers["pull_request"].get("branches") == ["main"]
    assert triggers["push"].get("branches") == ["main"]
    assert workflow.get("concurrency", {}).get("cancel-in-progress") == "true"
    for job_name, job in workflow["jobs"].items():
        permissions = job.get("permissions", {"contents": "read"})
        assert permissions == {"contents": "read"}, f"CI:{job_name} can write"
    assert all(not step.get("uses", "").startswith("actions/upload-artifact@")
               for _, step in steps(workflow)), "CI retains a routine artifact"


def validate_release(workflow: dict) -> None:
    triggers = workflow.get("on")
    assert isinstance(triggers, dict) and set(triggers) == {"push"}, (
        "Release must trigger only from a tag push"
    )
    assert triggers["push"] == {"tags": ["v*"]}, "Release tag filter changed"
    jobs = workflow["jobs"]
    assert set(jobs) == {"build", "publish"}, "Release job isolation changed"
    assert jobs["build"].get("permissions") == {"contents": "read"}
    assert jobs["publish"].get("permissions") == {"contents": "write"}
    assert jobs["publish"].get("needs") == "build"
    publish_steps = jobs["publish"].get("steps", [])
    assert all(not step.get("uses", "").startswith("actions/checkout@")
               for step in publish_steps), "Publish job checks out repository code"
    assert sum(step.get("uses", "").startswith("actions/upload-artifact@")
               for step in jobs["build"].get("steps", [])) == 1
    assert sum(step.get("uses", "").startswith("actions/download-artifact@")
               for step in publish_steps) == 1


def main() -> int:
    try:
        ci = load("ci.yml")
        release = load("release.yml")
        validate_common("ci.yml", ci)
        validate_common("release.yml", release)
        validate_ci(ci)
        validate_release(release)
    except AssertionError as error:
        print(f"Workflow validation failed: {error}", file=sys.stderr)
        return 1
    print("Workflow validation passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
