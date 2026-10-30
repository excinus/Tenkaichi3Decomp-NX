#!/usr/bin/env python3
"""Two builds of the Switch's game folder compared (docs/port/switch_plan.md, phase 5; .github/workflows/
follow-upstream.yml): the last release's tree and the new one, both built with the same release number and key.

    python3 compare.py <old build> <new build> [--out <file>]

The builds: port/build folders (or copies of their release-switch/ and stubs.txt). Says whether the two game
folders (release-switch: the program, its lists, the launcher's copy, every file the zip has) are the same file for
file, and which functions the new program leaves out that the old one did not (stubs.txt: what the Switch does not
have, linked as stubs). Writes `same=true|false` and `stubs=<count>` to --out (the step's GITHUB_OUTPUT), and the
new stubs, one per line, to <new build>/stubs_added.txt.
The builds are reproducible when made in the same place (the program holds a few of its source files' paths) with
the same compiler in the same container: two builds of the same source give the same files, so "the same" means the
update changes nothing for the Switch.
"""
import argparse, hashlib, pathlib, sys

def files(folder):
    out = {}
    for p in sorted(folder.rglob("*")):
        if p.is_file():
            out[str(p.relative_to(folder))] = hashlib.sha256(p.read_bytes()).hexdigest()
    return out

def stubs(build):
    p = build / "stubs.txt"
    return {line.strip() for line in p.read_text().splitlines() if line.strip()} if p.exists() else set()

def main():
    a = argparse.ArgumentParser()
    a.add_argument("old")
    a.add_argument("new")
    a.add_argument("--out")
    args = a.parse_args()
    old, new = pathlib.Path(args.old), pathlib.Path(args.new)
    fo, fn = files(old / "release-switch"), files(new / "release-switch")
    if not fn:
        sys.exit(f"{new} has no release-switch: build it first (build.sh)")
    changed = sorted(k for k in fo.keys() | fn.keys() if fo.get(k) != fn.get(k))
    for k in changed[:40]:
        print(("added   " if k not in fo else "removed " if k not in fn else "changed ") + k)
    if len(changed) > 40:
        print(f"... and {len(changed) - 40} more")
    added = sorted(stubs(new) - stubs(old))
    (new / "stubs_added.txt").write_text("".join(s + "\n" for s in added))
    print(f"{len(changed)} files differ; {len(added)} functions newly left out (stubs)" + (": " + ", ".join(added[:20]) if added else ""))
    if args.out:
        with open(args.out, "a") as f:
            f.write(f"same={'true' if not changed else 'false'}\nstubs={len(added)}\n")

if __name__ == "__main__":
    main()
