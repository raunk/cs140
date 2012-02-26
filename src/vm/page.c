#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include "lib/kernel/hash.h"
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "userprog/process.h"
#include "userprog/syscall.h"
#include "userprog/pagedir.h"
#include "threads/vaddr.h"
#include "vm/page.h"
#include "vm/frame.h"

static unsigned supp_page_hash (const struct hash_elem *p_, void *aux UNUSED);
static bool supp_page_less (const struct hash_elem *a_, const struct hash_elem *b_,
  void *aux UNUSED);
  

static struct hash supp_page_table;

void
supp_remove_entry(struct supp_page_entry* spe)
{
  hash_delete(&supp_page_table, &spe->hash_elem);
}


static unsigned
supp_page_hash (const struct hash_elem *p_, void *aux UNUSED)
{
  const struct supp_page_entry *entry = hash_entry (p_, struct supp_page_entry, 
                                                      hash_elem);
  return hash_bytes (&entry->key, sizeof(struct supp_page_key));
}

static bool
supp_page_less (const struct hash_elem *a_, const struct hash_elem *b_,
           void *aux UNUSED)
{
  const struct supp_page_entry *a = hash_entry (a_, struct supp_page_entry, 
                                                  hash_elem);
  const struct supp_page_entry *b = hash_entry (b_, struct supp_page_entry, 
                                                  hash_elem);
  
  const struct supp_page_key *a_key = &a->key;
  const struct supp_page_key *b_key = &b->key;
  if (a_key->tid != b_key->tid) {
    return a_key->tid < b_key->tid;
  }
  return a_key->vaddr < b_key->vaddr;
}

void
supp_page_init(void)
{
  hash_init(&supp_page_table, supp_page_hash, supp_page_less, NULL);
}

struct supp_page_entry *
supp_page_lookup (tid_t tid, void *vaddr)
{
  struct supp_page_entry entry;
  struct hash_elem *e;

  entry.key.tid = tid;
  entry.key.vaddr = vaddr;
  e = hash_find (&supp_page_table, &entry.hash_elem);
  return e != NULL ? hash_entry (e, struct supp_page_entry, hash_elem) : NULL;
}

void
supp_page_insert_for_on_disk(tid_t tid, void *vaddr, struct file *f,
    int off, int bytes_to_read, bool writable)
{
  struct supp_page_entry *entry = (struct supp_page_entry*) 
                            malloc(sizeof(struct supp_page_entry));
  if (entry == NULL) {
    //TODO
  }
  entry->key.tid = tid;
  entry->key.vaddr = vaddr;
  struct hash_elem *e = hash_insert(&supp_page_table, &entry->hash_elem);
  
  struct supp_page_entry *entry_to_set = entry;
  if (e != NULL) {
    entry_to_set = hash_entry (e, struct supp_page_entry, hash_elem);
    free(entry);
  }
  
  entry_to_set->status = PAGE_ON_DISK;
  entry_to_set->i = file_get_inode(f); 

  printf("Set inode %p\n", entry_to_set->i);

  entry_to_set->off = off;
  entry_to_set->bytes_to_read = bytes_to_read;
  entry_to_set->writable = writable;
}

bool 
supp_page_bring_into_memory(void* addr, bool write)
{
  void *upage = pg_round_down(addr);
  struct supp_page_entry *entry = supp_page_lookup(thread_current()->tid, upage);
  if (entry) {
     // If we page faulted on writing to a non-writeable location
     // exit the process
     if(write && !entry->writable)
      {
        exit_current_process(-1);
      } 

    /* Get a page of memory. */
     uint8_t *kpage = frame_get_page (PAL_USER, upage);
    
  //   printf("Bring page %p into mem\n", upage);

     if (kpage == NULL) {
       //exit_current_process(-1); // TODO: check if we should be exiting process here
     }

     int bytes_to_read = entry->bytes_to_read;
      
      printf("Bytes to read %d\n", bytes_to_read);

     /* Load this page. Don't read from disk if bytes_to_read is zero. */
     if (bytes_to_read > 0)
      { 

        printf("inode val=%p\n", entry->i);
        struct file* f = file_open(entry->i);
        
        int bytes = safe_file_read_at (f, kpage, bytes_to_read, entry->off);
        printf("Bytes read %d\n", bytes);
        if( bytes != bytes_to_read)
          { 
            printf("Free page in SUP BRING IN\n");
            frame_free_page (kpage);
          }
       }
     memset (kpage + bytes_to_read, 0, PGSIZE - bytes_to_read);
    
 
     /* Add the page to the process's address space. */
     if (!install_page (upage, kpage, entry->writable)) 
       {
         frame_free_page (kpage);
       }
     entry->status = PAGE_IN_MEM;
     return true;
   }
   return false;
}

