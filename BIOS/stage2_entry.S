/* Fexox OS Project, EULA GPLv2 */
 
























    .set LOW_REGS, 0x7000

    .section .text.entry, "ax"
    .code16
    .globl _start16
_start16:
    cli
    xorw    %ax, %ax
    movw    %ax, %ds
    movw    %ax, %es
    movw    %ax, %ss
    movw    $0x7000, %sp
    cld
    movb    %dl, boot_drive

    call    enable_a20
    testw   %ax, %ax
    jnz     1f
    movw    $msg_a20, %si
    call    rm_puts
    jmp     rm_hang
1:
    lgdtl   gdt_desc
    movl    %cr0, %eax
    orl     $1, %eax
    movl    %eax, %cr0
    ljmpl   $0x08, $pm_entry

 
rm_puts:
    lodsb
    orb     %al, %al
    jz      2f
    movb    $0x0E, %ah
    movw    $7, %bx
    int     $0x10
    jmp     rm_puts
2:  ret
rm_hang:
    cli
    hlt
    jmp     rm_hang

 




enable_a20:
    call    a20_check
    jnz     a20_ok
    movw    $0x2401, %ax
    int     $0x15
    call    a20_check
    jnz     a20_ok
    inb     $0x92, %al
    testb   $2, %al
    jnz     3f
    orb     $2, %al
    andb    $0xFE, %al
    outb    %al, $0x92
3:  call    a20_check
    jnz     a20_ok
    call    kbd_wait
    movb    $0xD1, %al
    outb    %al, $0x64
    call    kbd_wait
    movb    $0xDF, %al
    outb    %al, $0x60
    call    kbd_wait
    movw    $200, %cx
4:  call    a20_check
    jnz     a20_ok
    pushw   %cx
    movw    $0xFFFF, %cx
5:  loop    5b
    popw    %cx
    loop    4b
    xorw    %ax, %ax
    ret
a20_ok:
    movw    $1, %ax
    ret

kbd_wait:
    pushw   %cx
    movw    $0xFFFF, %cx
6:  inb     $0x64, %al
    testb   $2, %al
    jz      7f
    loop    6b
7:  popw    %cx
    ret

a20_check:                         
    pushw   %ds
    pushw   %es
    pushw   %si
    pushw   %di
    pushw   %dx
    xorw    %ax, %ax
    movw    %ax, %ds
    decw    %ax
    movw    %ax, %es
    movw    $0x0500, %si
    movw    $0x0510, %di
    movb    %ds:(%si), %al
    pushw   %ax
    movb    %es:(%di), %al
    pushw   %ax
    movb    $0x00, %ds:(%si)
    movb    $0xFF, %es:(%di)
    cmpb    $0xFF, %ds:(%si)
    popw    %ax
    movb    %al, %es:(%di)
    popw    %ax
    movb    %al, %ds:(%si)
    movw    $0, %ax
    je      8f
    movw    $1, %ax
8:  popw    %dx
    popw    %di
    popw    %si
    popw    %es
    popw    %ds
    orw     %ax, %ax
    ret

 


    .balign 8
gdt:
    .quad   0x0000000000000000         
    .quad   0x00CF9A000000FFFF         
    .quad   0x00CF92000000FFFF         
    .quad   0x00009A000000FFFF         
    .quad   0x000092000000FFFF         
gdt_end:
gdt_desc:
    .word   gdt_end - gdt - 1
    .long   gdt
idt_rm:
    .word   0x03FF
    .long   0
    .balign 4
saved_esp:  .long 0
saved_r:    .long 0
    .globl boot_drive
boot_drive: .byte 0
msg_a20:    .asciz "Fexox OS: A20 failed"

 


    .code32
pm_entry:
    movw    $0x10, %ax
    movw    %ax, %ds
    movw    %ax, %es
    movw    %ax, %fs
    movw    %ax, %gs
    movw    %ax, %ss
    movl    $0x90000, %esp
    cld
    movl    $__bss_start, %edi
    movl    $__bss_end, %ecx
    subl    %edi, %ecx
    xorl    %eax, %eax
    rep stosb
    movzbl  boot_drive, %eax
    pushl   %eax
    call    stage2_main
1:  cli
    hlt
    jmp     1b

 




    .globl bios_int
bios_int:
    pushal
    movl    36(%esp), %eax            
    movb    %al, int_no
    movl    40(%esp), %esi            
    movl    %esi, saved_r
    movl    $LOW_REGS, %edi
    movl    $10, %ecx
    cld
    rep movsl
    movl    %esp, saved_esp
    cli
    ljmp    $0x18, $pm16_entry

    .code16
