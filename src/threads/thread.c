#include "threads/thread.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/switch.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "threads/malloc.h"
#include "threads/fixed-point.h"

#ifdef USERPROG
#include "userprog/process.h"
#include "userprog/syscall.h"
#endif

#ifdef DEBUG
#define debug(fmt, ...)  printf(fmt, __VA_ARGS__)
#else
#define debug(fmt, ...)  do {} while (0)
#endif

/* Random value for struct thread's `magic' member.
   Used to detect stack overflow.  See the big comment at the top
   of thread.h for details. */
#define THREAD_MAGIC 0xcd6abf4b

/* List of processes in THREAD_READY state, that is, processes
   that are ready to run but not actually running. */
static struct list ready_list;

/* List of all processes.  Processes are added to this list
   when they are first scheduled and removed when they exit. */
static struct list all_list;

/* Priority queue of threads prioritized by wakeup time. */
static struct list wakeup_list;

/* Idle thread. */
static struct thread *idle_thread;

/* Initial thread, the thread running init.c:main(). */
static struct thread *initial_thread;

/* Lock used by allocate_tid(). */
static struct lock tid_lock;

/* Stack frame for kernel_thread(). */
struct kernel_thread_frame 
  {
    void *eip;                  /* Return address. */
    thread_func *function;      /* Function to call. */
    void *aux;                  /* Auxiliary data for function. */
  };

/* Statistics. */
static long long idle_ticks;    /* # of timer ticks spent idle. */
static long long kernel_ticks;  /* # of timer ticks in kernel threads. */
static long long user_ticks;    /* # of timer ticks in user programs. */

/* Scheduling. */
#define TIME_SLICE 4            /* # of timer ticks to give each thread. */
static unsigned thread_ticks;   /* # of timer ticks since last yield. */

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
bool thread_mlfqs;

/* Estimate for number of threads ready to run over the past minute.
   This number is actually a 17.14 fixed point integer. */
static int load_avg;

/* Array of PRI_MAX + 1 queues for the multi-level feedback
   queue scheduler */
static struct list queue_list[PRI_MAX+1];

/* This represents the total number of ready threads, or the total size
  all all threads in the queue_list */
static int mlfqs_queue_size;

static void kernel_thread (thread_func *, void *aux);

static void idle (void *aux UNUSED);
static struct thread *running_thread (void);
static struct thread *next_thread_to_run (void);
static void init_thread (struct thread *, const char *name, int priority);
static bool is_thread (struct thread *) UNUSED;
static void *alloc_frame (struct thread *, size_t size);
static void schedule (void);
void thread_schedule_tail (struct thread *prev);
static tid_t allocate_tid (void);
bool thread_wakeup_tick_less_func (const struct list_elem *a, 
                                  const struct list_elem *b, void *aux);
bool thread_priority_function(const struct list_elem *a, 
                              const struct list_elem* b, void * aux);
bool thread_donation_priority_less_func (const struct list_elem *a, 
                              const struct list_elem *b, void *aux);
int thread_get_priority_for_thread(struct thread* t);
void thread_yield_if_not_highest_priority(void);
void thread_reinsert_into_list(struct thread *t, struct list *list);
void thread_initialize_priority_queues(void);
void thread_compute_recent_cpu_for_thread(struct thread* t, void *aux);
void thread_compute_priority_for_thread(struct thread* t, void *aux UNUSED);
struct file_descriptor_elem* thread_get_file_descriptor_elem(int fd);
void thread_free_file_descriptor_elems(struct thread* t);

static void thread_add_to_queue(struct thread* t);

static struct thread * thread_pop_max_priority_list(void);

static unsigned mmap_hash_fn (const struct hash_elem *p_, void *aux UNUSED);
static bool mmap_less_fn (const struct hash_elem *a_, const struct hash_elem *b_,
  void *aux UNUSED);
  
static unsigned
mmap_hash_fn(const struct hash_elem *p_, void *aux UNUSED)
{
  const struct mmap_elem* e = hash_entry(p_, struct mmap_elem, elem);
  return hash_int(e->map_id);
}

static bool
mmap_less_fn(const struct hash_elem *a_, const struct hash_elem *b_,
           void *aux UNUSED)
{
  const struct mmap_elem *a = hash_entry(a_, struct mmap_elem, elem);
  const struct mmap_elem *b = hash_entry(b_, struct mmap_elem, elem); 

  return a->map_id < b->map_id;
}


