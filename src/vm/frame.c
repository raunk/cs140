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

static void frame_write_to_swap(struct frame *frm, struct supp_page_entry *supp_pg);
static struct frame* frame_find_eviction_candidate(void);
static void free_frame_and_check_clock(struct list_elem* e, struct frame* frm);

void
frame_init(size_t user_page_limit)
{
  clock_ptr = NULL;
  list_init (&frame_list);
  lock_init (&frame_lock);
}

struct frame*
frame_get_page(enum palloc_flags flags, void *uaddr)
{
  /* Ensure we are always getting from the user pool */
  uaddr = pg_round_down(uaddr);
  flags = PAL_USER | PAL_ZERO;
  
  /* Attempt to allocate a page, if this comes back null then
     we need to evict */
  void *page = palloc_get_page(flags);
  struct frame* frm;
  if(page == NULL) {
    lock_acquire (&frame_lock);
    
    frm = frame_find_eviction_candidate();
    frm->is_evictable = false;
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
    lock_acquire (&frame_lock);
    frm = (struct frame*) malloc(sizeof(struct frame));
    if(frm == NULL) {
      PANIC ("frame_get: WE RAN OUT OF MALLOC SPACE. SHIT!\n");
    }
    frm->is_evictable = false;
    frm->physical_address = page;
    frm->user_address = uaddr;
    frm->owner = thread_current ();
    
    list_push_front(&frame_list, &frm->elem);
    lock_release (&frame_lock);
    
    /* Setup the pointer to be used in the clock algorithm */
    if(clock_ptr == NULL) {
      clock_ptr = list_begin (&frame_list);
    }
  }
  //printf("Returning physical page %p for user page %p\n", page, uaddr);
  //frm->is_evictable = true;
  return frm;
}


void
frame_free_user_page(void *vaddr)
{
  /* Search frame_list for struct frame mapped to page */
  struct list_elem *e;

  struct thread* cur = thread_current();

  for (e = list_begin (&frame_list); e != list_end (&frame_list);
       e = list_next (e))
    {
      struct frame *frm = list_entry (e, struct frame, elem);

      if (frm->user_address == vaddr &&
          frm->owner == cur) {
        /* Remove the struct frame from the frame list and
             free both the page and the struct frame */
        free_frame_and_check_clock(e, frm);
        return;
      }
    }
  
  PANIC ("frame_free: TRIED TO FREE PAGE NOT MAPPED IN FRAME LIST\n");

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
        free_frame_and_check_clock(e, frm);
        lock_release (&frame_lock);
        return;
      }
    }
  lock_release (&frame_lock);
  PANIC ("frame_free: TRIED TO FREE PAGE NOT MAPPED IN FRAME LIST\n");
}

static void
frame_write_to_swap(struct frame *frm, struct supp_page_entry *supp_pg)
{
  supp_pg->status = PAGE_IN_SWAP;
  bool written = swap_write_to_slot(frm->physical_address, supp_pg->swap);
  if(!written) {
    // TODO: kill process, free resources
    PANIC("OUT OF SWAP SPACE.\n");
  }
}



static void
free_frame_and_check_clock(struct list_elem* e, struct frame* frm)
{
  if(e == clock_ptr)
    {
      clock_ptr = list_next(clock_ptr);
      if(clock_ptr == list_end(&frame_list))
        clock_ptr = list_begin(&frame_list);
    }
  if (e == clock_ptr) {
    /* Handle case where e is only elem in the list. */
    clock_ptr = NULL;
  }

  list_remove(e);
  palloc_free_page(frm->physical_address);
  free(frm);
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
        if(!frm->is_evictable) continue;
        frm->is_evictable = false;
        
//        printf("Clock ptr %p\n", clock_ptr);
        clock_ptr = list_next(clock_ptr);
        if(clock_ptr == list_end (&frame_list)) {
          clock_ptr = list_begin (&frame_list);
        }
        

        if(!is_thread(frm->owner))
          {
            printf("NOT A THREAD %p\n", frm->owner);
            continue;
          }
        
        /* Has this page been referenced? */
        if(pagedir_is_accessed (frm->owner->pagedir, frm->user_address)) {
          /* Clear the reference bit, try next frame. */
          pagedir_set_accessed(frm->owner->pagedir, frm->user_address, false);
        } else {
          /* Reference bit is cleared. */
          struct supp_page_entry *supp_pg = supp_page_lookup (frm->owner->tid, frm->user_address);
          if(supp_pg == NULL) {
            PANIC("frame_find_eviction_candidate: COULDN'T FIND PAGE %p IN SUPP PAGE TABLE!\n",
                frm->user_address);
          }
          
          if(supp_pg->f != NULL) {
            /* It's a file page */
            if(pagedir_is_dirty (frm->owner->pagedir, frm->user_address)) {
              if (supp_pg->is_mmapped) {
                /* Mmapped file page has been modified, so write back to disk. */
                ASSERT(supp_pg->writable);
                safe_file_write_at(supp_pg->f, frm->physical_address, PGSIZE, supp_pg->off);
                pagedir_set_dirty (frm->owner->pagedir, frm->user_address, false);
                
                /* Give page second chance. */
                continue;
              } else {
                /* File page is not mmapped, so write to swap.*/
                frame_write_to_swap(frm, supp_pg);
              }
            } else {
              /* It's a file page that isn't dirty, we can just throw it out. */
              //supp_pg->status = PAGE_ON_DISK;              
              frame_write_to_swap(frm, supp_pg);              
            }
          } else {
            /* It's a stack page, we must write it to swap */
            frame_write_to_swap(frm, supp_pg);
          }
          
          /* Choose to evict this frame. */
          pagedir_clear_page (frm->owner->pagedir, frm->user_address);
          
          return frm;
        }
        frm->is_evictable = true;
      }
      
}

void
frame_cleanup_for_thread(struct thread* t)
{
//  printf("Cleaning up \n");
  lock_acquire (&frame_lock);

  if(list_empty(&frame_list)) return;

  struct list_elem *e = list_front (&frame_list);
  struct list_elem *next;
  while(e != list_end (&frame_list)) {
    next = list_next (e);
    
    struct frame *frm = list_entry (e, struct frame, elem);

    if (frm->owner == t) {
      struct supp_page_entry *supp_e = supp_page_lookup (t->tid, frm->user_address);
      supp_remove_entry(supp_e);
      
      pagedir_clear_page (frm->owner->pagedir, frm->user_address);

      free_frame_and_check_clock(e, frm);
    }
    e = next;
  }
  lock_release (&frame_lock);
}
