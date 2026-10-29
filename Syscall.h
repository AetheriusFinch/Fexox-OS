/* Fexox OS Project, EULA GPLv2 */
 

#ifndef SYSCALL_H
#define SYSCALL_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

 


#define SYS_READ          0
#define SYS_WRITE         1
#define SYS_OPEN          2
#define SYS_CLOSE         3
#define SYS_STAT          4
#define SYS_FSTAT         5
#define SYS_LSEEK         8
#define SYS_MMAP          9
#define SYS_MPROTECT      10
#define SYS_MUNMAP        11
#define SYS_BRK           12
#define SYS_GETPID        20
#define SYS_SCHED_YIELD   24
#define SYS_GETPID2       39    
#define SYS_CLONE         56
#define SYS_FORK          57
#define SYS_EXECVE        59
#define SYS_EXIT          60
#define SYS_WAIT4         61
#define SYS_FCNTL         72
#define SYS_CHDIR         80
#define SYS_RENAME        82
#define SYS_MKDIR         83
#define SYS_RMDIR         84
#define SYS_UNLINK        87
#define SYS_READDIR       89
#define SYS_FTRUNCATE     91
#define SYS_ARCH_PRCTL    158
#define SYS_GET_FRAMEBUFFER 500   
#define SYS_PIPE          502
#define SYS_SHM_GET       503
#define SYS_SHM_AT        504
#define SYS_SHM_DETACH    505
#define SYS_WIN_CREATE    600   
#define SYS_WIN_CLOSE     601   
#define SYS_WIN_MOVE      602   
#define SYS_WIN_GET_RECT  603   
#define SYS_INPUT_POLL    604   
#define SYS_KBD_READ_CHAR 605   
#define SYS_KLOG_READ     880   

#define SYS_NET_SOCKET    700
#define SYS_NET_CONNECT   701
#define SYS_NET_SEND      702
#define SYS_NET_RECV      703
#define SYS_NET_CLOSE     704
#define SYS_NET_RESOLVE   705
#define SYS_NET_STATUS    706

#define SC_NET_SOCK_STREAM 1    
#define SC_NET_SOCK_DGRAM  2    

 
#define SYS_PREAD64       17    
#define SYS_PWRITE64      18    
#define SYS_READV         19    
#define SYS_ACCESS        21    
#define SYS_MREMAP        25    
#define SYS_MINCORE       27    
#define SYS_MADVISE       28    
#define SYS_DUP           32    
#define SYS_DUP2          33    
#define SYS_NANOSLEEP     35    
#define SYS_SENDFILE      40    
#define SYS_UNAME         63    
#define SYS_TRUNCATE      76    
#define SYS_GETCWD        79    
#define SYS_GETTIMEOFDAY  96    
#define SYS_SYSINFO       99    
#define SYS_GETDENTS64    217   
#define SYS_CLOCK_GETTIME 228   
#define SYS_GETRANDOM     318   
#define SYS_WRITEV        506   

 


#define EPERM     1    
#define ENOENT    2    
#define ESRCH     3    
#define EINTR     4    
#define EIO       5    
#define EBADF     9    
#define ECHILD    10   
#define ENOMEM    12   
#define EACCES    13   
#define EFAULT    14   
#define EEXIST    17   
#define EXDEV     18   
#define ENOTDIR   20   
#define EISDIR    21   
#define EINVAL    22   
#define ENFILE    23   
#define EMFILE    24   
#define ENOSPC    28   
#define EROFS     30   
#define EPIPE     32   
#define ESPIPE    29   
#define ERANGE    34   
#define ENOSYS    38   
#define ENOTEMPTY 39   
#define EOVERFLOW 75   

 


#define PROT_NONE   0x00
#define PROT_READ   0x01
#define PROT_WRITE  0x02
#define PROT_EXEC   0x04

#define MAP_SHARED    0x01
#define MAP_PRIVATE   0x02
#define MAP_FIXED     0x10
#define MAP_ANON      0x20
#define MAP_ANONYMOUS 0x20
#define MAP_FAILED    ((void *)(uintptr_t)-1)

 


#define CLONE_VM      0x00000100   
#define CLONE_FS      0x00000200   
#define CLONE_FILES   0x00000400   
#define CLONE_THREAD  0x00010000   

 


#define F_DUPFD   0
#define F_GETFD   1
#define F_SETFD   2
#define F_GETFL   3
#define F_SETFL   4
#define FD_CLOEXEC 1

 