/* Initializes the threading system by transforming the code
   that's currently running into a thread.  This can't work in
   general and it is possible in this case only because loader.S
   was careful to put the bottom of the stack at a page boundary.

   Also initializes the run queue and the tid lock.

   After calling this function, be sure to initialize the page
   allocator before trying to create any threads with
   thread_create().

   It is not safe to call thread_current() until this function
   finishes. */
void
thread_init (void) 
{
  ASSERT (intr_get_level () == INTR_OFF);

  lock_init (&tid_lock);
  list_init (&ready_list);
  list_init (&all_list);
  list_init (&wakeup_list);

  if(thread_mlfqs)
  {
    thread_initialize_priority_queues();
    mlfqs_queue_size = 0; 
  }

  /* Set up a thread structure for the running thread. */
  initial_thread = running_thread ();
  init_thread (initial_thread, "main", PRI_DEFAULT);
  initial_thread->status = THREAD_RUNNING;
  initial_thread->tid = allocate_tid ();
}

/* Initialize the queues for the multi-level feedback queue
   scheduler */
void 
thread_initialize_priority_queues(void)
{
    int i;
    for(i = 0; i <= PRI_MAX; i++)
    {
        list_init(&queue_list[i]);      
    }
}

/* Get the thread given by the tid TID */
struct thread *
thread_get_by_tid(tid_t tid)
{
  struct list_elem *e;
  for (e = list_begin (&all_list); e != list_end (&all_list);
       e = list_next (e))
    {
      struct thread *t = list_entry (e, struct thread, allelem);
      if(t->tid == tid)
        return t;
    }

  return NULL;
}


/* Insert thread T into the queue_list in bucket for its current priority */
static void
thread_add_to_queue(struct thread* t)
{
  struct list* cur_list = &queue_list[t->priority];
  list_push_back(cur_list, &t->priority_elem);
  mlfqs_queue_size++; 
}


/* Compute a priority for the current thread using the formula
    priority = PRI_MAX - (recent_cpu / 4) - (nice * 2) */
void
thread_compute_priority_for_thread(struct thread* t, void *aux UNUSED)
{
  int cpu_part = fp_fixed_to_integer_zero(
                  fp_divide_integer(t->recent_cpu, 4));
  t->priority = PRI_MAX - cpu_part - (t->nice * 2);
  if(t->priority < PRI_MIN)
  {
    t->priority = PRI_MIN; 
  }

  if(t->priority > PRI_MAX)
  {
    t->priority = PRI_MAX;
  }
}


/* Compute the priority for all of the threads. This is written as a simple
   wrapper to the thread foreach function */
void 
thread_compute_priorities(void)
{
  thread_foreach(thread_compute_priority_for_thread, NULL);
}

/* Compute the load average for the system according to the formula

      load_avg = (59/60)*load_avg + (1/60)*ready_threads

   For this multiplication these constants have been converted to fixed point
   integer format. The number of ready threads is the number of threads on
   all of the queues */
void 
thread_compute_load_average(void)
{
  int left = fp_multiply(LOAD_AVG_MULTIPLIER, load_avg);
  int running_thread = 1;
  if( thread_current () == idle_thread)
    running_thread = 0;
  int right = fp_multiply_integer(LOAD_AVG_READY_MULTIPLIER, 
                          mlfqs_queue_size + running_thread);
  load_avg = fp_add(left, right);
}

/* Compute recent cpu according to the formula:

    recent_cpu = (2*load_avg)/(2*load_avg + 1) * recent_cpu + nice
  
  the multiplier (2*load_avg)/(2*load_avg + 1) has already been computed and 
  is passed in using the aux parameter, since it is the same for all threads */
void 
thread_compute_recent_cpu_for_thread(struct thread* t, void *aux)
{
  int multiplier = *(int*)aux;
  int left = fp_multiply(multiplier, t->recent_cpu);
  int result = fp_add_integer(left, t->nice);
  t->recent_cpu = result;
}

/* Compute the recent cpu for all threads. Computer the multiplier for load
   average that is shared among all threads to pass in to the thread for each
   callback, which will compute the recent cpu for that thread */
void thread_compute_recent_cpu(void)
{
  int top = fp_multiply_integer(load_avg, 2);
  int bot = fp_add_integer(fp_multiply_integer(load_avg, 2), 1);
  int multiplier = fp_divide(top, bot);
  thread_foreach(thread_compute_recent_cpu_for_thread, &multiplier);
}

/* Starts preemptive thread scheduling by enabling interrupts.
   Also creates the idle thread. */
