#include "lib/kernel/hash.h"
#include "threads/thread.h"

/* Definitions of supplemental page table */
enum page_status
  {
    PAGE_ON_DISK,
    PAGE_IN_SWAP,
    PAGE_IN_MEM
  };

struct supp_page_key
 {
   tid_t tid;
   void *vaddr;
 };
 
struct supp_page_entry
 {
   enum page_status status;
   
   /* Page on disk */
   struct file *f;
   int off;
   int bytes_to_read;
   
   struct hash_elem hash_elem;
   
   struct supp_page_key key;
 };

