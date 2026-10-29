/* Fexox OS Project, EULA GPLv2 */
#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>


 
#define SCHED_MAX_TASKS     1024

 
#define SCHED_KSTACK_SIZE   (16 * 1024)

 
#define SCHED_TIMESLICE_NS  4000000ULL

 
#define NICE_0_WEIGHT       1024

 
#define SCHED_PRIO_MIN   -20
#define SCHED_PRIO_MAX    19
#define SCHED_PRIO_NORMAL  0

 


typedef enum {
    TASK_RUNNING   = 0,   
    TASK_RUNNABLE  = 1,   
    TASK_SLEEPING  = 2,   
    TASK_ZOMBIE    = 3,   
    TASK_DEAD      = 4,   
} task_state_t;

 





typedef struct {
    uint64_t rsp;    
    uint64_t cr3;    
     
    uint64_t rbx;
    uint64_t rbp;
    uint64_t r12;
    uint64_t r13;
    uint64_t r14;
    uint64_t r15;
    uint64_t rflags;
} task_context_t;

 


typedef struct task {
     
    uint32_t        tid;             
    char            name[32];        

     
    task_state_t    state;

     
    uint64_t        vruntime;        
    uint64_t        start_time;      
    int             priority;        
    uint32_t        weight;          

     
    void           *kstack_base;     
    uint64_t        kstack_size;     
    uint64_t        kstack_top;      

     
    task_context_t  ctx;

     
    uint64_t        pml4_phys;       

     
    struct task    *next_all;
    struct task    *prev_all;

     
    int             heap_idx;

     
    uint64_t        total_runtime;   
    uint64_t        switches;        
} task_t;

 



 
int  sched_init(void);

 





int  sched_create_kthread(void (*fn)(void *), void *arg,
                          const char *name, int prio);

 
void sched_exit(void);

 
void sched_yield(void);

 
void sched_sleep_ns(uint64_t ns);

 
void sched_wake(uint32_t tid);

 
task_t *sched_current(void);

 
task_t *sched_find(uint32_t tid);

 
uint64_t sched_tick(uint64_t frame_rsp);

 
uint32_t sched_task_count(void);

#endif  
