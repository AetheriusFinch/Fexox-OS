/* Fexox OS Project, EULA GPLv2 */
#ifndef STORAGE_H
#define STORAGE_H



int  storage_init(void (*log)(const char *));
int  storage_root_is_fhfs(void);

 
int  storage_start_services(void);

 
void storage_poll(void);

#endif
