/* Fexox OS Project, EULA GPLv2 */
#ifndef STORAGE_H
#define STORAGE_H

/* Registers fhfs + fat32, finds/creates the FHFS root and mounts "/" (FHFS), /boot and /mnt/<dev>.
   Returns 1 when a root filesystem is mounted, 0 otherwise. `log` prints to the boot console. */
int  storage_init(void (*log)(const char *));
int  storage_root_is_fhfs(void);

/* Call after sched_init(): starts fhfs-sync (delayed atomic commits) and storaged (USB automount). */
int  storage_start_services(void);

/* One scan pass for newly appeared disks/partitions. */
void storage_poll(void);

#endif