void
thread_start (void) 
{
  /* Create the idle thread. */
  struct semaphore idle_started;
  sema_init (&idle_started, 0);
  thread_create ("idle", PRI_MIN, idle, &idle_started);

  /* Start preemptive thread scheduling. */
  intr_enable ();

  /* Wait for the idle thread to initialize idle_thread. */
  sema_down (&idle_started);
}

/* Compare two donation_elems by the priority donated. This
   comparator is used to maintain sorted lists of donations
   in each thread. */
bool 
thread_donation_priority_less_func (const struct list_elem *a, 
          const struct list_elem *b, void *aux UNUSED)
{
  struct donation_elem* d1 = list_entry(a, struct donation_elem, elem);
  struct donation_elem* d2 = list_entry(b, struct donation_elem, elem);

  return d1->priority > d2->priority;
}

/* Reinsert an element into a list of threads ordered by priority. */
void
thread_reinsert_into_list(struct thread *t, struct list *list)
{
  struct list_elem *elem = &t->elem;
  list_remove(elem);
  list_insert_ordered(list, elem, thread_priority_function, NULL);
}

/* The current thread donates priority to some specified thread
   t_donor. We must perform nested donation so we that the current
   thread's priority is also passed to any threads (and their children)
   on which t_donor is waiting. */
void
thread_donate_priority(struct thread* t_donor)
{  
  /* process nested donations */
  struct thread *t_rec_prev = t_donor;
  struct thread *t_rec = t_donor->t_donating_to;
  int priority_to_donate = thread_get_priority_for_thread(t_donor);

  while (t_rec != NULL) {
    int t_rec_old_priority = thread_get_priority_for_thread(t_rec);
    
    /* add donation elem for from to rec_t's recvd_donations */
    struct donation_elem* cur_elem = (struct donation_elem*) malloc(
                                      sizeof(struct donation_elem));

    if(cur_elem == NULL)
    {
      return;
    }

    cur_elem->l = t_rec_prev->lock_waiting_for;
    cur_elem->elem.prev = NULL;
    cur_elem->elem.next = NULL;
    cur_elem->priority = priority_to_donate;
    
    list_insert_ordered (&t_rec->recvd_donations, &cur_elem->elem, 
                        thread_donation_priority_less_func, NULL);
    
    /* If thread priority increases and is in the ready list, 
       re-insert it into the ready list so that the ready list 
       remains sorted. */
    if (priority_to_donate > t_rec_old_priority 
          && t_rec->status == THREAD_READY) 
    {
      thread_reinsert_into_list(t_rec, &ready_list);
    }
    
    t_rec_prev = t_rec;
    t_rec = t_rec->t_donating_to;
  }
}

/* Removes and frees all the priority donations made to the specified thread
   for a specific lock. If for_lock is NULL, then removes and frees all
   priority donations. Called when a thread releases a lock or exits. */
void
thread_remove_donations(struct thread* t, struct lock* for_lock)
{
  struct list_elem *e;
  struct list *recvd_donations = &t->recvd_donations;
  for(e = list_begin(recvd_donations); e != list_end(recvd_donations);)
  {
    struct donation_elem *donation = list_entry(e, struct donation_elem, 
                                                elem);
    struct list_elem *next_e = list_next(e);
    if (for_lock == NULL || donation->l == for_lock) {
      list_remove(e);
      free(donation);
    }
    e = next_e;
  }
}

/* Allocates a new file_descriptor_elem to wrap the given file struct with
   a file descriptor number and assigns to the current thread. The file
   descriptor number is unique to the current thread. */
struct file_descriptor_elem*
thread_add_file_descriptor_elem(struct file *fi)
{
  struct thread* cur = thread_current();
  struct file_descriptor_elem* fd_elem =
      (struct file_descriptor_elem*) malloc( sizeof(struct file_descriptor_elem));

  if(fd_elem == NULL) 
  {
    exit_current_process(-1);
  }
  
  fd_elem->fd = cur->next_fd++;
  fd_elem->f = fi;
  list_push_front (&cur->file_descriptors, &fd_elem->elem);
  return fd_elem;
}

/* Returns the file_descriptor_elem held by the current thread with the
   given file descriptor number fd. Returns NULL if no such thread found. */
