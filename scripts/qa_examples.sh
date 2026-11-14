#!/usr/bin/env bash
# Runs the full asset pipeline and QA dashboard over every committed example
# project. This is what `make test-all` calls: code tests live in ctest, this
# catches *content* regressions (a scene pointing at a deleted model, a dialogue
# line without a recording, a package that no longer verifies).
#
# Usage: scripts/qa_examples.sh [path-to-bully_asset_cli] [examples-dir]
#
# Exit code 0 = every project built, packaged and passed QA.
set -euo pipefail

CLI="${1:-build/apps/asset_cli/bully_asset_cli}"
EXAMPLES="${2:-examples}"

if [[ ! -x "$CLI" ]]; then
    echo "error: asset CLI not found at '$CLI' (build it first: cmake --build build)" >&2
    exit 2
fi
if [[ ! -d "$EXAMPLES" ]]; then
    echo "error: no examples directory at '$EXAMPLES'" >&2
    exit 2
fi

projects=()
while IFS= read -r project; do
    projects+=("$project")
done < <(find "$EXAMPLES" -maxdepth 2 -name 'project.bullyproj' -printf '%h\n' | sort)

if [[ ${#projects[@]} -eq 0 ]]; then
    echo "error: no project.bullyproj files under '$EXAMPLES'" >&2
    exit 2
fi

failed=0
for project in "${projects[@]}"; do
    name="$(basename "$project")"
    echo "==> $name: build"
    if ! "$CLI" build --project "$project" --quiet; then
        echo "    FAILED to build $name" >&2
        failed=1
        continue
    fi
    # `qa` returns non-zero when a rule reports an error (warnings are fine:
    # unrecorded dialogue lines are expected in a demo project).
    echo "==> $name: QA"
    if ! "$CLI" qa --project "$project" --quiet > "$project/reports/qa-report.md"; then
        echo "    QA reported errors for $name (see $project/reports/qa-report.md)" >&2
        failed=1
        continue
    fi
    echo "==> $name: package"
    if ! "$CLI" package --project "$project" --quiet; then
        echo "    FAILED to package $name" >&2
        failed=1
        continue
    fi
    echo "    ok"
done

if [[ $failed -ne 0 ]]; then
    echo "one or more example projects failed" >&2
    exit 1
fi
echo "all ${#projects[@]} example project(s) built, verified and packaged"
