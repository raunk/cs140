#include <debug.h>
#include <kernel/bitmap.h>
#include "devices/block.h"
#include "vm/swap.h"
#include "threads/synch.h"

static int get_free_slot_index(void);

struct block *swap_block;
static struct lock swap_lock;

/* Bitmap with SWAP_SIZE bits used to keep track of which swap slots
   are in-use. */
struct bitmap *map;

void
swap_init(void)
{
  swap_block = block_get_role(BLOCK_SWAP);
  map = bitmap_create(block_size(swap_block));
  
  lock_init (&swap_lock);

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
swap_write_to_slot(const void *page, int swap_arr[8])
{
  lock_acquire(&swap_lock);
  int i;
  for(i = 0; i < 8; i++) {
    int idx = get_free_slot_index();
    if (idx >= 0) {
      block_write(swap_block, idx, page + i*BLOCK_SECTOR_SIZE);
      swap_arr[i] = idx; 
    } else {
      return false;
    }
  }
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
    printf("CURRENTLY READING INDEX: %d\n", swap_arr[i]);
    printf("READING TO LOCATION: %p\n", (buffer + i*BLOCK_SECTOR_SIZE));
    block_read(swap_block, swap_arr[i], buffer + i*BLOCK_SECTOR_SIZE);
  }
  lock_release(&swap_lock);
}

/* Flags the slot at index INDEX as free. */
void
swap_free_slot(swap_slot_t idx)
{
  ASSERT(bitmap_test(map, idx));
  lock_acquire(&swap_lock);
  bitmap_reset(map, idx);
  lock_release(&swap_lock);
}