struct file_descriptor_elem*
thread_get_file_descriptor_elem(int fd)
{
  struct list* l = &thread_current()->file_descriptors;
  struct list_elem *e;
  for (e = list_begin (l); e != list_end (l); e = list_next (e)) {
    struct file_descriptor_elem *fd_elem =
        list_entry (e, struct file_descriptor_elem, elem);
    if (fd_elem->fd == fd)
      return fd_elem;
  }
  return NULL;
}

/* Frees each file_descriptor_elem held by the given thread. */
void
thread_free_file_descriptor_elems(struct thread* t)
{
  struct list* l = &t->file_descriptors;
  struct list_elem *e;
  for (e = list_begin (l); e != list_end (l);) {
    struct file_descriptor_elem *fd_elem =
        list_entry (e, struct file_descriptor_elem, elem);
    struct list_elem *next_e = list_next(e);
    safe_file_close(fd_elem->f);
    free(fd_elem);
    e = next_e;
  }
}

/* Called by the timer interrupt handler at each timer 
   tick.  It wakes up threads that may have been sleeping that
   were scheduled to wake up at this tick. We check the ordered
   list WAKEUP_LIST, and since it is ordered, we only need to 
   consider elements at the front of the list. Since it is possible
   multiple threads need to wake up at the same tick, we loop. 
   If we determine this thread should be woken up,
   we remove it from the queue WAKEUP_LIST */
void
thread_wakeup_sleeping (int64_t ticks)
{
  while(!list_empty(&wakeup_list))
  {
    struct list_elem *first = list_front(&wakeup_list);
    struct thread* t = list_entry(first, struct thread, wakeup_elem);
    if(ticks >= t->wakeup_tick)
    {
       list_pop_front(&wakeup_list);
       thread_unblock(t);
    }
    else
    {
      return;
    }
  }
}

/* Called by the timer interrupt handler at each timer tick.
   Thus, this function runs in an external interrupt context. */
void
thread_tick (void) 
{
  struct thread *t = thread_current ();

  /* Update statistics. */
  if (t == idle_thread)
    idle_ticks++;
#ifdef USERPROG
  else if (t->pagedir != NULL)
    user_ticks++;
#endif
  else
    kernel_ticks++;

  /* Enforce preemption. */
  if (++thread_ticks >= TIME_SLICE)
    intr_yield_on_return ();
}

/* Prints thread statistics. */
void
thread_print_stats (void) 
{
  printf ("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
          idle_ticks, kernel_ticks, user_ticks);
}

/* Creates a new kernel thread named NAME with the given initial
   PRIORITY, which executes FUNCTION passing AUX as the argument,
   and adds it to the ready queue.  Returns the thread identifier
   for the new thread, or TID_ERROR if creation fails.

   If thread_start() has been called, then the new thread may be
   scheduled before thread_create() returns.  It could even exit
   before thread_create() returns.  Contrariwise, the original
   thread may run for any amount of time before the new thread is
   scheduled.  Use a semaphore or some other form of
   synchronization if you need to ensure ordering.

   The code provided sets the new thread's `priority' member to
   PRIORITY, but no actual priority scheduling is implemented.
   Priority scheduling is the goal of Problem 1-3. */
tid_t
thread_create (const char *name, int priority,
               thread_func *function, void *aux) 
{
  struct thread *t;
  struct kernel_thread_frame *kf;
  struct switch_entry_frame *ef;
  struct switch_threads_frame *sf;
  tid_t tid;
  enum intr_level old_level;

  ASSERT (function != NULL);

  /* Allocate thread. */
  t = palloc_get_page (PAL_ZERO);
  if (t == NULL)
    return TID_ERROR;

  /* Initialize thread. */
  init_thread (t, name, priority);
  tid = t->tid = allocate_tid ();

  /* Prepare thread for first run by initializing its stack.
     Do this atomically so intermediate values for the 'stack' 
     member cannot be observed. */
  old_level = intr_disable ();

  /* Stack frame for kernel_thread(). */
  kf = alloc_frame (t, sizeof *kf);
  kf->eip = NULL;
  kf->function = function;
  kf->aux = aux;

  /* Stack frame for switch_entry(). */
  ef = alloc_frame (t, sizeof *ef);
  ef->eip = (void (*) (void)) kernel_thread;

  /* Stack frame for switch_threads(). */
  sf = alloc_frame (t, sizeof *sf);
  sf->eip = switch_entry;
  sf->ebp = 0;
  
  
  intr_set_level (old_level);

  /* Add to run queue. */
  thread_unblock (t);

  return tid;
}

