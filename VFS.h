/* Fexox OS Project, EULA GPLv2 */
 


#ifndef VFS_H
#define VFS_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

 


#define VFS_OK          0
#define VFS_ERR_NOMEM   (-1)
#define VFS_ERR_NOENT   (-2)    
#define VFS_ERR_NOTDIR  (-3)    
#define VFS_ERR_ISDIR   (-4)    
#define VFS_ERR_IO      (-5)    
#define VFS_ERR_INVAL   (-6)    
#define VFS_ERR_NOSPC   (-7)    
#define VFS_ERR_EXIST   (-8)    
#define VFS_ERR_NXDEV   (-9)    
#define VFS_ERR_BUSY    (-10)   
#define VFS_ERR_ROFS    (-11)   
#define VFS_ERR_NFILE   (-12)   
#define VFS_ERR_BADF    (-13)   
#define VFS_ERR_OVERFLOW (-14)  
#define VFS_ERR_CORRUPT (-15)   
#define VFS_ERR_UNSUP   (-16)   
#define VFS_ERR_LOOP    (-17)    
#define VFS_ERR_ACCES   (-18)    
#define VFS_ERR_PERM    (-19)    
#define VFS_ERR_NAMETOOLONG (-20)
#define VFS_ERR_NODATA  (-21)    
#define VFS_ERR_RANGE   (-22)    
#define VFS_ERR_XDEV    (-23)    
#define VFS_ERR_NOTEMPTY (-24)   
#define VFS_ERR_MLINK   (-25)    
#define VFS_ERR_2BIG    (-26)

 


#define O_RDONLY    0x0000
#define O_WRONLY    0x0001
#define O_RDWR      0x0002
#define O_ACCMODE   0x0003
#define O_CREAT     0x0040
#define O_TRUNC     0x0200
#define O_APPEND    0x0400
#define O_DIRECTORY 0x0800

 


#define VFS_TYPE_FILE  1
#define VFS_TYPE_DIR   2
#define VFS_TYPE_LINK  3    

 


#define VFS_MAX_NAME      255
#define VFS_MAX_PATH      4096
#define VFS_MAX_FD        256     
#define VFS_MAX_MOUNTS    16
#define VFS_MAX_BLKDEVS   24

 


typedef struct {
    uint8_t  type;           
    uint64_t size;           
    uint64_t alloc_size;     
    uint32_t ctime;          
    uint32_t mtime;          
    uint32_t atime;          
    uint32_t attr;           
    uint32_t cluster;        
} vfs_stat_t;

 


typedef struct {
    char     name[VFS_MAX_NAME + 1];
    uint8_t  type;           
    uint64_t size;
    uint32_t cluster;
} vfs_dirent_t;

 


struct vfs_node;
struct vfs_mount;
struct blkdev;

 



 
typedef struct blkdev_ops {
     
    int (*read_sectors)(struct blkdev *dev, uint64_t lba,
                        uint32_t count, void *buf);
     
    int (*write_sectors)(struct blkdev *dev, uint64_t lba,
                         uint32_t count, const void *buf);
     
    void (*destroy)(struct blkdev *dev);
      
    int (*flush)(struct blkdev *dev);
} blkdev_ops_t;

 
typedef struct blkdev {
    char           name[32];      
    uint32_t       sector_size;   
    uint64_t       sector_count;  
    void          *priv;          
    blkdev_ops_t  *ops;
    bool           readonly;
} blkdev_t;

 
int  blkdev_register(blkdev_t *dev);

 
blkdev_t *blkdev_find(const char *name);

 
int       blkdev_count(void);
blkdev_t *blkdev_at(int idx);

 
int blkdev_read(blkdev_t *dev, uint64_t lba, uint32_t count, void *buf);
int blkdev_write(blkdev_t *dev, uint64_t lba, uint32_t count, const void *buf);

 



 
typedef struct vfs_node_ops {
     

    int64_t (*read)(struct vfs_node *node, uint64_t offset,
                    uint64_t size, void *buf);

     

    int64_t (*write)(struct vfs_node *node, uint64_t offset,
                     uint64_t size, const void *buf);

     


    int (*readdir)(struct vfs_node *node, uint32_t index,
                   vfs_dirent_t *out);

     
    struct vfs_node *(*lookup)(struct vfs_node *parent, const char *name);

     
    struct vfs_node *(*create)(struct vfs_node *parent,
                               const char *name, uint8_t type);

     
    int (*unlink)(struct vfs_node *parent, const char *name);

     
    int (*sync)(struct vfs_node *node);

     
    void (*release)(struct vfs_node *node);

     
    int (*truncate)(struct vfs_node *node, uint64_t new_size);

     
    const struct vfs_node_xops *x;      
} vfs_node_ops_t;

 
typedef struct vfs_node {
    vfs_stat_t      stat;
    vfs_node_ops_t *ops;
    struct vfs_mount *mount;   
    uint32_t        refcount;
    void           *fs_priv;   
} vfs_node_t;

 
typedef struct fs_ops {
     
    vfs_node_t *(*mount)(blkdev_t *dev, uint32_t flags);
     
    void (*umount)(struct vfs_mount *mnt);
     
    const char *name;
} fs_ops_t;

 
typedef struct vfs_mount {
    char        path[VFS_MAX_PATH];   
    vfs_node_t *root;
    blkdev_t   *dev;
    fs_ops_t   *fs;
    uint32_t    flags;                
    void       *fs_priv;              
} vfs_mount_t;

 



 
int  vfs_init(void);

 
int  vfs_register_fs(fs_ops_t *ops);

 




#define MS_RDONLY  1
int  vfs_mount(const char *dev_name, const char *fs_name,
               const char *path, uint32_t flags);

 
