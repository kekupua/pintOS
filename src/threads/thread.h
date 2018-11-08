#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H

#include <debug.h>
#include <list.h>
#include <stdint.h>
#include "synch.h"
<<<<<<< HEAD
#include "fixed_point.h"
=======
>>>>>>> 61aa98fd2c446d4367d2d952d809964b7c1eeccc

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

/* Thread prioriy donation. */
<<<<<<< HEAD
#define PRIDON_MAX_DEPTH 9              /* Max depth of nested donation. */
=======
#define PRI_MAX_NEST 9              /* Max nested donation. */

>>>>>>> 61aa98fd2c446d4367d2d952d809964b7c1eeccc

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
/* The `elem' member has a multiple purpose.  It can be an element in
   the run queue (thread.c), or it can be an element in a
   semaphore wait list (synch.c) or an element in timer sleeping list
   (timer.c). It can be used these ways only because they are
   mutually exclusive: only a thread in the ready state is on the
   run queue, whereas only a thread in the blocked state is on a
   semaphore wait list or a timer sleeping list. */
struct thread
  {
    /* Owned by thread.c. */
    tid_t tid;                          /* Thread identifier. */
    enum thread_status status;          /* Thread state. */
    char name[16];                      /* Name (for debugging purposes). */
    uint8_t *stack;                     /* Saved stack pointer. */
    int priority;                       /* Priority. */
    int base_priority;                  /* For priority donation */
    struct list_elem allelem;           /* List element for all threads list. */

    /* Shared between thread.c, synch.c and timer.c. */
    struct list_elem elem;              /* List element. */
    struct list locks;                  /* Locks held for priority donation. */
    struct lock *lock_waiting;          /* Lock waiting on for priority donation. */
    int64_t wakeup_ticks;               /* Wakeup ticks used by timer sleep */

    struct list locks;                  /* Locks held for priority donation. */
    struct lock *lock_waiting;          /* Lock waiting on for priority donation. */

    int nice;                           /* Niceness for 4.4BSD scheduler. */
    fixed_t recent_cpu;                 /* Recent CPU for 4.4BSD scheduler. */

    int64_t wakeup_ticks;               /* Wakeup ticks used by timer sleep */


#ifdef USERPROG
    /* Owned by userprog/process.c. */
    uint32_t *pagedir;                  /* Page directory. */
#endif

    /* Owned by thread.c. */
    unsigned magic;                     /* Detects stack overflow. */
  };

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
extern bool thread_mlfqs;

void thread_init (void);
void thread_start (void);

// void thread_tick (void);
void thread_print_stats (void);

typedef void thread_func (void *aux);
tid_t thread_create (const char *name, int priority, thread_func *, void *);

void thread_block (void);
void thread_unblock (struct thread *);

struct thread *thread_current (void);
tid_t thread_tid (void);
const char *thread_name (void);

void thread_exit (void) NO_RETURN;
void thread_yield (void);

/* Performs some operation on thread t, given auxiliary data AUX. */
typedef void thread_action_func (struct thread *t, void *aux);
void thread_foreach (thread_action_func *, void *);

void thread_add_lock (struct lock *);
void thread_remove_lock (struct lock *);

int thread_get_priority (void);
void thread_set_priority (int);
void thread_donate_priority (struct thread *);
void thread_update_priority (struct thread *);

void thread_test_preemption (void);

int thread_get_nice (void);
void thread_set_nice (int);
int thread_get_recent_cpu (void);
int thread_get_load_avg (void);

void thread_test_preemption (void);
void thread_add_lock (struct lock *);
void thread_remove_lock (struct lock *);

void thread_donate_priority (struct thread *);
void thread_update_priority (struct thread *);

bool is_wakeup_ticks_less(const struct list_elem *t1, const struct list_elem *t2, void *aux UNUSED);
bool is_priority_greater(const struct list_elem *t1, const struct list_elem *t2, void *aux);

struct thread *thread_get(tid_t tid);

void thread_mlfqs_incr_recent_cpu(void);
void thread_mlfqs_calc_recent_cpu(struct thread *);
void thread_mlfqs_update_priority(struct thread *);
void thread_mlfqs_refresh(void);

bool thread_wakeup_ticks_less(const struct list_elem *,
                              const struct list_elem *,
                              void *);
bool thread_priority_large(const struct list_elem *,
                           const struct list_elem *,
                           void *);


#endif /* threads/thread.h */
