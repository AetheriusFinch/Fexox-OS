/* Fexox OS Project, EULA GPLv2 */
#ifndef FXAPP_H
#define FXAPP_H

#include <stdint.h>



#define FXAPP_MAGIC        0x50415846u    
#define FXAPP_VERSION      1


#define FXAPP_TYPE_C_ELF64 1

#define FXAPP_NAME_MAX     64
#define FXAPP_ICON_MAX      64
#define FXAPP_PATH_MAX      96

#pragma pack(push, 1)
typedef struct {
    uint32_t magic;              
    uint16_t version;            
    uint16_t app_type;           
    char     name[FXAPP_NAME_MAX];       
    char     icon[FXAPP_ICON_MAX];       
    char     elf_path[FXAPP_PATH_MAX];   
    uint32_t icon_w, icon_h;     
    uint8_t  reserved[64];       
} fxapp_header_t;
#pragma pack(pop)

typedef struct {
    fxapp_header_t header;
    int             valid;       
} fxapp_t;

 


int fxapp_load(const char *path, fxapp_t *out);

 




int fxapp_launch(const fxapp_t *app, int prio);

#endif  
