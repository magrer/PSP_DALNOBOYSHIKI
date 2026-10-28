#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
# Runtime access must be limited to user-mode functions and memory stick saves.
if grep -Eni 'flash[012]:|sceIoAssign|sceIoDevctl|sceKernelLoadExec|sceKernelLoadModule|scePowerRequestColdReset|sceNand|sceIdStorage|sceKernelSetDdrMemoryProtection|sceKernelPatch' src/*.[ch]; then
 echo "FAIL: dangerous platform API referenced in game source" >&2; exit 1
fi
grep -q 'PSP_MODULE_USER' src/main_psp.c
grep -q 'ms0:/PSP/GAME/ROADHAUL/SAVE.DAT' src/main_psp.c
grep -q 'BUILD_PRX = 1' Makefile
printf 'PASS safety source audit: user module; memory-stick save; no flash/NAND/firmware calls\n'
