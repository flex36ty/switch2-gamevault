/* SPDX-License-Identifier: GPL-2.0-only */
ENTRY(bootstrapMain)
SECTIONS {
    . = 0x80;
    .text : { KEEP(*(.text.entry)) *(.text*) }
    .rodata : { *(.rodata*) }
    ASSERT(. <= 0x10000, "Bootstrap exceeds NRO text region")
    . = 0x11000;
    .data : { *(.data*) }
    ASSERT(. <= 0x12000, "Bootstrap exceeds NRO data page")
    . = 0x12000;
    .tdata : { *(.tdata*) }
    .tbss : { *(.tbss*) }
    .bss : { *(.bss*) *(COMMON) }
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
ASSERT(SIZEOF(.tdata) == 0, "Probe only supports zero-initialized compiler TLS")
ASSERT(SIZEOF(.tbss) <= 0xf0, "Compiler TLS exceeds bootstrap allocation")
ASSERT(ALIGNOF(.tbss) <= 16, "Compiler TLS alignment exceeds TCB alignment")
ASSERT(. <= 0x13000, "Bootstrap exceeds NRO BSS page")
