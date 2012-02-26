/* Definition of swap table */

/* Index of a swap slot. */
typedef uint32_t swap_slot_t;

void swap_init(void);
int swap_write_to_slot(const void *page);
void swap_read_from_slot(swap_slot_t index, void *buffer);
void swap_free_slot(swap_slot_t index);