pm16_entry:
    movw    $0x20, %ax
    movw    %ax, %ds
    movw    %ax, %es
    movw    %ax, %fs
    movw    %ax, %gs
    movw    %ax, %ss
    movl    %cr0, %eax
    andl    $0xFFFFFFFE, %eax
    movl    %eax, %cr0
    ljmp    $0x0000, $rm_entry
rm_entry:
    xorw    %ax, %ax
    movw    %ax, %ds
    movw    %ax, %es
    movw    %ax, %fs
    movw    %ax, %gs
    movw    %ax, %ss
    movl    $0x7000, %esp
    lidtl   idt_rm
    movl    LOW_REGS+4,  %ebx
    movl    LOW_REGS+8,  %ecx
    movl    LOW_REGS+12, %edx
    movl    LOW_REGS+16, %esi
    movl    LOW_REGS+20, %edi
    movl    LOW_REGS+24, %ebp
    movw    LOW_REGS+30, %es
    movw    LOW_REGS+32, %fs
    movw    LOW_REGS+34, %gs
    pushl   LOW_REGS+0                
    pushw   LOW_REGS+28               
    popw    %ds
    popl    %eax
    sti
int_insn:
    .byte   0xCD
int_no:
    .byte   0
    cli
    movl    %eax, %cs:LOW_REGS+0
    movl    %ebx, %cs:LOW_REGS+4
    movl    %ecx, %cs:LOW_REGS+8
    movl    %edx, %cs:LOW_REGS+12
    movl    %esi, %cs:LOW_REGS+16
    movl    %edi, %cs:LOW_REGS+20
    movl    %ebp, %cs:LOW_REGS+24
    movw    %ds,  %cs:LOW_REGS+28
    movw    %es,  %cs:LOW_REGS+30
    movw    %fs,  %cs:LOW_REGS+32
    movw    %gs,  %cs:LOW_REGS+34
    pushfl
    popl    %eax
    movl    %eax, %cs:LOW_REGS+36
    xorw    %ax, %ax
    movw    %ax, %ds
    lgdtl   gdt_desc
    movl    %cr0, %eax
    orl     $1, %eax
    movl    %eax, %cr0
    ljmpl   $0x08, $pm32_back

    .code32
pm32_back:
    movw    $0x10, %ax
    movw    %ax, %ds
    movw    %ax, %es
    movw    %ax, %fs
    movw    %ax, %gs
    movw    %ax, %ss
    movl    saved_esp, %esp
    movl    $LOW_REGS, %esi
    movl    saved_r, %edi
    movl    $10, %ecx
    cld
    rep movsl
    popal
    ret

 











    .globl enter_long_mode
enter_long_mode:
    cli
    movl    4(%esp),  %ebx
    movl    8(%esp),  %eax
    movl    %eax, ll_entry_lo
    movl    12(%esp), %eax
    movl    %eax, ll_entry_hi
    movl    16(%esp), %eax
    movl    %eax, ll_info
    movl    20(%esp), %eax
    movl    %eax, ll_stack
    movl    $0x620, %eax              
    movl    %eax, %cr4
    movl    %ebx, %cr3
    movl    $0xC0000080, %ecx         
    rdmsr
    orl     $0x100, %eax              
    wrmsr
    movl    %cr0, %eax
    orl     $0x80010023, %eax         
    movl    %eax, %cr0
    lgdt    gdt64_desc
    ljmp    $0x08, $long_entry

long_entry:                           
    .byte   0x66, 0xB8, 0x10, 0x00    
    .byte   0x8E, 0xD8                
    .byte   0x8E, 0xC0                
    .byte   0x8E, 0xE0                
    .byte   0x8E, 0xE8                
    .byte   0x8E, 0xD0                
    .byte   0xBC                      
ll_stack:    .long 0
    .byte   0xBF                      
ll_info:     .long 0
    .byte   0xB9                      
ll_entry_hi: .long 0
    .byte   0xB8                      
ll_entry_lo: .long 0
    .byte   0x48, 0xC1, 0xE1, 0x20    
    .byte   0x48, 0x09, 0xC8          
    .byte   0xFF, 0xE0                

    .balign 8
gdt64:
    .quad   0x0000000000000000
    .quad   0x00209A0000000000
    .quad   0x0000920000000000
gdt64_end:
gdt64_desc:
    .word   gdt64_end - gdt64 - 1
    .long   gdt64
    .long   0