/* Puts the current thread to sleep.  It will not be scheduled
   again until awoken by thread_unblock().

   This function must be called with interrupts turned off.  It
   is usually a better idea to use one of the synchronization
   primitives in synch.h. */
void
thread_block (void) 
{
  ASSERT (!intr_context ());
  ASSERT (intr_get_level () == INTR_OFF);

  thread_current ()->status = THREAD_BLOCKED;
  schedule ();
}

/* Comparator to compare two threads on their time to wake. */
bool 
thread_wakeup_tick_less_func (const struct list_elem *a, 
                              const struct list_elem *b, 
                              void *aux UNUSED)
{
    struct thread* t1 = list_entry(a, struct thread, wakeup_elem);
    struct thread* t2 = list_entry(b, struct thread, wakeup_elem);
    return t1->wakeup_tick < t2->wakeup_tick;
}

/* Add the current thread to the wakeup list for wakeup time
   specified by wakeup_tick */
void
thread_add_to_wakeup_list (int64_t wakeup_tick) 
{
    struct thread* cur_thread = thread_current();
    cur_thread->wakeup_tick = wakeup_tick;

    list_insert_ordered (&wakeup_list, &cur_thread->wakeup_elem, 
                          thread_wakeup_tick_less_func, NULL);
}

/* Comparator to compare two threads on their current effective
   priority.  */
bool thread_priority_function(const struct list_elem *a, 
      const struct list_elem* b, void* aux UNUSED)
{
    struct thread *t1 = list_entry(a, struct thread, elem);
    struct thread *t2 = list_entry(b, struct thread, elem);

    return thread_get_priority_for_thread(t1) > 
           thread_get_priority_for_thread(t2);    
}

/* Transitions a blocked thread T to the ready-to-run state.
   This is an error if T is not blocked.  (Use thread_yield() to
   make the running thread ready.)

   This function does not preempt the running thread.  This can
   be important: if the caller had disabled interrupts itself,
   it may expect that it can atomically unblock a thread and
   update other data. */
void
thread_unblock (struct thread *t) 
{
  enum intr_level old_level;

  ASSERT (is_thread (t));
  
  /*
  * Disable interrupts to change the state of the thread to ready.
  * This needs to happen atomically so that the state stays in sync.
  */
  old_level = intr_disable ();
  ASSERT (t->status == THREAD_BLOCKED);

  if(thread_mlfqs)
  {
    thread_add_to_queue(t);
  }else{
    list_insert_ordered(&ready_list, &t->elem, thread_priority_function, 
                        NULL);
  }

  t->status = THREAD_READY;
  
  /* If the current thread priority is less than this thread's priority,
     yield immediately */
  if(thread_current() != idle_thread && !intr_context() )
  {
    int cur_priority = thread_get_priority();
    int this_priority = thread_get_priority_for_thread(t);
    if(cur_priority < this_priority)
    {
        thread_yield(); 
    }
  }
  intr_set_level (old_level);
  
}

/* Returns the name of the running thread. */
const char *
thread_name (void) 
{
  return thread_current ()->name;
}

/* Returns the running thread.
   This is running_thread() plus a couple of sanity checks.
   See the big comment at the top of thread.h for details. */
struct thread *
thread_current (void) 
{
  struct thread *t = running_thread ();
  
  /* Make sure T is really a thread.
     If either of these assertions fire, then your thread may
     have overflowed its stack.  Each thread has less than 4 kB
     of stack, so a few big automatic arrays or moderate
     recursion can cause stack overflow. */
  ASSERT (is_thread (t));
  ASSERT (t->status == THREAD_RUNNING);

  return t;
}

/* Returns the running thread's tid. */
tid_t
thread_tid (void) 
{
  return thread_current ()->tid;
}

/* Deschedules the current thread and destroys it.  Never
   returns to the caller. */
void
thread_exit (void) 
{
  ASSERT (!intr_context ());

#ifdef USERPROG
  process_exit ();
#endif

  /* Remove thread from all threads list, set our status to dying,
     and schedule another process.  That process will destroy us
     when it calls thread_schedule_tail(). */
  intr_disable ();
  list_remove (&thread_current()->allelem);
  thread_current ()->status = THREAD_DYING;
  schedule ();
  NOT_REACHED ();
}

/* Yields the CPU.  The current thread is not put to sleep and
   may be scheduled again immediately at the scheduler's whim. */
