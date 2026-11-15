/* Fexox OS Project, EULA GPLv2 */


















#ifndef FEXOXHYBRIDFS_H
#define FEXOXHYBRIDFS_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "VFS.h"

 
#define FH_S_IFMT   0170000u
#define FH_S_IFSOCK 0140000u
#define FH_S_IFLNK  0120000u
#define FH_S_IFREG  0100000u
#define FH_S_IFBLK  0060000u
#define FH_S_IFDIR  0040000u
#define FH_S_IFCHR  0020000u
#define FH_S_IFIFO  0010000u
#define FH_S_ISUID  04000u
#define FH_S_ISGID  02000u
#define FH_S_ISVTX  01000u
#define FH_S_ISDIR(m) (((m) & FH_S_IFMT) == FH_S_IFDIR)
#define FH_S_ISREG(m) (((m) & FH_S_IFMT) == FH_S_IFREG)
#define FH_S_ISLNK(m) (((m) & FH_S_IFMT) == FH_S_IFLNK)

#define FH_MAY_EXEC  1u
#define FH_MAY_WRITE 2u
#define FH_MAY_READ  4u

 
#define FH_OK            0
#define FH_EIO          (-1)
#define FH_ENOENT       (-2)
#define FH_ENOSPC       (-3)
#define FH_ENOMEM       (-4)
#define FH_EROFS        (-5)
#define FH_EISDIR       (-6)
#define FH_ENOTDIR      (-7)
#define FH_EEXIST       (-8)
#define FH_ENOTEMPTY    (-9)
#define FH_EINVAL       (-10)
#define FH_EACCES       (-11)
#define FH_ENAMETOOLONG (-12)
#define FH_ECORRUPT     (-13)    
#define FH_EPERM        (-14)
#define FH_EMLINK       (-15)
#define FH_ERANGE       (-16)
#define FH_ENODATA      (-17)
#define FH_E2BIG        (-18)
#define FH_EBUSY        (-19)

#define FHFS_NAME_MAX   255
#define FHFS_MAX_GROUPS 32

 
#define FH_MEDIA_HDD    1
#define FH_MEDIA_SSD    2
#define FH_MEDIA_FLASH  3

 
#define FHFS_MNT_NOCOMPRESS  0x100u

typedef struct fhfs       fhfs_t;
typedef struct fhfs_inode fhfs_inode_t;

typedef struct {
    uint32_t uid, gid;
    uint32_t ngroups;
    uint32_t groups[FHFS_MAX_GROUPS];
} fhfs_cred_t;

typedef struct {
    uint64_t ino;
    uint32_t mode, uid, gid, nlink;
    uint64_t size, blocks;              
    uint64_t rdev;
    uint64_t atime_ns, mtime_ns, ctime_ns, btime_ns;
    uint32_t flags;
    uint32_t blksize;
} fhfs_attr_t;

#define FH_SET_MODE  0x01u
#define FH_SET_UID   0x02u
#define FH_SET_GID   0x04u
#define FH_SET_SIZE  0x08u
#define FH_SET_ATIME 0x10u
#define FH_SET_MTIME 0x20u
typedef struct {
    uint32_t valid;
    uint32_t mode, uid, gid;
    uint64_t size;
    uint64_t atime_ns, mtime_ns;
} fhfs_setattr_t;

typedef struct {
    uint64_t total_blocks, used_blocks, free_blocks;
    uint32_t block_size, record_size, media;
    uint64_t txg;
    uint32_t checksum_errors;
} fhfs_stat_t;

 
#pragma pack(push, 1)

typedef struct {                  
    uint64_t blk;                 
    uint32_t clen;                
    uint32_t lsize;               
    uint64_t cksum;               
    uint32_t birth;               
    uint8_t  comp;                
    uint8_t  flags;
    uint16_t pad;
} fh_bp_t;

typedef struct {                  
    uint64_t magic;
    uint32_t version, blk_size;
    uint64_t total_blocks;
    uint32_t media, rec_shift;
    uint32_t seg_blocks, flags;
    uint64_t data_start, data_end;
    uint64_t meta_start, meta_blocks;
    uint8_t  uuid[16];
    uint64_t created_ns;
    char     label[32];
    uint64_t cksum;               
} fh_label_t;

typedef struct {                  
    uint64_t magic;
    uint64_t txg;
    uint64_t time_ns;
    fh_bp_t  inode_root;
    uint32_t inode_depth, pad0;
    uint64_t used_blocks;
    uint64_t reserved0;
    fh_bp_t  snap_root;           
    fh_bp_t  reserved[2];
    uint64_t cksum;
} fh_uber_t;

