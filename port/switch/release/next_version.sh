#!/bin/sh
# The next Switch release's tag for a commit (docs/port/switch_plan.md, phase 5): v<V>-nx<n>, where V is the newest
# release of the original repository that the commit has (its tags, fetched here from it, never pushed), and n one
# more than this fork's last release of V (its tags on origin).
#   sh port/switch/release/next_version.sh <commit>          prints the tag (v0.1.39.2-nx1)
# Run in a full clone of this fork (actions/checkout with fetch-depth: 0).
set -e
UPSTREAM=https://github.com/z3xox/Tenkaichi3Decomp.git
git fetch --quiet --no-tags "$UPSTREAM" '+refs/tags/v*:refs/upstream-tags/v*'
base=$(git for-each-ref --merged "$1" --sort=-v:refname --format='%(refname:lstrip=2)' refs/upstream-tags | grep -E '^v[0-9]+(\.[0-9]+){1,3}$' | head -n 1)
[ -n "$base" ] || { echo "no release of the original repository in $1" >&2; exit 1; }
n=$(git ls-remote --tags origin "refs/tags/$base-nx*" | sed -n "s|.*refs/tags/$base-nx\([0-9]*\)\$|\1|p" | sort -n | tail -n 1)
echo "$base-nx$(( ${n:-0} + 1 ))"
