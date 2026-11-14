#!/usr/bin/env python3
"""Build (if needed) and run a Bully Studio project.

A thin launcher around `bully_asset_cli build` + `bully_game`, so the usual
"why did nothing happen?" questions are answered for you:

  * finds the build tree (build/, build/headless/, build/debug/, ...) and tells
    you how to configure one if it cannot;
  * compiles the project's assets when they are missing or stale;
  * explains a missing SDL3 window instead of dumping a bare error;
  * forwards every other flag to `bully_game` unchanged.

Usage
-----
    scripts/run_game.py                        # examples/hello-retro, windowed
    scripts/run_game.py --headless --frames 300
    scripts/run_game.py mygame                 # a project you created
    scripts/run_game.py mygame --headless
    scripts/run_game.py mygame/packages/mygame.bpackage   # run a shipped build
    scripts/run_game.py --list                 # what can be run?

Options handled here (everything else goes to bully_game):

    --headless            run without a window
    --frames N            stop after N frames (headless default 600)
    --delta S             fixed frame delta
    --scene ID            entry scene
    --verbose             debug logging
    -v, --debug           show the commands being run
    -n, --dry-run         print what would run, do nothing
    -y, --yes             never ask, just build
    --build-dir DIR       use a specific CMake build tree
    --no-build            never build assets, just run
    --rebuild             force a full asset rebuild (ignores the cache)
    --list                list runnable projects/packages
    -h, --help            this text

Exit codes: 0 ran successfully, 1 build/boot failure, 2 usage error.
"""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_PROJECT = "examples/hello-retro"

# Presets put binaries in different places; check the ones this repo defines
# plus a plain ./build for a manual configure.
BUILD_CANDIDATES = [
    "build/headless",
    "build",
    "build/debug",
    "build/release",
    "build/asan",
]


# --------------------------------------------------------------------- output --
def _supports_colour() -> bool:
    return sys.stdout.isatty() and os.environ.get("NO_COLOR") is None


class Ui:
    """Minimal, dependency-free terminal output."""

    def __init__(self, quiet: bool = False) -> None:
        self.quiet = quiet
        self.colour = _supports_colour()

    def _c(self, code: str, text: str) -> str:
        return f"\033[{code}m{text}\033[0m" if self.colour else text

    def step(self, message: str) -> None:
        if not self.quiet:
            print(self._c("36", f"==> {message}"))

    def info(self, message: str) -> None:
        if not self.quiet:
            print(f"    {message}")

    def warn(self, message: str) -> None:
        print(self._c("33", f"warning: {message}"), file=sys.stderr)

    def error(self, message: str) -> None:
        print(self._c("31", f"error: {message}"), file=sys.stderr)

    def ok(self, message: str) -> None:
        if not self.quiet:
            print(self._c("32", message))


# ---------------------------------------------------------------------- build --
def find_executable(name: str, build_dir: Path | None) -> Path | None:
    """Locate a built tool, preferring an explicit build dir."""
    candidates: list[Path] = []
    if build_dir is not None:
        candidates.append(build_dir)
    else:
        candidates.extend(REPO_ROOT / candidate for candidate in BUILD_CANDIDATES)

    for root in candidates:
        direct = root / "apps" / "game" / name if name == "bully_game" else None
        if direct is not None and direct.is_file() and os.access(direct, os.X_OK):
            return direct
        # Fall back to a search: layouts differ slightly between generators.
        if root.is_dir():
            found = sorted(root.rglob(name))
            for path in found:
                if path.is_file() and os.access(path, os.X_OK):
                    return path
    return None


def configure_hint(ui: Ui) -> None:
    ui.error("no build tree found (bully_game is not built yet)")
    ui.info("build it first:")
    ui.info("  cmake --preset headless")
    ui.info("  cmake --build --preset headless -j\"$(nproc)\"")
    ui.info("that is enough for --headless; a window additionally needs SDL3 + OpenGL")
    ui.info("on Arch Linux, for a window:  sudo pacman -S sdl3 mesa")


