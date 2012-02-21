#include "vm/frame.h"

static struct list frame_list;

void
frame_init(size_t user_page_limit)
{
  list_init(&frame_list);
}

void*
frame_get_page(enum palloc_flags flags, void *uaddr)
{
  /* Ensure we are always getting from the user pool */
  flags = PAL_USER | flags;
  
  /* Attempt to allocate a page, if this comes back null then
     we need to evict */
  void *page = palloc_get_page(flags);
  /* TODO: not sure if this is right way to check that we ran out of pages
      Maybe we should check if pages within some count?? */
  if(page != NULL) {
    struct frame *frm = (struct frame*) malloc(sizeof(struct frame));
    frm->physical_address = page;
    frm->user_address = uaddr;
    frm->owner = thread_current ();
    
    list_push_front(&frame_list, &frm->elem);
  } else {
    printf("WE RAN OUT OF SPACE. SHIT!\n");
  }
}