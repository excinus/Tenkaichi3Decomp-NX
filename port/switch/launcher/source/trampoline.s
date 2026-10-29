// The launcher's two pieces of assembly (port/switch/launcher; docs/port/switch_plan.md, "The launcher").

// int Launcher_Call(const ConfigEntry *entries, u64 handle, u64 entry): runs the loaded program's start (libnx's
// crt0, homebrew ABI: x0 its configuration, x1 -1) and returns its result code when it ends. The program's exit
// returns here with the stack pointer it was given, but not the registers a C caller keeps (x19-x29, d8-d15): they
// are saved here and put back.
    .section .text.Launcher_Call, "ax", %progbits
    .global Launcher_Call
    .type   Launcher_Call, %function
    .p2align 2
Launcher_Call:
    adrp x9, sLauncherSaved
    add  x9, x9, #:lo12:sLauncherSaved
    stp  x19, x20, [x9, #0x00]
    stp  x21, x22, [x9, #0x10]
    stp  x23, x24, [x9, #0x20]
    stp  x25, x26, [x9, #0x30]
    stp  x27, x28, [x9, #0x40]
    stp  x29, x30, [x9, #0x50]
    mov  x10, sp
    str  x10, [x9, #0x60]
    stp  d8, d9, [x9, #0x68]
    stp  d10, d11, [x9, #0x78]
    stp  d12, d13, [x9, #0x88]
    stp  d14, d15, [x9, #0x98]
    blr  x2
    adrp x9, sLauncherSaved
    add  x9, x9, #:lo12:sLauncherSaved
    ldp  x19, x20, [x9, #0x00]
    ldp  x21, x22, [x9, #0x10]
    ldp  x23, x24, [x9, #0x20]
    ldp  x25, x26, [x9, #0x30]
    ldp  x27, x28, [x9, #0x40]
    ldp  x29, x30, [x9, #0x50]
    ldr  x10, [x9, #0x60]
    mov  sp, x10
    ldp  d8, d9, [x9, #0x68]
    ldp  d10, d11, [x9, #0x78]
    ldp  d12, d13, [x9, #0x88]
    ldp  d14, d15, [x9, #0x98]
    ret

// An exception in this process comes to the launcher's start first (the system knows only it). While the program
// runs, it is passed on to the program's start, whose libnx gives it to the program's handler (the crash report,
// port/src/plat_crash.c), as nx-hbloader does for the programs it loads; otherwise back to the system.
    .section .text.__libnx_exception_entry, "ax", %progbits
    .global __libnx_exception_entry
    .type   __libnx_exception_entry, %function
    .p2align 2
__libnx_exception_entry:
    adrp x7, gLauncherEntry
    ldr  x7, [x7, #:lo12:gLauncherEntry]
    cbz  x7, 1f
    br   x7
1:  mov  w0, #0xf801 // KERNELRESULT(UnhandledUserInterrupt)
    svc  0x28        // svcReturnFromException

    .section .bss.sLauncherSaved, "aw", %nobits
    .p2align 4
sLauncherSaved:
    .space 0xa8
