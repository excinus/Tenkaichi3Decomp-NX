// The Switch program's dynamic section, empty (port/tools/link.py, BT3_CC=nx64).
//
// libnx's start-up (crt0) applies a homebrew program's relocations from its dynamic section, which every libnx
// program has because it is linked position-independent at address 0 (libnx's switch.specs: -pie). The game is
// linked at a fixed address instead (the window + 0x20000000, docs/port/switch_plan.md: port/switch/nx.specs, no
// -pie), so it has no relocations and the linker makes no dynamic section; crt0 still looks for one (_DYNAMIC, in
// its MOD0 header). This is one, with nothing in it but its end (DT_NULL).
    .section .dynamic, "aw"
    .p2align 3
    .globl _DYNAMIC
    .hidden _DYNAMIC
_DYNAMIC:
    .quad 0, 0