#define ARCH_SET_FS 0x1002
#define ARCH_GET_FS 0x1003
#define ARCH_SET_GS 0x1001
#define ARCH_GET_GS 0x1004

 


typedef struct {
    uint8_t  type;        
    uint64_t size;
    uint64_t alloc_size;
    uint32_t ctime;
    uint32_t mtime;
    uint32_t atime;
    uint32_t attr;
    uint32_t cluster;
} sc_stat_t;

 


#define SHM_MAX_REGIONS  32
#define SHM_MAX_NAME     64

typedef struct {
    char     name[SHM_MAX_NAME];
    uint64_t phys_base;     
    uint64_t virt_kernel;   
    uint64_t size;          
    uint32_t pages;         
    uint32_t refcount;      
    bool     valid;
} shm_region_t;

 


#define PIPE_BUF_SIZE  4096
#define PIPE_MAX       64

typedef struct {
    uint8_t  buf[PIPE_BUF_SIZE];
    uint32_t read_pos;
    uint32_t write_pos;
    uint32_t count;        
    bool     valid;
    bool     write_closed;
    bool     read_closed;
    uint8_t  rd_refs;      
    uint8_t  wr_refs;      
} pipe_t;

 




typedef struct {
    uint64_t phys_addr;    
    uint64_t virt_addr;    
    uint32_t width;        
    uint32_t height;       
    uint32_t pitch;        
    uint32_t bpp;          
    uint8_t  mode;         
     





    uint8_t  r_shift, g_shift, b_shift;
} sc_framebuffer_t;

 





typedef struct {
    int32_t  x, y;
    uint32_t w, h;
    uint32_t redraw_gen;
} sc_win_rect_t;

 








typedef struct {
    int32_t x, y;
    uint8_t buttons;
} sc_input_t;

 



 
typedef struct { int64_t tv_sec; int64_t tv_nsec; } sc_timespec_t;
 
typedef struct { int64_t tv_sec; int64_t tv_usec; } sc_timeval_t;

 
#define SC_CLOCK_REALTIME            0    
#define SC_CLOCK_MONOTONIC           1
#define SC_CLOCK_PROCESS_CPUTIME_ID  2    
#define SC_CLOCK_THREAD_CPUTIME_ID   3
#define SC_CLOCK_MONOTONIC_RAW       4
#define SC_CLOCK_BOOTTIME            7

 
#define SC_UTS_LEN 65
typedef struct {
    char sysname[SC_UTS_LEN];
    char nodename[SC_UTS_LEN];
    char release[SC_UTS_LEN];
    char version[SC_UTS_LEN];
    char machine[SC_UTS_LEN];
    char domainname[SC_UTS_LEN];
} sc_utsname_t;

 
typedef struct { uint64_t iov_base; uint64_t iov_len; } sc_iovec_t;
#define SC_IOV_MAX 1024

 
typedef struct {
    int64_t  uptime;
    uint64_t loads[3];
    uint64_t totalram, freeram, sharedram, bufferram, totalswap, freeswap;
    uint16_t procs;
    uint16_t pad;
    uint32_t pad2;
    uint64_t totalhigh, freehigh;
    uint32_t mem_unit;
    uint32_t pad3;
} sc_sysinfo_t;

 
#define MREMAP_MAYMOVE  1
#define MREMAP_FIXED    2    

 
#define MADV_NORMAL      0
#define MADV_RANDOM      1
#define MADV_SEQUENTIAL  2
#define MADV_WILLNEED    3
#define MADV_DONTNEED    4    
#define MADV_FREE        8
#define MADV_DONTFORK    10
#define MADV_DOFORK      11
#define MADV_HUGEPAGE    14
#define MADV_NOHUGEPAGE  15

 
#define SC_F_OK 0
#define SC_X_OK 1
#define SC_W_OK 2
#define SC_R_OK 4

 
#define GRND_NONBLOCK 1
#define GRND_RANDOM   2
#define GRND_INSECURE 4

 
#define SC_DT_UNKNOWN 0
#define SC_DT_DIR     4
#define SC_DT_REG     8
 


void syscall_init(void);

 




int64_t syscall_dispatch(uint64_t nr,
                         uint64_t a1, uint64_t a2, uint64_t a3,
                         uint64_t a4, uint64_t a5);

 




bool syscall_validate_ptr(const void *ptr, size_t size);

#endif  
