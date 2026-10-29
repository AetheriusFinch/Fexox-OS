/* Fexox OS Project, EULA GPLv2 */
 












    .set STAGE2_SECTORS, 80           
    .set STAGE2_SEG,     0x0800       

    .code16
    .section .text
    .globl _start
_start:
    cli
    xorw    %ax, %ax
    movw    %ax, %ds
    movw    %ax, %es
    movw    %ax, %ss
    movw    $0x7C00, %sp
    sti
    cld
    movb    %dl, drive

     
    movb    $0x41, %ah
    movw    $0x55AA, %bx
    int     $0x13
    jc      fail
    cmpw    $0xAA55, %bx
    jne     fail

    movw    $(STAGE2_SECTORS / 16), %cx
1:
    pushw   %cx
    movb    drive, %dl
    movw    $dap, %si
    movb    $0x42, %ah
    int     $0x13
    jnc     2f
     
    xorw    %ax, %ax
    movb    drive, %dl
    int     $0x13
    movb    drive, %dl
    movw    $dap, %si
    movb    $0x42, %ah
    int     $0x13
    jc      fail_pop
2:
    addw    $0x200, dap_seg           
    addl    $16, dap_lba
    popw    %cx
    loop    1b

    movb    drive, %dl
    ljmp    $0x0000, $0x8000

fail_pop:
    popw    %cx
fail:
    movw    $msg, %si
3:
    lodsb
    orb     %al, %al
    jz      hang
    movb    $0x0E, %ah
    movw    $0x0007, %bx
    int     $0x10
    jmp     3b
hang:
    cli
    hlt
    jmp     hang

drive:  .byte 0
    .balign 4
dap:
    .byte   0x10, 0x00
    .word   16                        
dap_off:.word   0x0000
dap_seg:.word   STAGE2_SEG
dap_lba:.long   1
    .long   0
msg:    .asciz  "Fexox OS: cannot load stage2"
