#include "vm/frame.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include <debug.h>
#include <stdio.h>

static struct list frame_list;

void
frame_init(size_t user_page_limit)
{
  list_init(&frame_list);
}

void*
frame_get_page(enum palloc_flags flags, void *uaddr)
{
  printf("Ask frame for %p\n", uaddr);
  uaddr = pg_round_down(uaddr);

  printf("Get frame for %p\n", uaddr);

  /* Ensure we are always getting from the user pool */
  flags = PAL_USER | flags;
  
  /* Attempt to allocate a page, if this comes back null then
     we need to evict */
  void *page = palloc_get_page(flags);
  /* TODO: not sure if this is right way to check that we ran out of pages
      Maybe we should check if pages within some count?? */
  if(page != NULL) {
    struct frame *frm = (struct frame*) malloc(sizeof(struct frame));
    if(frm == NULL) {
      PANIC ("frame_get: WE RAN OUT OF SPACE. SHIT!\n");
    }
    
    frm->physical_address = page;
    frm->user_address = uaddr;
    frm->owner = thread_current ();
   
    printf("Got phys=%p uaddr=%p\n", frm->physical_address,
            frm->user_address);
 
    list_push_front(&frame_list, &frm->elem);
  } else {
    PANIC ("frame_get: WE RAN OUT OF SPACE. SHIT!\n");
  }
  
  return page;
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
        list_remove(e);
        printf("Freeing %p\n", frm->physical_address);
        palloc_free_page(frm->physical_address);
        free(frm);
        return;
      }
    }
  
  PANIC ("frame_free: TRIED TO FREE PAGE NOT MAPPED IN FRAME LIST\n");
}

void
frame_free_page(void *page)
{
  /* Search frame_list for struct frame mapped to page */
  struct list_elem *e;
  for (e = list_begin (&frame_list); e != list_end (&frame_list);
       e = list_next (e))
    {
      struct frame *frm = list_entry (e, struct frame, elem);

      printf("Freeing Frame phys=%p, uadd=%p\n", frm->physical_address,
            frm->user_address);

      printf("Check against page %p\n", page);

      if (frm->physical_address == page) {
        /* Remove the struct frame from the frame list and
           free both the page and the struct frame */
        list_remove(e);
        free(frm);
        palloc_free_page(page);
        return;
      }
    }
  
  PANIC ("frame_free: TRIED TO FREE PAGE NOT MAPPED IN FRAME LIST\n");
}
