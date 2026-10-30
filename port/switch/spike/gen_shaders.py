#!/usr/bin/env python3
"""Writes spike_shaders.h: the port's shaders (port/src/gs/shaders) as C strings, with the two variants of gs.frag
that port/tools/undefined.py makes (gsnz.frag, gswb.frag) made the same way.  gen_shaders.py <out.h>"""
import pathlib, sys

ROOT = pathlib.Path(__file__).resolve().parents[3]
SHADERS = ROOT / "port/src/gs/shaders"

def c_string(text):
    return '"' + text.replace("\\", "\\\\").replace('"', '\\"').replace("\n", '\\n"\n    "') + '"'

def main():
    gs = (SHADERS / "gs.frag").read_text()
    sources = {f.stem.capitalize() + f.suffix[1:].capitalize(): f.read_text()
               for f in sorted(SHADERS.glob("*.*")) if f.suffix in (".vert", ".frag")}
    sources["GsnzFrag"] = gs.replace("\n", "\n#define NO_FRAG_DEPTH\n", 1)
    sources["GswbFrag"] = gs.replace("\n", "\n#define NO_FRAG_DEPTH\n#define GS_BLOCK\n", 1)
    out = "/* generated from port/src/gs/shaders by port/switch/spike/gen_shaders.py */\n"
    out += "".join(f"static const char k{name}[] =\n    {c_string(text)};\n" for name, text in sorted(sources.items()))
    pathlib.Path(sys.argv[1]).write_text(out)

main()
