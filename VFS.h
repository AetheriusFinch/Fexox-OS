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
#define VFS_MAX_BLKDEVS   8

 


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

#endif  
