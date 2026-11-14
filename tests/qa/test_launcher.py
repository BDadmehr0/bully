#!/usr/bin/env python3
"""End-to-end tests for scripts/run_game.py.

Run by ctest as the `launcher` test:

    python3 tests/qa/test_launcher.py --repo <src> --game <bully_game> --cli <bully_asset_cli>

The launcher is part of the documented "how do I run it?" workflow, so it is
tested against the real binaries rather than mocked: a scaffolded project, a
dry run, a real headless run, and the usage-error paths.
"""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

FAILURES: list[str] = []


def check(condition: bool, message: str) -> None:
    if condition:
        print(f"  ok   {message}")
    else:
        print(f"  FAIL {message}")
        FAILURES.append(message)


class Launcher:
    def __init__(self, repo: Path, game: Path, cli: Path) -> None:
        self.script = repo / "scripts" / "run_game.py"
        self.repo = repo
        self.game = game
        self.cli = cli

    def run(
        self, *arguments: str, cwd: Path | None = None
    ) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [sys.executable, str(self.script), *arguments],
            cwd=str(cwd or self.repo),
            capture_output=True,
            text=True,
            timeout=120,
        )


def scaffold(cli: Path, directory: Path) -> None:
    result = subprocess.run(
        [str(cli), "new", str(directory), "--template", "retro-action"],
        capture_output=True,
        text=True,
        timeout=120,
    )
    if result.returncode != 0:
        raise SystemExit(f"could not scaffold a project: {result.stderr}{result.stdout}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", required=True, type=Path)
    parser.add_argument("--game", required=True, type=Path)
    parser.add_argument("--cli", required=True, type=Path)
    args = parser.parse_args()

    launcher = Launcher(args.repo.resolve(), args.game.resolve(), args.cli.resolve())

    print("--help")
    result = launcher.run("--help")
    check(result.returncode == 0, "--help exits 0")
    check("--headless" in result.stdout, "--help documents --headless")
    check("--dry-run" in result.stdout, "--help documents --dry-run")

    print("--list")
    result = launcher.run("--list")
    check(result.returncode == 0, "--list exits 0")
    check("projects:" in result.stdout or "packages:" in result.stdout,
          "--list names something runnable")

    with tempfile.TemporaryDirectory(prefix="bully-launcher-") as temporary:
        project = Path(temporary) / "launch-test"
        scaffold(args.cli.resolve(), project)

        print("--dry-run on an unbuilt project")
        result = launcher.run(str(project), "--headless", "--frames", "5", "--dry-run")
        check(result.returncode == 0, "dry run exits 0")
        check("--project" in result.stdout, "dry run shows the asset build command")
        check("--frames" in result.stdout, "dry run shows the forwarded --frames")
        check(not (project / "built").exists() or
              not any(p.is_file() and p.name != ".gitkeep" for p in (project / "built").rglob("*")),
              "dry run does not build anything")

        print("real headless run, assets built automatically")
        result = launcher.run(str(project), "--headless", "--frames", "20")
        check(result.returncode == 0, "run exits 0")
        check("fixed steps:       20" in result.stdout, "20 frames were simulated")
        check("mounted assets:" in result.stdout, "runtime statistics are printed")

        print("second run reuses the build")
        result = launcher.run(str(project), "--headless", "--frames", "2")
        check(result.returncode == 0, "second run exits 0")
        check("already present" in result.stdout, "reports existing compiled assets")

        print("manifest path is accepted")
        result = launcher.run(
            str(project / "project.bullyproj"), "--headless", "--frames", "2"
        )
        check(result.returncode == 0, "project.bullyproj resolves to its directory")

        print("--no-build skips the asset build")
        result = launcher.run(str(project), "--headless", "--frames", "2", "--no-build")
        check(result.returncode == 0, "--no-build still runs")
        check("compiling assets" not in result.stdout, "--no-build does not compile")

        print("a missing target is a usage error")
        result = launcher.run("does-not-exist", "--headless")
        check(result.returncode == 2, "missing target exits 2")
        check("no such project" in result.stderr, "explains the missing target")

        print("a directory that is not a project is a usage error")
        result = launcher.run(str(project / "assets"), "--headless")
        check(result.returncode == 2, "non-project directory exits 2")
        check("not a Bully project" in result.stderr, "explains what is missing")

        print("a package run")
        packaged = subprocess.run(
            [str(args.cli.resolve()), "package", "--project", str(project)],
            capture_output=True,
            text=True,
            timeout=120,
        )
        packages = sorted(project.glob("packages/*.bpackage"))
        if packaged.returncode == 0 and packages:
            result = launcher.run(str(packages[0]), "--headless", "--frames", "3")
            check(result.returncode == 0, "package runs")
            check("running package" in result.stdout, "launcher reports a package run")
        else:
            check(False, "could not build a package to test with")

        print("no build tree")
        result = launcher.run(
            str(project), "--headless", "--build-dir", str(Path(temporary) / "nope")
        )
        check(result.returncode == 2, "missing build tree exits 2")
        check("no build tree found" in result.stderr, "explains how to configure")

    print()
    if FAILURES:
        print(f"{len(FAILURES)} check(s) failed")
        return 1
    print("all launcher checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