def resolve_target(target: str, ui: Ui) -> tuple[Path, bool]:
    """Returns (path, is_package). Accepts dirs, manifests and packages."""
    path = Path(target).expanduser()
    if not path.is_absolute():
        # Relative paths are tried against the cwd first, then the repo root, so
        # `scripts/run_game.py mygame` works from either place.
        if not path.exists():
            alternative = REPO_ROOT / path
            if alternative.exists():
                path = alternative
    path = path.resolve()

    if not path.exists():
        ui.error(f"no such project or package: {target}")
        sys.exit(2)

    if path.is_file():
        if path.suffix == ".bpackage":
            return path, True
        if path.name == "project.bullyproj":
            return path.parent, False
        ui.error(f"expected a project directory or a .bpackage, got: {path}")
        sys.exit(2)

    if not (path / "project.bullyproj").is_file():
        ui.error(f"not a Bully project (no project.bullyproj): {path}")
        ui.info("create one with:")
        ui.info(f"  bully_asset_cli new {path.name} --template retro-action")
        sys.exit(2)
    return path, False


def count_compiled_assets(project: Path) -> int:
    built = project / "built"
    if not built.is_dir():
        return 0
    return sum(
        1
        for path in built.rglob("*")
        if path.is_file() and path.name != ".gitkeep"
    )


def plan_build(
    project: Path,
    cli: Path,
    *,
    force: bool,
    dry_run: bool,
    verbose: bool,
    ui: Ui,
) -> int:
    """Compiles assets when needed. Returns an exit code (0 = go ahead)."""
    compiled = count_compiled_assets(project)
    if compiled == 0:
        ui.step(f"compiling assets for {project.name} (none built yet)")
    else:
        ui.step(f"building {project.name} ({compiled} compiled asset(s) already present)")

    command = [str(cli), "build", "--project", str(project)]
    if force:
        command.append("--force")
    if verbose or dry_run:
        ui.info(" ".join(command))
    if dry_run:
        return 0

    result = subprocess.run(command, cwd=REPO_ROOT, check=False)
    if result.returncode != 0:
        ui.error("asset build failed - see the diagnostics above")
        return 1
    return 0


def plan_run(
    target: Path,
    is_package: bool,
    game: Path,
    forwarded: list[str],
    *,
    dry_run: bool,
    verbose: bool,
    ui: Ui,
) -> int:
    command = [str(game), str(target), *forwarded]
    ui.step("running " + ("package " if is_package else "project ") + target.name)
    if verbose or dry_run:
        ui.info(" ".join(command))
    if dry_run:
        ui.ok("(dry run - nothing was executed)")
        return 0

    # Run from the repository root so relative asset paths stay predictable.
    try:
        result = subprocess.run(command, cwd=REPO_ROOT, check=False)
    except KeyboardInterrupt:
        print()
        ui.info("interrupted")
        return 130

    exit_code = result.returncode
    if exit_code == 1:
        ui.error("the game failed to boot (see the message above)")
    elif exit_code == 2:
        ui.error("the game host rejected the arguments")
    return exit_code


# ------------------------------------------------------------------------ CLI --
class Parser(argparse.ArgumentParser):
    """argparse with a friendlier exit code (2 for usage, as documented)."""

    def error(self, message: str) -> None:  # pragma: no cover - argparse path
        self.print_usage(sys.stderr)
        print(f"error: {message}", file=sys.stderr)
        sys.exit(2)


