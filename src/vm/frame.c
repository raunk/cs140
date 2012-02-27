#include "vm/frame.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include <debug.h>
#include "userprog/pagedir.h"
#include "userprog/syscall.h"
#include "threads/vaddr.h"
#include "vm/page.h"
#include "vm/swap.h"
#include <stdio.h>
#include <string.h>

static struct list frame_list;
struct list_elem *clock_ptr;

static struct lock frame_lock;

static struct frame* frame_find_eviction_candidate(void);

void
frame_init(size_t user_page_limit)
{
  clock_ptr = NULL;
  list_init (&frame_list);
  lock_init (&frame_lock);
}

void*
frame_get_page(enum palloc_flags flags, void *uaddr)
{
  /* Ensure we are always getting from the user pool */
  uaddr = pg_round_down(uaddr);
  flags = PAL_USER | PAL_ZERO;
  
  /* Attempt to allocate a page, if this comes back null then
     we need to evict */
  void *page = palloc_get_page(flags);
  if(page == NULL) {
    lock_acquire (&frame_lock);
    
    struct frame* frm = frame_find_eviction_candidate();
    
    // printf("KPAGE: %d\n", *(int*)frm->physical_address);
    //     printf("Evicting page %p at physical memory location %p\n", frm->user_address, frm->physical_address);
    // printf("Page came from file ptr %p at offset %d\n", supp_pg->f, supp_pg->off);
    //     printf("Wrote page %p to swap at slot %d\n", frm->user_address, swap_idx);
    //
    lock_release (&frame_lock);
    
    memset (frm->physical_address, 0, PGSIZE);
    
    frm->owner = thread_current ();
    frm->user_address = uaddr;
    
    page = frm->physical_address;
    
  } else {
    struct frame *frm = (struct frame*) malloc(sizeof(struct frame));
    if(frm == NULL) {
      PANIC ("frame_get: WE RAN OUT OF MALLOC SPACE. SHIT!\n");
    }
    
    frm->physical_address = page;
    frm->user_address = uaddr;
    frm->owner = thread_current ();
    
    lock_acquire (&frame_lock);
    list_push_front(&frame_list, &frm->elem);
    lock_release (&frame_lock);
    
    /* Setup the pointer to be used in the clock algorithm */
    if(clock_ptr == NULL) {
      clock_ptr = list_begin (&frame_list);
    }
  }
  //printf("Returning physical page %p for user page %p\n", page, uaddr);
  return page;
}

void
frame_free_page(void *page)
{
  lock_acquire (&frame_lock);
  
  page = pg_round_down(page);
  /* Search frame_list for struct frame mapped to page */
  struct list_elem *e;
  for (e = list_begin (&frame_list); e != list_end (&frame_list);
       e = list_next (e))
    {
      struct frame *frm = list_entry (e, struct frame, elem);
      if (frm->physical_address == page) {
        /* Remove the struct frame from the frame list and
           free both the page and the struct frame */
        list_remove(e);
        palloc_free_page(page);
        free(frm);
        lock_release (&frame_lock);
        return;
      }
    }
  lock_release (&frame_lock);
  PANIC ("frame_free: TRIED TO FREE PAGE NOT MAPPED IN FRAME LIST\n");
}

static struct frame*
frame_find_eviction_candidate(void)
{
  // printf("--------------- Pages currently --------------------------\n");
  //   struct list_elem *e;
  //   for (e = list_begin (&frame_list); e != list_end (&frame_list);
  //        e = list_next (e))
  //     {
  //       struct frame *frm = list_entry (e, struct frame, elem);
  //       printf("%p -> ", frm->user_address);
  //     }
  //   printf("\n");
  //   printf("--------------- End Pages currently --------------------------\n");
  //   printf("--------------- Begin clock algorithm ------------------------\n");
  /* Cycle pages in order circularly */
  while (1)
      { 
        struct frame *frm = list_entry (clock_ptr, struct frame, elem);
        clock_ptr = list_next(clock_ptr);
        if(clock_ptr == list_end (&frame_list)) {
          clock_ptr = list_begin (&frame_list);
        }
        
        /* Has this page been referenced? */
        if(pagedir_is_accessed (frm->owner->pagedir, frm->user_address)) {
          /* Clear the reference bit */
          pagedir_set_accessed(frm->owner->pagedir, frm->user_address, false);
        } else {
          
          struct supp_page_entry *supp_pg = supp_page_lookup (frm->owner->tid, frm->user_address);
          if(supp_pg == NULL) {
            printf("COULDN'T FIND PAGE %p IN SUPP PAGE TABLE!\n", frm->user_address);
          }
          
          if(supp_pg->f != NULL) {
            /* It's a file page */
            if(pagedir_is_dirty (frm->owner->pagedir, frm->user_address)) {
              // TODO synchronization!!!
              if(supp_pg->writable) {
                //printf("Beginning write to file...\n");
                safe_file_write_at(supp_pg->f, frm->physical_address, supp_pg->bytes_to_read, 
                  supp_pg->off);
              }
              
              //pagedir_set_dirty (frm->owner->pagedir, frm->user_address, false);
              
            } else {
              /* It's a file page that isn't dirty, we can just throw it out */
              pagedir_clear_page (frm->owner->pagedir, frm->user_address); 
              supp_pg->status = PAGE_ON_DISK;
              return frm;
            }
          } else {
            /* It's a stack page, we must write it to swap */
            pagedir_clear_page (frm->owner->pagedir, frm->user_address); 

            bool written = swap_write_to_slot(frm->physical_address, supp_pg->swap);
            if(!written) {
              PANIC("OUT OF SWAP SPACE.\n");
            }
            supp_pg->status = PAGE_IN_SWAP;
            return frm;
          }
        }

      }
      
}
