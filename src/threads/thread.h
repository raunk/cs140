#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H

#include <debug.h>
#include <list.h>
#include <stdint.h>
#include "threads/synch.h"
#include <hash.h>
/* States in a thread's life cycle. */
enum thread_status
  {
    THREAD_RUNNING,     /* Running thread. */
    THREAD_READY,       /* Not running but ready to run. */
    THREAD_BLOCKED,     /* Waiting for an event to trigger. */
    THREAD_DYING        /* About to be destroyed. */
  };

/* Thread identifier type.
   You can redefine this to whatever type you like. */
typedef int tid_t;
#define TID_ERROR ((tid_t) -1)          /* Error value for tid_t. */

/* Thread priorities. */
#define PRI_MIN 0                       /* Lowest priority. */
#define PRI_DEFAULT 31                  /* Default priority. */
#define PRI_MAX 63                      /* Highest priority. */

/* A kernel thread or user process.

   Each thread structure is stored in its own 4 kB page.  The
   thread structure itself sits at the very bottom of the page
   (at offset 0).  The rest of the page is reserved for the
   thread's kernel stack, which grows downward from the top of
   the page (at offset 4 kB).  Here's an illustration:

        4 kB +---------------------------------+
             |          kernel stack           |
             |                |                |
             |                |                |
             |                V                |
             |         grows downward          |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             +---------------------------------+
             |              magic              |
             |                :                |
             |                :                |
             |               name              |
             |              status             |
        0 kB +---------------------------------+

   The upshot of this is twofold:

      1. First, `struct thread' must not be allowed to grow too
         big.  If it does, then there will not be enough room for
         the kernel stack.  Our base `struct thread' is only a
         few bytes in size.  It probably should stay well under 1
         kB.

      2. Second, kernel stacks must not be allowed to grow too
         large.  If a stack overflows, it will corrupt the thread
         state.  Thus, kernel functions should not allocate large
         structures or arrays as non-static local variables.  Use
         dynamic allocation with malloc() or palloc_get_page()
         instead.

   The first symptom of either of these problems will probably be
   an assertion failure in thread_current(), which checks that
   the `magic' member of the running thread's `struct thread' is
   set to THREAD_MAGIC.  Stack overflow will normally change this
   value, triggering the assertion. */
/* The `elem' member has a dual purpose.  It can be an element in
   the run queue (thread.c), or it can be an element in a
   semaphore wait list (synch.c).  It can be used these two ways
   only because they are mutually exclusive: only a thread in the
   ready state is on the run queue, whereas only a thread in the
   blocked state is on a semaphore wait list. */
struct thread
  {
    /* Owned by thread.c. */
    tid_t tid;                      /* Thread identifier. */
    enum thread_status status;      /* Thread state. */
    char name[16];                  /* Name (for debugging purposes). */
    uint8_t *stack;                 /* Saved stack pointer. */
    int priority;                   /* Priority. */
    struct list_elem priority_elem; /* List element for multilevel queues */ 

    int nice;                       /* Nice value between -20 and 20 */
    int recent_cpu;                 /* Measurement of thread's 
                                        recent cpu usage */

    struct list_elem allelem;       /* List element for all threads list. */

    /* Shared between thread.c and synch.c. */
    struct list_elem elem;          /* List element. */
    
    /* List element for priority queue of threads to wakeup */
    struct list_elem wakeup_elem;       
    int64_t wakeup_tick;            /* A time to wake this thread up */
    
    /* Used to implement priority donations. */
    struct thread *t_donating_to;  /* Pointer to the thread that currently 
                                      holds a donation from this thread */
    struct list recvd_donations;   /* List of donations received 
                                      from other threads */
    struct lock *lock_waiting_for; /* Pointer to the lock that this 
                                      thread is currently waiting for */
    /* System call specifics */
    struct list_elem child_elem;    /* Element for putting this thread into 
                                       other threads' lists of children */
    struct list child_list;         /* List of child threads of current
                                       thread */
    struct thread *parent;          /* The parent of the current thread */
    struct semaphore is_loaded_sem; /* Used to signal whether a process 
                                       successfully loaded or not */
    struct semaphore is_dying;      /* Used to signal a parent process 
                                       that might be waiting on cur process */

    struct list file_descriptors;   /* List of open file descriptors */
    int next_fd;
   
    struct hash map_hash;           /* Hash for memory mapping */
    int next_map_id;
 
    int waited_on_by;   /* pid of process waiting on this thread */
    int exit_status;    /* Exit status for this thread */

    struct file* executing_file;

#ifdef USERPROG
    /* Owned by userprog/process.c. */
    uint32_t *pagedir;                  /* Page directory. */
#endif

    /* Owned by thread.c. */
    unsigned magic;                     /* Detects stack overflow. */
  };
  
struct file_descriptor_elem
{
  int fd; /* File descriptor number */
  struct file *f; /* File struct pointer. New one is allocated each time a file
                     is opened, deallocated when the file is closed. */
  struct list_elem elem;
};
  
struct mmap_elem
{
  int map_id; /* Mappping id number */
  void* vaddr; /* Virtual address this was mapped at */
  int length; /* Length of the file that was mapped in memory */
  struct hash_elem elem; 
};


struct donation_elem
{
  struct lock *l; /* Pointer to the lock that, when released, would
                     cause this donation to expire */
  int priority; /* Priority value of the donation */
  struct list_elem elem;
};

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
extern bool thread_mlfqs;

/* Constants for formulas in multi-level feedback queue scheduler */

#define LOAD_AVG_MULTIPLIER 16110
#define LOAD_AVG_READY_MULTIPLIER 273


void thread_init (void);
void thread_start (void);

void thread_tick (void);
void thread_print_stats (void);

typedef void thread_func (void *aux);
tid_t thread_create (const char *name, int priority, thread_func *, void *);

void thread_block (void);
void thread_unblock (struct thread *);

struct thread *thread_current (void);
tid_t thread_tid (void);
const char *thread_name (void);
struct thread *thread_get_by_tid(tid_t tid);
struct thread* thread_get_by_child_tid(tid_t tid);

void thread_exit (void) NO_RETURN;
void thread_yield (void);

/* Performs some operation on thread t, given auxiliary data AUX. */
typedef void thread_action_func (struct thread *t, void *aux);
void thread_foreach (thread_action_func *, void *);

int thread_get_priority (void);
int thread_get_priority_for_thread(struct thread* t);
void thread_set_priority (int);

int thread_get_nice (void);
void thread_set_nice (int);
int thread_get_recent_cpu (void);
int thread_get_load_avg (void);

/* Methods for specifying sleep intervals. */
void thread_add_to_wakeup_list (int64_t wakeup_tick);
void thread_wakeup_sleeping (int64_t ticks);

/* Methods for priority scheduling and priority donation. */
void thread_donate_priority(struct thread* donor_t);
void thread_remove_donations(struct thread* t, struct lock* for_lock);
bool thread_priority_function(const struct list_elem *a, 
                              const struct list_elem* b, void* aux);
void thread_yield_if_not_highest_priority(void);

/* Methods for handling file descriptors. */
struct file_descriptor_elem* thread_add_file_descriptor_elem(struct file *fi);
struct file_descriptor_elem* thread_get_file_descriptor_elem(int fd);

/* Re-compute methods for multi-level feedback queue scheduler */
void thread_compute_priorities(void);
void thread_compute_load_average(void);
void thread_compute_recent_cpu(void);

// Memory mapping functions */
void thread_setup_mmap(struct thread* t);
#endif /* threads/thread.h */
