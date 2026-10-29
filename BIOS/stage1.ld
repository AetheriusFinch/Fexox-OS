/* Fexox OS Project, EULA GPLv2 */
OUTPUT_FORMAT(elf32-i386)
ENTRY(_start)
SECTIONS {
    . = 0x7C00;
    .text : { *(.text) *(.rodata*) *(.data*) }
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame) }
}
