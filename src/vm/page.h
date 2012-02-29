#ifndef VM_PAGE_H
#define VM_PAGE_H
#include "lib/kernel/hash.h"
#include "threads/thread.h"
#include <stdbool.h>
#include "threads/synch.h"

/* Definitions of supplemental page table */
enum page_status
  {
    PAGE_ON_DISK,
    PAGE_IN_SWAP,
    PAGE_IN_MEM
  };

struct supp_page_key
 {
   void *vaddr;
 };
 
struct supp_page_entry
 {
   enum page_status status;
   
   bool is_mmapped;
   
   /* Page on disk */
   struct file *f;
   int off;
   int bytes_to_read;
   bool writable;
   
   /* Sectors in swap */
   int swap[8];
   
   struct hash_elem hash_elem;
   
   struct supp_page_key key;
 };
 
void supp_page_init(void);
struct supp_page_entry* supp_page_lookup (struct thread* for_thread, void *vaddr);
struct supp_page_entry* supp_page_insert_for_on_disk(struct thread* for_thread, void *vaddr, struct file *f,
    int off, int bytes_to_read, bool writable, bool is_mmapped);
struct supp_page_entry* supp_page_insert_for_on_stack(struct thread* for_thread, void *vaddr);
bool supp_page_bring_into_memory(void* addr, bool write);
void supp_remove_entry(struct thread* for_thread, void *vaddr);
unsigned supp_page_hash (const struct hash_elem *p_, void *aux UNUSED);
bool supp_page_less (const struct hash_elem *a_, const struct hash_elem *b_,
  void *aux UNUSED);

//struct lock supp_page_lock;

#endif /* vm/page.h */