int  vfs_umount(const char *path);

 





int  vfs_mount_count(void);
bool vfs_mount_info(int idx, char *path_out, size_t path_sz,
                    char *dev_out, size_t dev_sz);

 

 
int     vfs_open(const char *path, int flags);

 
int     vfs_close(int fd);

 
int64_t vfs_read(int fd, void *buf, uint64_t size);

 
int64_t vfs_write(int fd, const void *buf, uint64_t size);

 
int64_t vfs_seek(int fd, int64_t offset, int whence);

 
int     vfs_stat(const char *path, vfs_stat_t *out);

 
int     vfs_fstat(int fd, vfs_stat_t *out);

 

int     vfs_readdir(int fd, uint32_t index, vfs_dirent_t *out);

 
int     vfs_mkdir(const char *path);

 
int     vfs_unlink(const char *path);

 
int     vfs_rmdir(const char *path);

 
int     vfs_truncate(const char *path, uint64_t size);
int     vfs_ftruncate(int fd, uint64_t size);

 


int fat32_register(void);

 


int virtio_blk_init(void);


 

 

#define VFS_PERM_X  1u
#define VFS_PERM_W  2u
#define VFS_PERM_R  4u

typedef struct {
    uint32_t uid, gid, euid, egid;     
    uint32_t umask;
    uint32_t ngroups;
    uint32_t groups[32];               
} vfs_cred_t;



void              vfs_set_cred_provider(const vfs_cred_t *(*fn)(void));
const vfs_cred_t *vfs_cred(void);

typedef struct {
    uint64_t ino, size, blocks;                  
    uint64_t rdev;
    uint64_t atime_ns, mtime_ns, ctime_ns, btime_ns;
    uint32_t mode, uid, gid, nlink, blksize, dev;
} vfs_attr_t;

#define VFS_SET_MODE  0x01u
#define VFS_SET_UID   0x02u
#define VFS_SET_GID   0x04u
#define VFS_SET_SIZE  0x08u
#define VFS_SET_ATIME 0x10u
#define VFS_SET_MTIME 0x20u
typedef struct {
    uint32_t valid;
    uint32_t mode, uid, gid;
    uint64_t size, atime_ns, mtime_ns;
} vfs_setattr_t;

 
typedef struct vfs_node_xops {
    int (*getattr)(struct vfs_node *n, vfs_attr_t *out);
    int (*setattr)(struct vfs_node *n, const vfs_setattr_t *sa, const vfs_cred_t *cr);
    int (*permission)(struct vfs_node *n, const vfs_cred_t *cr, uint32_t want);    
    int (*create2)(struct vfs_node *parent, const char *name, uint32_t mode, uint64_t rdev,
                   const vfs_cred_t *cr, struct vfs_node **out);                   
    int (*symlink)(struct vfs_node *parent, const char *name, const char *target,
                   const vfs_cred_t *cr, struct vfs_node **out);
    int (*readlink)(struct vfs_node *n, char *buf, size_t sz);                     
    int (*link)(struct vfs_node *parent, const char *name, struct vfs_node *target, const vfs_cred_t *cr);
    int (*unlink2)(struct vfs_node *parent, const char *name, const vfs_cred_t *cr, int rmdir);
    int (*rename)(struct vfs_node *od, const char *on, struct vfs_node *nd, const char *nn,
                  const vfs_cred_t *cr);
    int (*getxattr)(struct vfs_node *n, const char *name, void *buf, size_t sz);
    int (*setxattr)(struct vfs_node *n, const char *name, const void *val, size_t sz, int flags);
    int (*listxattr)(struct vfs_node *n, char *buf, size_t sz);
    int (*removexattr)(struct vfs_node *n, const char *name);
    int (*reflink)(struct vfs_node *src, struct vfs_node *dst);
} vfs_node_xops_t;

 
#define VFS_O_EXCL      0x00080
#define VFS_O_NOFOLLOW  0x20000
#define VFS_O_LXDIR     0x10000       

int  vfs_open2(const char *path, int flags, uint32_t mode);
int  vfs_getattr(const char *path, int follow, vfs_attr_t *out);
int  vfs_fgetattr(int fd, vfs_attr_t *out);
int  vfs_setattr(const char *path, int follow, const vfs_setattr_t *sa);
int  vfs_fsetattr(int fd, const vfs_setattr_t *sa);
int  vfs_access2(const char *path, uint32_t mode, int use_real_ids, int follow);
int  vfs_mkdir2(const char *path, uint32_t mode);
int  vfs_unlink2(const char *path);                   
int  vfs_rmdir2(const char *path);
int  vfs_symlink2(const char *target, const char *linkpath);
int  vfs_readlink2(const char *path, char *buf, size_t sz);    
int  vfs_link2(const char *oldpath, const char *newpath);
int  vfs_rename2(const char *oldpath, const char *newpath, uint32_t flags);    
int  vfs_truncate2(const char *path, uint64_t size);
int  vfs_ftruncate2(int fd, uint64_t size);
int  vfs_fsync2(int fd);
int  vfs_mknod2(const char *path, uint32_t mode, uint64_t rdev);

 
vfs_node_t *vfs_lookup_node(const char *path, int follow, int *err);    
vfs_node_t *vfs_fd_node(int fd);                                         
void        vfs_node_put(vfs_node_t *n);
vfs_mount_t *vfs_mount_of_path(const char *path);
int         vfs_attach(const char *path, vfs_node_t *root, fs_ops_t *fs, blkdev_t *dev, uint32_t flags);
void        vfs_sync_all(void);

int fhfs_register(void);             

#endif  