typedef struct {                  
    uint32_t mode, uid, gid, nlink;
    uint64_t size;
    uint64_t blocks4k;
    uint64_t rdev;
    uint64_t atime, mtime, ctime, btime;    
    uint32_t flags, gen;
    fh_bp_t  data_root;
    uint8_t  data_depth, rec_shift;
    uint8_t  pad[6];
    fh_bp_t  xattr_bp;
    uint64_t parent;              
    uint8_t  reserved[88];
    uint32_t cksum;
} fh_dinode_t;

#pragma pack(pop)

 

 
int  fhfs_register(void);

 
int  fhfs_format(blkdev_t *dev, const char *label, uint32_t media);
 
bool fhfs_probe(blkdev_t *dev);

fhfs_t *fhfs_mount_dev(blkdev_t *dev, uint32_t flags, int *err);
int     fhfs_sync(fhfs_t *fs);             
int     fhfs_umount_fs(fhfs_t *fs);
void    fhfs_tick(void);
int     fhfs_start_syncd(void);                               
void    fhfs_statfs(fhfs_t *fs, fhfs_stat_t *out);

 
int     fhfs_snapshot_create(fhfs_t *fs, const char *name);
int     fhfs_snapshot_delete(fhfs_t *fs, const char *name);
int     fhfs_snapshot_list(fhfs_t *fs, uint32_t idx, char *name48, uint64_t *txg, uint64_t *time_ns);
fhfs_t *fhfs_snapshot_mount(fhfs_t *fs, const char *name, int *err);    
int     fhfs_reflink(fhfs_inode_t *src, fhfs_inode_t *dst);             

fhfs_inode_t *fhfs_root(fhfs_t *fs);
fhfs_inode_t *fhfs_iref(fhfs_inode_t *ip);
void          fhfs_iput(fhfs_inode_t *ip);

int fhfs_lookup(fhfs_inode_t *dir, const char *name, const fhfs_cred_t *cr, fhfs_inode_t **out);
int fhfs_create(fhfs_inode_t *dir, const char *name, uint32_t mode, uint32_t uid, uint32_t gid,
                uint64_t rdev, const fhfs_cred_t *cr, fhfs_inode_t **out);
int fhfs_symlink(fhfs_inode_t *dir, const char *name, const char *target,
                 uint32_t uid, uint32_t gid, const fhfs_cred_t *cr, fhfs_inode_t **out);
int fhfs_readlink(fhfs_inode_t *ip, char *buf, size_t sz);
int fhfs_link(fhfs_inode_t *dir, const char *name, fhfs_inode_t *target, const fhfs_cred_t *cr);
int fhfs_unlink(fhfs_inode_t *dir, const char *name, const fhfs_cred_t *cr);
int fhfs_rmdir(fhfs_inode_t *dir, const char *name, const fhfs_cred_t *cr);
int fhfs_rename(fhfs_inode_t *odir, const char *oname, fhfs_inode_t *ndir, const char *nname,
                const fhfs_cred_t *cr);

int64_t fhfs_read(fhfs_inode_t *ip, uint64_t off, uint64_t len, void *buf);
int64_t fhfs_write(fhfs_inode_t *ip, uint64_t off, uint64_t len, const void *buf);
int     fhfs_truncate(fhfs_inode_t *ip, uint64_t size);
int     fhfs_fsync(fhfs_inode_t *ip);

 
int fhfs_readdir(fhfs_inode_t *dir, uint32_t index, char *name, size_t name_sz,
                 uint64_t *ino, uint8_t *type);

int fhfs_getattr(fhfs_inode_t *ip, fhfs_attr_t *out);
int fhfs_setattr(fhfs_inode_t *ip, const fhfs_setattr_t *sa, const fhfs_cred_t *cr);
int fhfs_permission(fhfs_inode_t *ip, const fhfs_cred_t *cr, uint32_t want);

int fhfs_getxattr(fhfs_inode_t *ip, const char *name, void *buf, size_t sz);
int fhfs_setxattr(fhfs_inode_t *ip, const char *name, const void *val, size_t sz, int flags);
int fhfs_listxattr(fhfs_inode_t *ip, char *buf, size_t sz);
int fhfs_removexattr(fhfs_inode_t *ip, const char *name);

uint64_t fhfs_ino(fhfs_inode_t *ip);
uint32_t fhfs_mode(fhfs_inode_t *ip);
uint64_t fhfs_size(fhfs_inode_t *ip);
int      fhfs_errno(int fh_err);           

 
uint64_t fhfs_time_ns(void);
int      blkdev_flush(blkdev_t *dev);

 
int fhfs_node_statfs(vfs_node_t *n, fhfs_stat_t *out);             
int fhfs_vfs_snapshot(int cmd, const char *path, const char *name, const char *at, uint32_t idx, char *out48);

#endif  
