/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Felucca update loader entry. The SPL decompresses the loader to its load
 * address (0x01C0A800) and calls its first byte. Interrupts off,
 * our own stacks, clear .bss, then C. Never returns. */
	.section .text.crt0,"ax",@progbits
	.globl _start
_start:
	goto 1f
	.ascii "FELUCCA-LOADER-1"	/* 16 bytes: Felucca (and the firmwares built on it, SLOOP, The Firmware Hub's
				 * package check) recognise its loader in a package by this */
	.ascii "MIDIMANIA-LOADER-1"	/* and Midi-Mania 0.3.0 to 0.9.1 by this (their only marker): with it they take
				 * the package as their own loader's and skip the boot-area check, which a
				 * Midi-Mania package does not pass (bsp/chip/ota.c ota_boot_valid). Both must
				 * stay plain in the package: build.py keeps the loader's first 64 bytes as
				 * LZ4 literals (0.9.1 and earlier look at bytes 0x48..0x88 of the loader entry) */
	.balign 2
1:	cli
	sp = _stack_top
	ssp = _sstack_top
	call ldr_cstart
	goto .
