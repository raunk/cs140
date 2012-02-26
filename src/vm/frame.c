#include "vm/frame.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include <debug.h>
#include "userprog/pagedir.h"
#include "threads/vaddr.h"
#include "vm/page.h"
#include "vm/swap.h"
#include <stdio.h>

static struct list frame_list;
struct list_elem *clock_ptr;

static void frame_evict_page(void);

void
frame_init(size_t user_page_limit)
{
  clock_ptr = NULL;
  list_init(&frame_list);
}

void*
frame_get_page(enum palloc_flags flags, void *uaddr)
{
  /* Ensure we are always getting from the user pool */
  uaddr = pg_round_down(uaddr);
  printf("UADDR: %p\n", uaddr);
  flags = PAL_USER | flags;
  
  /* Attempt to allocate a page, if this comes back null then
     we need to evict */
  void *page = palloc_get_page(flags);
  if(page == NULL) {
    frame_evict_page();
    
    page = palloc_get_page(flags);
    if(page == NULL) {
      PANIC ("frame_get: WE RAN OUT OF PAGE SPACE. SHIT!\n");
    }
    
    struct frame *frm = (struct frame*) malloc(sizeof(struct frame));
    if(frm == NULL) {
      PANIC ("frame_get: WE RAN OUT OF MALLOC SPACE. SHIT!\n");
    }
    
    frm->physical_address = page;
    frm->user_address = uaddr;
    frm->owner = thread_current ();
    
    /* Add just before clock pointer */
    list_insert (clock_ptr, &frm->elem);
    
  } else {
    struct frame *frm = (struct frame*) malloc(sizeof(struct frame));
    if(frm == NULL) {
      PANIC ("frame_get: WE RAN OUT OF MALLOC SPACE. SHIT!\n");
    }
    
    frm->physical_address = page;
    frm->user_address = uaddr;
    frm->owner = thread_current ();
    
    list_push_front(&frame_list, &frm->elem);
    
    /* Setup the pointer to be used in the clock algorithm */
    if(clock_ptr == NULL) {
      clock_ptr = list_begin (&frame_list);
    }
  }
  printf("Returning physical page %p\n", page);
  return page;
}

void
frame_free_page(void *page)
{
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
        pagedir_clear_page (frm->owner->pagedir, frm->user_address); 
        palloc_free_page(page);
        free(frm);
        
        return;
      }
    }
  
  PANIC ("frame_free: TRIED TO FREE PAGE NOT MAPPED IN FRAME LIST\n");
}

static void
frame_evict_page(void)
{
  printf("--------------- Pages currently --------------------------\n");
  struct list_elem *e;
  for (e = list_begin (&frame_list); e != list_end (&frame_list);
       e = list_next (e))
    {
      struct frame *frm = list_entry (e, struct frame, elem);
      printf("%p -> ", frm->user_address);
    }
  printf("\n");
  printf("--------------- End Pages currently --------------------------\n");
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
          printf("Clearing reference bit at %p\n", frm->user_address);
          pagedir_set_accessed(frm->owner->pagedir, frm->user_address, false);
        } else {
          /* Is this page dirty? */
          // if(pagedir_is_dirty (frm->owner->pagedir, frm->user_address)) {
          //             // TODO begin writing to disk
          //             // TODO clear the dirty bit
          //             printf("Page is dirty at %p\n", frm->user_address);
          //             /* Write to swap and evict, for now */
          //             
          //           } else {
          //             
          //             struct supp_page_entry *supp_pg = supp_page_lookup (frm->owner->tid, frm->user_address);
          //             
          //             // supp_pg->swap_idx = swap_idx;
          //             supp_pg->status = PAGE_ON_DISK;
          //             
          //             printf("Evicting page %p at physical memory location %p\n", frm->user_address, frm->physical_address);
          //             printf("Page came from file ptr %p at offset %d\n", supp_pg->f, supp_pg->off);
          //             
          //             struct frame *frm1 = list_entry (clock_ptr, struct frame, elem);
          //             printf("Leaving clock pointer at %p\n", frm1->user_address);
          //             
          //             frame_free_page(frm->physical_address);
          //             return;
          //           }
          
          /* Write to swap and evict for now, no matter what */
          struct supp_page_entry *supp_pg = supp_page_lookup (frm->owner->tid, frm->user_address);
          printf("GOT HERE...%p\n", frm->physical_address);
          int swap_idx = swap_write_to_slot(frm->physical_address);
          printf("AFTER WRITING TO SLOT...\n");
          supp_pg->swap_idx = swap_idx;
          supp_pg->status = PAGE_IN_SWAP;
          
          printf("Evicting page %p at physical memory location %p\n", frm->user_address, frm->physical_address);
          printf("Page came from file ptr %p at offset %d\n", supp_pg->f, supp_pg->off);
                      
          struct frame *frm1 = list_entry (clock_ptr, struct frame, elem);
          printf("Leaving clock pointer at %p\n", frm1->user_address);
          
          frame_free_page(frm->physical_address);
        }

      }
}
