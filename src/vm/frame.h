#ifndef VM_FRAME_H
#define VM_FRAME_H

#include <stddef.h>
#include <list.h>
#include "threads/palloc.h"

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
void* frame_get_page(enum palloc_flags flags, void *uaddr);
void frame_free_page(void *page);
void frame_free_user_page(void *vaddr);
void frame_cleanup_for_thread(struct thread* t);

#endif /* vm/frame.h */