void
thread_yield (void) 
{
  struct thread *cur = thread_current ();
  enum intr_level old_level;
  
  ASSERT (!intr_context ());

  old_level = intr_disable ();
  if (cur != idle_thread) 
  {
    if(thread_mlfqs)
    {
      thread_add_to_queue(cur);
    } else {
      list_insert_ordered(&ready_list, &cur->elem, thread_priority_function,
                          NULL);
    }
  }
  cur->status = THREAD_READY;
  schedule ();
  intr_set_level (old_level);
}

/* Invoke function 'func' on all threads, passing along 'aux'.
   This function must be called with interrupts off. */
void
thread_foreach (thread_action_func *func, void *aux)
{
  struct list_elem *e;

  ASSERT (intr_get_level () == INTR_OFF);

  for (e = list_begin (&all_list); e != list_end (&all_list);
       e = list_next (e))
    {
      struct thread *t = list_entry (e, struct thread, allelem);
      func (t, aux);
    }
}

/* Yield the current thread if some other thread is ready 
   that has a higher priority. */
void
thread_yield_if_not_highest_priority(void)
{
  enum intr_level old_level = intr_disable ();
  if (list_empty(&ready_list))
    return; 
    
  struct thread *next_ready_t = list_entry(list_front(&ready_list), 
                                            struct thread, elem);
  if (thread_get_priority() < thread_get_priority_for_thread(next_ready_t)) {
    thread_yield();
  }
  intr_set_level (old_level);
}

/* Sets the current thread's priority to NEW_PRIORITY. */
void
thread_set_priority (int new_priority) 
{
  thread_current ()->priority = new_priority;
  
  // Yield CPU if new priority of current thread is no longer
  // the highest.
  thread_yield_if_not_highest_priority();
}

/* Returns the thread in the list of child threads of the current thread
   with the given tid. Returns NULL if no thread with that tid found. */
struct thread*
thread_get_by_child_tid(tid_t tid)
{
  struct thread* cur = thread_current ();
  struct list_elem *e;
  for (e = list_begin (&cur->child_list); e != list_end (&cur->child_list);
       e = list_next (e))
    {
      struct thread *thr = list_entry (e, struct thread, child_elem);
      if(thr->tid == tid)
        return thr;
    }
  return NULL;
}

/* Returns the priority for T, the thread passed as a parameter 
   A thread's priority is the maximum of any donations and its
   current set priority.
*/
int
thread_get_priority_for_thread(struct thread* t)
{
  int priority = t->priority;
  if (!list_empty(&t->recvd_donations)) {
    struct donation_elem *d_elem =
        list_entry(list_front(&t->recvd_donations), struct donation_elem, elem);
    if (d_elem->priority > priority)
      priority = d_elem->priority;
  }
  return priority;
}

/* Returns the current thread's priority. */
int
thread_get_priority (void) 
{
  return thread_get_priority_for_thread(thread_current ());
}

/* Sets the current thread's nice value to NICE. */
void
thread_set_nice (int nice UNUSED) 
{
  thread_current ()->nice = nice;
  thread_compute_priority_for_thread(thread_current (), NULL);
  // TODO: if no longer has highest priority, yield
}

/* Returns the current thread's nice value. */
int
thread_get_nice (void) 
{
  return thread_current ()->nice;
}

/* Returns 100 times the system load average. */
int
thread_get_load_avg (void) 
{
  return fp_fixed_to_integer_zero(fp_multiply_integer(load_avg, 100));
}

/* Returns 100 times the current thread's recent_cpu value. */
int
thread_get_recent_cpu (void) 
{
  return fp_fixed_to_integer_zero(fp_multiply_integer(
                    thread_current ()->recent_cpu, 100));
}

/* Return the thread from the highest non-empty queue. This method should
   only be called when the size of the entire queue_list is greater than 0 */
static struct thread *
thread_pop_max_priority_list(void)
{
  ASSERT(mlfqs_queue_size > 0);
  int cur_priority;
  for(cur_priority = PRI_MAX; cur_priority >= 0; cur_priority--)
  {
    struct list* cur_list = &queue_list[cur_priority];
    if(!list_empty(cur_list))
    {
      mlfqs_queue_size--;
      return list_entry(list_pop_front(cur_list), 
                        struct thread, priority_elem);
    }
  }
  return NULL;  // Should not reach here
}


/* Idle thread.  Executes when no other thread is ready to run.

   The idle thread is initially put on the ready list by
   thread_start().  It will be scheduled once initially, at which
   point it initializes idle_thread, "up"s the semaphore passed
   to it to enable thread_start() to continue, and immediately
   blocks.  After that, the idle thread never appears in the
   ready list.  It is returned by next_thread_to_run() as a
   special case when the ready list is empty. */
