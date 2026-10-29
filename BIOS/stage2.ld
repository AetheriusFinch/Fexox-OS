/* Fexox OS Project, EULA GPLv2 */
OUTPUT_FORMAT(elf32-i386)
ENTRY(_start16)
SECTIONS {
    . = 0x8000;
    .text   : { *(.text.entry) *(.text .text.*) }
    .rodata : { *(.rodata .rodata.*) }
    .data   : { *(.data .data.*) }
    . = ALIGN(4);
    __bss_start = .;
    .bss    : { *(.bss .bss.*) *(COMMON) }
    __bss_end = .;
    /DISCARD/ : { *(.eh_frame) *(.comment) *(.note*) }
}
