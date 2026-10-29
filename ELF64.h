/* Fexox OS Project, EULA GPLv2 */
 


#ifndef ELF64_H
#define ELF64_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

 

typedef uint64_t Elf64_Addr;
typedef uint64_t Elf64_Off;
typedef uint16_t Elf64_Half;
typedef uint32_t Elf64_Word;
typedef int32_t  Elf64_Sword;
typedef uint64_t Elf64_Xword;
typedef int64_t  Elf64_Sxword;

 
#define EI_NIDENT   16
#define EI_MAG0     0
#define EI_MAG1     1
#define EI_MAG2     2
#define EI_MAG3     3
#define EI_CLASS    4
#define EI_DATA     5
#define EI_VERSION  6
#define EI_OSABI    7

#define ELFMAG0     0x7F
#define ELFMAG1     'E'
#define ELFMAG2     'L'
#define ELFMAG3     'F'

#define ELFCLASS64  2    
#define ELFDATA2LSB 1    
#define EV_CURRENT  1

 
#define ET_EXEC     2    
#define ET_DYN      3    

 
#define EM_X86_64   62

 
#define PT_NULL     0
#define PT_LOAD     1
#define PT_DYNAMIC  2
#define PT_INTERP   3
#define PT_NOTE     4
#define PT_PHDR     6
#define PT_TLS      7
#define PT_GNU_EH_FRAME 0x6474e550
#define PT_GNU_STACK    0x6474e551
#define PT_GNU_RELRO    0x6474e552

 
#define PF_X        0x1    
#define PF_W        0x2    
#define PF_R        0x4    

 



typedef struct __attribute__((packed)) {
    uint8_t     e_ident[EI_NIDENT];
    Elf64_Half  e_type;
    Elf64_Half  e_machine;
    Elf64_Word  e_version;
    Elf64_Addr  e_entry;         
    Elf64_Off   e_phoff;         
    Elf64_Off   e_shoff;         
    Elf64_Word  e_flags;
    Elf64_Half  e_ehsize;        
    Elf64_Half  e_phentsize;     
    Elf64_Half  e_phnum;         
    Elf64_Half  e_shentsize;
    Elf64_Half  e_shnum;
    Elf64_Half  e_shstrndx;
} Elf64_Ehdr;

typedef struct __attribute__((packed)) {
    Elf64_Word  p_type;
    Elf64_Word  p_flags;
    Elf64_Off   p_offset;        
    Elf64_Addr  p_vaddr;         
    Elf64_Addr  p_paddr;         
    Elf64_Xword p_filesz;        
    Elf64_Xword p_memsz;         
    Elf64_Xword p_align;         
} Elf64_Phdr;

 


#define ELF_OK          0
#define ELF_ERR_IO      (-1)    
#define ELF_ERR_MAGIC   (-2)    
#define ELF_ERR_CLASS   (-3)    
#define ELF_ERR_ENDIAN  (-4)    
#define ELF_ERR_TYPE    (-5)    
#define ELF_ERR_ARCH    (-6)    
#define ELF_ERR_NOMEM   (-7)    
#define ELF_ERR_INVAL   (-8)    
#define ELF_ERR_INTERP  (-9)    

 


#define ELF64_MAX_SEGS  16      

typedef struct {
    Elf64_Addr  vaddr;          
    uint64_t    num_pages;      
    uint64_t    phys_base;      
    bool        exec;
    bool        write;
} elf64_seg_t;

typedef struct {
    Elf64_Addr  entry;          
    Elf64_Addr  load_base;      
    Elf64_Addr  stack_top;      
    uint64_t    stack_pages;    
    uint64_t    stack_phys;     

     







    uint64_t    pml4_phys;

    uint32_t    nseg;
    elf64_seg_t segs[ELF64_MAX_SEGS];
} elf64_image_t;

 



 











int elf64_load(const char *path, elf64_image_t *out);

 




void elf64_free(elf64_image_t *img);

 












int elf64_spawn(const char *path, const char *name, int prio);

 



int elf64_validate_header(const Elf64_Ehdr *ehdr);

#endif  
