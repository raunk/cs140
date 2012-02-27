#ifndef VM_SWAP_H
#define VM_SWAP_H
/* Definition of swap table */

/* Index of a swap slot. */
typedef uint32_t swap_slot_t;

void swap_init(void);
bool swap_write_to_slot(const void *page, int swap_arr[4]);
void swap_read_from_slot(int swap_arr[4], void *buffer);
void swap_free_slot(swap_slot_t idx);

#endif /* vm/swap.h */