static void
idle (void *idle_started_ UNUSED) 
{
  struct semaphore *idle_started = idle_started_;
  idle_thread = thread_current ();
  sema_up (idle_started);

  for (;;) 
    {
      /* Let someone else run. */
      intr_disable ();
      thread_block ();

      /* Re-enable interrupts and wait for the next one.

         The `sti' instruction disables interrupts until the
         completion of the next instruction, so these two
         instructions are executed atomically.  This atomicity is
         important; otherwise, an interrupt could be handled
         between re-enabling interrupts and waiting for the next
         one to occur, wasting as much as one clock tick worth of
         time.

         See [IA32-v2a] "HLT", [IA32-v2b] "STI", and [IA32-v3a]
         7.11.1 "HLT Instruction". */
      asm volatile ("sti; hlt" : : : "memory");
    }
}

/* Function used as the basis for a kernel thread. */
static void
kernel_thread (thread_func *function, void *aux) 
{
  ASSERT (function != NULL);

  intr_enable ();       /* The scheduler runs with interrupts off. */
  function (aux);       /* Execute the thread function. */
  thread_exit ();       /* If function() returns, kill the thread. */
}

/* Returns the running thread. */
struct thread *
running_thread (void) 
{
  uint32_t *esp;

  /* Copy the CPU's stack pointer into `esp', and then round that
     down to the start of a page.  Because `struct thread' is
     always at the beginning of a page and the stack pointer is
     somewhere in the middle, this locates the curent thread. */
  asm ("mov %%esp, %0" : "=g" (esp));
  return pg_round_down (esp);
}

/* Returns true if T appears to point to a valid thread. */
static bool
is_thread (struct thread *t)
{
  return t != NULL && t->magic == THREAD_MAGIC;
}

/* Does basic initialization of T as a blocked thread named
   NAME. */
static void
init_thread (struct thread *t, const char *name, int priority)
{
  ASSERT (t != NULL);
  ASSERT (PRI_MIN <= priority && priority <= PRI_MAX);
  ASSERT (name != NULL);

  memset (t, 0, sizeof *t);

  t->status = THREAD_BLOCKED;
  strlcpy (t->name, name, sizeof t->name);
  t->stack = (uint8_t *) t + PGSIZE;
  t->priority = priority;
  t->magic = THREAD_MAGIC;

  /* Set thread nice and recent cpu to 0 */
  t->nice = 0;
  t->recent_cpu = 0;
  
  /* Initialize this thread's list of recvd donations */
  list_init(&t->recvd_donations);
  t->t_donating_to = NULL;
  t->lock_waiting_for = NULL;
  
  /* Initialize structures needed to support file descriptors */
  t->next_fd = 2;
  list_init(&t->file_descriptors);
 

 
  /* Setup thread variables for signaling dying condition */
  list_init(&t->child_list);
  sema_init(&t->is_loaded_sem, 0);
  sema_init(&t->is_dying, 0);
  
  // use running_thread since current thread might not have status
  // set to running yet
  struct thread* cur = running_thread ();
  if(is_thread(cur)) {
    list_push_back(&cur->child_list, &t->child_elem);
    t->parent = cur;
  }
  
  t->waited_on_by = -1; 
  t->exit_status = 0;
  t->load_status = 0; 
  
  list_push_back (&all_list, &t->allelem);
}

/* Initialize structures to support memory mapping */
void thread_setup_mmap(struct thread* t)
{
  t->next_map_id = 0;
  hash_init(&t->map_hash, mmap_hash_fn, mmap_less_fn, NULL);
}


/* Add a memory map entry for this thread, and return back 
 * the map_id number  */
int 
thread_add_mmap_entry(void* vaddr, int length, struct inode* inode)
{
  struct thread* cur = thread_current();
  struct mmap_elem* map_elem = (struct mmap_elem*)malloc(sizeof(struct mmap_elem));

  if(map_elem == NULL)
  {
    exit_current_process(-1);
  }   

  map_elem->inode = inode;
  map_elem->vaddr = vaddr;
  map_elem->length = length;
  map_elem->map_id = cur->next_map_id++;
  
  // We don't need the hash element now
  hash_insert(&cur->map_hash, &map_elem->elem);
  return map_elem->map_id;   
} 

