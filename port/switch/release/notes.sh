#!/bin/sh
# A Switch release's notes (docs/port/switch_plan.md, phase 5), as GitHub shows them and the game's "What's new"
# (plain text: the game shows them as they are).
#   sh port/switch/release/notes.sh <tag> <commit> [<the notes typed in>] [<file of the pre-release's reasons>]
# Run in a full clone of this fork; the original repository's tags are fetched (never pushed).
UPSTREAM=z3xox/Tenkaichi3Decomp
tag=$1 sha=$2 extra=$3 why=$4
base=${tag%-nx*}
git fetch --quiet --no-tags "https://github.com/$UPSTREAM.git" '+refs/tags/v*:refs/upstream-tags/v*' 2>/dev/null
prev=$(git tag --merged "$sha" --sort=-v:refname --list 'v*-nx*' | grep -vx "$tag" | head -n 1)
echo "Tenkaichi3Decomp for the Nintendo Switch, ${tag#v}: the port's ${base#v} (https://github.com/$UPSTREAM/releases/tag/$base), built for the Switch."
echo
if [ -n "$extra" ]; then
    printf '%s\n\n' "$extra"
fi
if [ -n "$why" ] && [ -s "$why" ]; then
    echo "PRE-RELEASE: not offered to the games until it is checked on a Switch, because:"
    sed 's/^/- /' "$why"
    echo
fi
echo "First install: download the zip for your PC (windows, linux or macos), unpack it, and start the setup inside: it puts the game on your SD card, with the game data from your own disc image or from your PC version of Tenkaichi3Decomp. You need Atmosphere and sys-clk on the Switch. Already installed: the game updates itself (+ and - together, then Update now)."
echo
if [ -z "$prev" ]; then
    echo "The first release for the Switch."
else
    pbase=${prev%-nx*}
    if [ "$pbase" != "$base" ]; then
        echo "The port's ${pbase#v} -> ${base#v}: see the original's release notes (link above)."
    fi
    changes=$(git log --no-merges --format='%s' "$prev..$sha" --not "refs/upstream-tags/$base" 2>/dev/null | cut -c1-200 | head -n 20)
    if [ -n "$changes" ]; then
        echo "For the Switch since ${prev#v}:"
        echo "$changes" | sed 's/^/- /'
    fi
fi
