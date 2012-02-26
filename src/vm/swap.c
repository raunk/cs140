#include <debug.h>
#include "devices/block.h"
#include "threads/malloc.h"
#include "vm/swap.h"

static int is_slot_in_use(swap_slot_t index);
static void flag_slot(swap_slot_t index, int is_in_use);
static int get_free_slot_index(void);

static struct block *swap_block;
static int swap_size;

#define DATA_SIZE 32
static uint32_t *slot_data;
static int num_data;

void
swap_init(void)
{
  swap_block = block_get_role(BLOCK_SWAP);
  swap_size = block_size(swap_block);
  
  num_data = 1 + swap_size / DATA_SIZE;
  slot_data = (uint32_t *) malloc(num_data);
  if (slot_data == NULL) {
    PANIC("Could not allocate memory for swap table data structure. ");
  }
  int i;
  for (i = 0; i < num_data; i++) {
    slot_data[i] = 0;
  }
}

static int
is_slot_in_use(swap_slot_t index)
{
  return (slot_data[index / DATA_SIZE] & (1 << (index % DATA_SIZE)));
}

static void
flag_slot(swap_slot_t index, int is_in_use)
{
  if (is_in_use) {
    slot_data[index / DATA_SIZE] |= (1 << (index % DATA_SIZE));
  } else {
    slot_data[index / DATA_SIZE] &= ~(1 << (index % DATA_SIZE));
  }
}

/* If a free swap slot is found, flags the slot as in-use and
   returns its slot index. Else, returns -1. */
static int
get_free_slot_index(void)
{
  int index;
  for (index = 0; index < swap_size; index++) {
    if (!is_slot_in_use(index)) {
      flag_slot(index, 1);
      return index;
    }
  }
  
  return -1;
}

/* If a free swap slot is found, copies page data to the slot and 
   returns slot index. Else, returns -1. */
int
swap_write_to_slot(const void *page)
{
  int index = get_free_slot_index();
  if (index >= 0) {
    block_write(swap_block, index, page); 
  }
  return index;
}

/* Copies the page data saved in the swap table at INDEX into BUFFER. */
void
swap_read_from_slot(swap_slot_t index, void *buffer)
{
  ASSERT(is_slot_in_use(index));
  block_read(swap_block, index, buffer);
}

/* Flags the slot at index INDEX as free. */
void
swap_free_slot(swap_slot_t index)
{
  ASSERT(is_slot_in_use(index));
  flag_slot(index, 0);
}
