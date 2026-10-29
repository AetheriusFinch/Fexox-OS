/* Fexox OS Project, EULA GPLv2 */
 

OUTPUT_FORMAT("elf64-x86-64")

OUTPUT_ARCH(i386:x86-64)

ENTRY(boot_stub_entry)



KERNEL_VIRT_BASE = 0xFFFFFFFF80000000;

PHYS_BASE        = 0x00200000;   



MEMORY {

    VIRT (rwx) : ORIGIN = KERNEL_VIRT_BASE, LENGTH = 0x40000000

    PHYS (rwx) : ORIGIN = PHYS_BASE,        LENGTH = 0x40000000

}



SECTIONS {

    . = KERNEL_VIRT_BASE;



    .boot : ALIGN(4K) {

        KEEP(*(.boot))

        KEEP(*(.boot.*))

    } > VIRT AT > PHYS :boot



    .text : ALIGN(4K) {

        *(.text)

        *(.text.*)

    } > VIRT AT > PHYS :text



    .rodata : ALIGN(4K) {

        *(.rodata)

        *(.rodata.*)

    } > VIRT AT > PHYS :rodata



    .data : ALIGN(4K) {

        *(.data)

        *(.data.*)

    } > VIRT AT > PHYS :data



     

    .bss : ALIGN(4K) {

        *(.bss)

        *(.bss.*)

        *(COMMON)

        . = ALIGN(4K);

        PROVIDE(kernel_stack_bottom = .);

        . += 0x10000;

        PROVIDE(kernel_stack_top = .);

    } > VIRT AT > PHYS :bss



    /DISCARD/ : {

        *(.eh_frame)

        *(.note.gnu.build-id)

        *(.comment)

        *(.gcc_except_table)

    }

}



PHDRS {

    boot   PT_LOAD FLAGS(5);

    text   PT_LOAD FLAGS(5);

    rodata PT_LOAD FLAGS(4);

    data   PT_LOAD FLAGS(6);

    bss    PT_LOAD FLAGS(6);

}


