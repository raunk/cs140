#include <debug.h>
#include <kernel/bitmap.h>
#include <list.h>
#include "devices/block.h"
#include "threads/synch.h"
#include "userprog/pagedir.h"
#include "vm/page.h"
#include "vm/swap.h"

static int get_free_slot_index(void);
static void swap_free_slot_unsafe(struct supp_page_entry *page);

struct block *swap_block;
static struct lock swap_lock;

/* Bitmap with SWAP_SIZE bits used to keep track of which swap slots
   are in-use. */
struct bitmap *map;

/* List of supp page table entries that are currently in swap. */
static struct list supp_page_entries;

void
swap_init(void)
{
  swap_block = block_get_role(BLOCK_SWAP);
  map = bitmap_create(block_size(swap_block));
  
  lock_init (&swap_lock);
  list_init (&supp_page_entries);

  if (map == NULL) {
    PANIC("Could not allocate memory for swap table data structure. ");
  }
}

/* If a free swap slot is found, flags the slot as in-use and
   returns its slot index. Else, returns -1. */
static int
get_free_slot_index(void)
{
  int swap_size = bitmap_size(map);
  int idx;
  for (idx = 0; idx < swap_size; idx++) {
    if (!bitmap_test(map, idx)) {
      bitmap_mark(map, idx);
      return idx;
    }
  }
  
  return -1;
}

/* If a free swap slot is found, copies page data to the slot and 
   returns slot index. Else, returns -1. */
bool
swap_write_to_slot(const void *data, struct supp_page_entry *page)
{
  lock_acquire(&swap_lock);
  int i;
  for(i = 0; i < 8; i++) {
    int idx = get_free_slot_index();
    if (idx >= 0) {
      block_write(swap_block, idx, data + i*BLOCK_SECTOR_SIZE);
      page->swap[i] = idx; 
    } else {
      return false;
    }
  }
  
  list_push_front (&supp_page_entries, &page->list_elem);
  
  lock_release(&swap_lock);
  return true;
}

/* Copies the page data saved in the swap table at INDEX into BUFFER. */
void
swap_read_from_slot(int swap_arr[8], void *buffer)
{
  lock_acquire(&swap_lock);
  int i;
  for(i = 0; i < 8; i++) {
    ASSERT(bitmap_test(map, swap_arr[i]));
    block_read(swap_block, swap_arr[i], buffer + i*BLOCK_SECTOR_SIZE);
  }
  lock_release(&swap_lock);
}

/* Unsafe version of swap_free_slot(). */
static void
swap_free_slot_unsafe(struct supp_page_entry *page)
{
  int i;
  for (i = 0; i < 8; i++) {
    ASSERT(bitmap_test(map, page->swap[i]));
    bitmap_reset(map, page->swap[i]);
  }
  
  list_remove(&page->list_elem);
}

/* Flags the slot at index INDEX as free. */
void
swap_free_slot(struct supp_page_entry *page)
{
  lock_acquire(&swap_lock);
  swap_free_slot_unsafe(page);
  lock_release(&swap_lock);
}

/* Frees all swap slots occupied by pages belonging to the current process. */
void
swap_free_slots_for_thread(struct thread *t)
{
  lock_acquire(&swap_lock);
  
  if (list_empty(&supp_page_entries)) {
    lock_release(&swap_lock);
    return;
  }
  
  struct list_elem *e = list_front (&supp_page_entries);
  struct list_elem *next;
  while(e != list_end (&supp_page_entries)) {
    next = list_next (e);
    
    struct supp_page_entry *page = list_entry (e, struct supp_page_entry, list_elem);

    if (page->key.tid == t->tid) {
      swap_free_slot_unsafe(page);
      
      pagedir_clear_page (t->pagedir, page->key.vaddr);
      
      supp_remove_entry(t->tid, page->key.vaddr);
    }
    e = next;
  }
  
  lock_release(&swap_lock);
}
