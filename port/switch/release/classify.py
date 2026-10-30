#!/usr/bin/env python3
"""Is an update of the original repository safe to publish for the Switch by itself? (docs/port/switch_plan.md,
phase 5; .github/workflows/follow-upstream.yml)

    python3 classify.py <base> <fork> <upstream> [--out <file>]

<base>: the two histories' common commit (git merge-base); <fork>: this fork's main before the merge; <upstream>: the
original repository's new release (its tag). Run in the repository. Prints the reasons to look at the update before
players get it, one per line (none: nothing found), and writes them to --out.

Both changes are seen as diffs from <base>, so their lines are numbered alike. The reasons:
  - the original changed lines next to this fork's own (within MARGIN lines, in the same file): the Switch's code
    there (its #ifdef __SWITCH__ blocks, its hooks in the game's code) may no longer fit what is around it, even
    where git merged both without a conflict;
  - it changed the build's tools (port/tools/: the compiler passes, the linking, the packaging);
  - it added files to the port (port/src/): new parts of the PC's platform layer, never run on a Switch;
  - it is big (more than BIG lines of code changed).
The build adds one more (the workflow's): functions the Switch program now leaves out (stubs).
"""
import argparse, re, subprocess

MARGIN = 25
BIG = 4000
CODE = (".c", ".cpp", ".h", ".hpp", ".s", ".py", ".frag", ".vert", ".glsl")

def git(*args):
    return subprocess.run(["git", *args], check=True, capture_output=True, text=True).stdout

def changed(a, b):
    """{path: status} of the files that differ from a to b"""
    out = {}
    for line in git("diff", "--name-status", "--no-renames", a, b).splitlines():
        status, path = line.split("\t", 1)
        out[path] = status[0]
    return out

def hunks(a, b, path):
    """the lines of `a` that b changes: [(first, last)], an insertion as the line it follows"""
    out = []
    for m in re.finditer(r"^@@ -(\d+)(?:,(\d+))? ", git("diff", "-U0", "--no-renames", a, b, "--", path), re.M):
        first, count = int(m.group(1)), int(m.group(2) or 1)
        out.append((first, first + max(count, 1) - 1))
    return out

def main():
    a = argparse.ArgumentParser()
    a.add_argument("base")
    a.add_argument("fork")
    a.add_argument("upstream")
    a.add_argument("--out")
    args = a.parse_args()
    up, mine = changed(args.base, args.upstream), changed(args.base, args.fork)
    reasons = []
    for path in sorted(up.keys() & mine.keys()):
        if path.startswith(".github/") or path == "README.md":
            continue  # (the fork's workflows and README are its own: the merge keeps them)
        theirs, ours = hunks(args.base, args.upstream, path), hunks(args.base, args.fork, path)
        near = [(t, o) for t in theirs for o in ours if t[0] - MARGIN <= o[1] and o[0] - MARGIN <= t[1]]
        if near:
            (t, o) = near[0]
            reasons.append(f"{path}: the original changed lines {t[0]}-{t[1]}, next to the Switch's own change at "
                           f"{o[0]}-{o[1]}" + (f" (and {len(near) - 1} more such places)" if len(near) > 1 else ""))
    tools = sorted(p for p in up if p.startswith("port/tools/") and p.endswith(".py"))
    if tools:
        reasons.append("the build's tools changed: " + ", ".join(tools))
    new = sorted(p for p, s in up.items() if s == "A" and p.startswith("port/src/") and p.endswith(CODE))
    if new:
        reasons.append("new parts of the port, not yet seen on a Switch: " + ", ".join(new[:12]) +
                       (f" and {len(new) - 12} more" if len(new) > 12 else ""))
    lines = 0
    for line in git("diff", "--numstat", "--no-renames", args.base, args.upstream).splitlines():
        plus, minus, path = line.split("\t", 2)
        if plus != "-" and path.endswith(CODE) and (path.startswith("src/") or path.startswith("port/")):
            lines += int(plus) + int(minus)
    if lines > BIG:
        reasons.append(f"a big update: {lines} lines of code changed")
    for r in reasons:
        print(r)
    if not reasons:
        print("(nothing found: the original's changes are away from the Switch's)")
    if args.out:
        with open(args.out, "w") as f:
            f.write("".join(r + "\n" for r in reasons))

if __name__ == "__main__":
    main()
