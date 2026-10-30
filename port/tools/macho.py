"""The game's data tables (port/build/gen/data, port/data: GNU assembly for ELF) as assembly for the macOS build
(asm) and the Switch build (elf_asm).

  - C symbols carry a leading underscore in Mach-O: `name:` / `.globl name` -> `_name`;
  - `.section .data` / `.section .bss` -> `.data` / `.bss`;
  - a pointer in a table (`.long Symbol`, `.long Symbol + k`) is a game address, i.e. an offset in the window
    (port/tools/macwin/macwin.cpp): `.long _Symbol - __mh_execute_header + 0x20000000 + k`, which the linker
    resolves with a subtractor relocation and which stays right wherever the system loads the program."""
import re
from macwin import ABS  # symbols at fixed game addresses (GNU ld --defsym in the other builds)
from toolchain import NX_WINDOW

LABEL = re.compile(r"^([A-Za-z_][\w.$]*):", re.M)
GLOBL = re.compile(r"^(\s*\.globl\s+)([A-Za-z_][\w.$]*)", re.M)
POINTER = re.compile(r"^(\s*\.long\s+)([A-Za-z_][\w.$]*)\s*(?:([+-])\s*(\w+))?\s*$", re.M)

def asm(text):
    text = re.sub(r"^(\s*)\.section\s+\.(data|bss)\s*$", r"\1.\2", text, flags=re.M)
    text = LABEL.sub(r"_\1:", text)
    text = GLOBL.sub(r"\1_\2", text)
    def pointer(m):
        k = f" {m.group(3)} {m.group(4)}" if m.group(3) else ""
        if m.group(2) in ABS:
            return f"{m.group(1)}{ABS[m.group(2)]:#x}{k}"
        return f"{m.group(1)}_{m.group(2)} - __mh_execute_header + 0x20000000{k}"
    return POINTER.sub(pointer, text)

# The Switch: names of the game's that its C library (devkitA64's newlib) defines too, given another name in the
# game's tables and the game's objects (undefined.py): the PS2 C library's character table, which the game's
# password screen reads (menu_za_b.c); the port's own files keep the Switch's.
NX_RENAME = {"_ctype_": "bt3_ps2_ctype_"}
NX_NAME = re.compile(r"(?<![\w.$])(" + "|".join(re.escape(k) for k in NX_RENAME) + r")(?![\w.$])")

def elf_asm(text):
    """The Switch: still ELF, so only the pointers change: an offset in the window, which starts at the fixed
    NX_WINDOW (`.long Symbol - NX_WINDOW + k`; GNU ld resolves it, the program being linked at NX_WINDOW + 0x20000000).
    And the names of NX_RENAME."""
    text = NX_NAME.sub(lambda m: NX_RENAME[m.group(1)], text)
    def pointer(m):
        k = f" {m.group(3)} {m.group(4)}" if m.group(3) else ""
        if m.group(2) in ABS:
            return f"{m.group(1)}{ABS[m.group(2)]:#x}{k}"
        return f"{m.group(1)}{m.group(2)} - {NX_WINDOW:#x}{k}"
    return POINTER.sub(pointer, text)
