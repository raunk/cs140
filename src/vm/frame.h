#ifndef VM_FRAME_H
#define VM_FRAME_H

#include <stddef.h>
#include <list.h>

/* Definitions of page frame */
struct frame {
    struct thread *owner;    /* The owner of this frame */
    void *physical_address;  /* Physical address where frame currently resides. 
                                This may change if frame needs to be swapped. */
    void *user_address;      /* User address where thread will access this 
                                memory */
    struct list_elem elem;
};

void frame_init(size_t user_page_limit);

#endif /* vm/frame.h */