def build_parser() -> argparse.ArgumentParser:
    parser = Parser(
        prog="run_game.py",
        description="Build a Bully Studio project's assets and run it.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    parser.add_argument(
        "target",
        nargs="?",
        default=DEFAULT_PROJECT,
        help=f"project directory, project.bullyproj, or .bpackage (default: {DEFAULT_PROJECT})",
    )
    parser.add_argument("--build-dir", type=Path, help="CMake build tree to use")
    parser.add_argument("--headless", action="store_true", help="run without a window")
    parser.add_argument("--frames", type=int, help="stop after N frames")
    parser.add_argument("--delta", type=float, help="fixed frame delta in seconds")
    parser.add_argument("--scene", help="entry scene asset id or path")
    parser.add_argument("--verbose", action="store_true", help="debug logging")
    parser.add_argument("--no-build", action="store_true", help="skip the asset build")
    parser.add_argument("--rebuild", action="store_true", help="force a full asset rebuild")
    parser.add_argument("--list", action="store_true", help="list runnable projects")
    parser.add_argument("--dry-run", "-n", action="store_true", help="show commands only")
    parser.add_argument("-v", "--debug", action="store_true", help="show commands")
    parser.add_argument("--quiet", "-q", action="store_true", help="minimal output")
    parser.add_argument(
        "--yes", "-y", action="store_true", help="assume yes for prompts"
    )
    return parser


def list_targets(ui: Ui) -> int:
    """Prints the projects and packages that can be run right now."""
    projects = sorted(REPO_ROOT.glob("examples/*/project.bullyproj"))
    projects += sorted(REPO_ROOT.glob("templates/*/project.bullyproj"))
    local = sorted(Path.cwd().glob("*/project.bullyproj")) if Path.cwd() != REPO_ROOT else []
    packages = sorted(REPO_ROOT.rglob("*.bpackage"))

    if not projects and not packages and not local:
        ui.info("nothing to run yet. Create a project:")
        ui.info("  bully_asset_cli new mygame --template retro-action")
        return 0

    if projects:
        print("projects:")
        for manifest in projects:
            if "third_party" in manifest.parts or "build" in manifest.parts:
                continue
            relative = manifest.parent.relative_to(REPO_ROOT)
            print(f"  {relative}{'  (default)' if relative.as_posix() == DEFAULT_PROJECT else ''}")
    for manifest in local:
        print(f"  {manifest.parent.name}  (current directory)")
    if packages:
        print("packages:")
        for package in packages:
            if "build" in package.parts:
                continue
            print(f"  {package.relative_to(REPO_ROOT)}")
    return 0


def forwarded_arguments(arguments: argparse.Namespace) -> list[str]:
    """Rebuilds the bully_game argv from the parsed options."""
    forwarded: list[str] = []
    if arguments.headless:
        forwarded.append("--headless")
    if arguments.frames is not None:
        forwarded += ["--frames", str(arguments.frames)]
    if arguments.delta is not None:
        forwarded += ["--delta", str(arguments.delta)]
    if arguments.scene is not None:
        forwarded += ["--scene", arguments.scene]
    if arguments.verbose:
        forwarded.append("--verbose")
    return forwarded


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    arguments = parser.parse_args(argv)
    ui = Ui(quiet=arguments.quiet)

    if arguments.list:
        return list_targets(ui)

    game = find_executable("bully_game", arguments.build_dir)
    if game is None:
        configure_hint(ui)
        return 2
    cli = find_executable("bully_asset_cli", arguments.build_dir)
    if cli is None and not arguments.no_build:
        ui.error("bully_asset_cli not found in the build tree; cannot compile assets")
        ui.info("run with --no-build to launch anyway, or build the CLI target")
        return 2

    target, is_package = resolve_target(arguments.target, ui)
    ui.info(f"game host : {game}")
    if not is_package:
        ui.info(f"project   : {target}")

    if not arguments.no_build and not is_package:
        if cli is None:  # unreachable: guarded above
            return 2
        code = plan_build(
            target,
            cli,
            force=arguments.rebuild,
            dry_run=arguments.dry_run,
            verbose=arguments.debug,
            ui=ui,
        )
        if code != 0:
            return code

    return plan_run(
        target,
        is_package,
        game,
        forwarded_arguments(arguments),
        dry_run=arguments.dry_run,
        verbose=arguments.debug,
        ui=ui,
    )


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        sys.exit(130)