struct mmap_elem*
thread_lookup_mmap_entry(int map_id)
{
  struct mmap_elem map_elem;
  struct hash_elem* e;

  map_elem.map_id = map_id;
  e = hash_find(&thread_current()->map_hash, &map_elem.elem);
  return e != NULL ? hash_entry(e, struct mmap_elem, elem): NULL;
}

/* Allocates a SIZE-byte frame at the top of thread T's stack and
   returns a pointer to the frame's base. */
static void *
alloc_frame (struct thread *t, size_t size) 
{
  /* Stack data is always allocated in word-size units. */
  ASSERT (is_thread (t));
  ASSERT (size % sizeof (uint32_t) == 0);

  t->stack -= size;
  return t->stack;
}

/* Chooses and returns the next thread to be scheduled.  Should
   return a thread from the run queue, unless the run queue is
   empty.  (If the running thread can continue running, then it
   will be in the run queue.)  If the run queue is empty, return
   idle_thread. */
static struct thread *
next_thread_to_run (void) 
{
  if(thread_mlfqs)
  {
    if(mlfqs_queue_size == 0)
    {
      return idle_thread;
    }else{
      return thread_pop_max_priority_list();
    }
  }else{
    if (list_empty (&ready_list))
      return idle_thread;
    else
      return list_entry (list_pop_front (&ready_list), struct thread, elem);
  }
}

/* Completes a thread switch by activating the new thread's page
   tables, and, if the previous thread is dying, destroying it.

   At this function's invocation, we just switched from thread
   PREV, the new thread is already running, and interrupts are
   still disabled.  This function is normally invoked by
   thread_schedule() as its final action before returning, but
   the first time a thread is scheduled it is called by
   switch_entry() (see switch.S).

   It's not safe to call printf() until the thread switch is
   complete.  In practice that means that printf()s should be
   added at the end of the function.

   After this function and its caller returns, the thread switch
   is complete. */
void
thread_schedule_tail (struct thread *prev)
{
  struct thread *cur = running_thread ();
  
  ASSERT (intr_get_level () == INTR_OFF);

  /* Mark us as running. */
  cur->status = THREAD_RUNNING;

  /* Start new time slice. */
  thread_ticks = 0;

#ifdef USERPROG
  /* Activate the new address space. */
  process_activate ();
#endif

  /* If the thread we switched from is dying, destroy its struct
     thread.  This must happen late so that thread_exit() doesn't
     pull out the rug under itself.  (We don't free
     initial_thread because its memory was not obtained via
     palloc().) */
  if (prev != NULL && prev->status == THREAD_DYING && prev != initial_thread)
    {
      ASSERT (prev != cur);

      struct list_elem* elem; 
      struct thread* child;
      while(!list_empty(&prev->child_list)) {
        elem = list_pop_front(&prev->child_list);
        child = list_entry(elem, struct thread, child_elem);
        /* Only free child if it is not still running */
        if(child->status == THREAD_DYING) {
          thread_remove_donations(child, NULL);
          thread_free_file_descriptor_elems(child);
          palloc_free_page(child);
        } else {
          /* Child is orphaned */
          child->parent = NULL;
        }
      }
      
      /* Prev has been orphaned so kill it now */
      if(prev->parent == NULL) {
        /* Remember to free any allocated memory associated with the thread
           being freed. Includes donations and file descriptors. */
        thread_remove_donations(prev, NULL);
        thread_free_file_descriptor_elems(prev);
        palloc_free_page(prev);
      }
    }
}

/* Schedules a new process.  At entry, interrupts must be off and
   the running process's state must have been changed from
   running to some other state.  This function finds another
   thread to run and switches to it.

   It's not safe to call printf() until thread_schedule_tail()
   has completed. */
static void
schedule (void) 
{
  struct thread *cur = running_thread ();
  struct thread *next = next_thread_to_run ();
  struct thread *prev = NULL;

  ASSERT (intr_get_level () == INTR_OFF);
  ASSERT (cur->status != THREAD_RUNNING);
  ASSERT (is_thread (next));

  if (cur != next)
    prev = switch_threads (cur, next);
  thread_schedule_tail (prev);
}

/* Returns a tid to use for a new thread. */
static tid_t
allocate_tid (void) 
{
  static tid_t next_tid = 1;
  tid_t tid;

  lock_acquire (&tid_lock);
  tid = next_tid++;
  lock_release (&tid_lock);

  return tid;
}

/* Offset of `stack' member within `struct thread'.
   Used by switch.S, which can't figure it out on its own. */
uint32_t thread_stack_ofs = offsetof (struct thread, stack);
