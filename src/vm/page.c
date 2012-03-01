#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include "lib/kernel/hash.h"
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/interrupt.h"
#include "threads/synch.h"
#include "userprog/process.h"
#include "userprog/syscall.h"
#include "userprog/pagedir.h"
#include "userprog/exception.h"
#include "threads/vaddr.h"
#include "vm/page.h"
#include "vm/frame.h"
#include "vm/swap.h"
#include <random.h>

static unsigned supp_page_hash (const struct hash_elem *p_, void *aux UNUSED);
static bool supp_page_less (const struct hash_elem *a_, const struct hash_elem *b_,
  void *aux UNUSED);
  
static struct hash supp_page_table;

/* Call thread_yield with probability 1/2 */
void
debug()
{
  unsigned long n = random_ulong() % 2;
  if(n == 0) {
    thread_yield();
  }
}

void
supp_remove_entry(tid_t tid, void* vaddr)
{
  lock_acquire(&supp_page_lock);
  struct supp_page_entry* spe = supp_page_lookup(tid, vaddr);
  hash_delete(&supp_page_table, &spe->hash_elem);
  free(spe);
  lock_release(&supp_page_lock);
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
  lock_init(&supp_page_lock);
}

struct supp_page_entry *
supp_page_lookup (tid_t tid, void *vaddr)
{
  vaddr = pg_round_down(vaddr);
  
  struct supp_page_entry entry;
  struct hash_elem *e;

  entry.key.tid = tid;
  entry.key.vaddr = vaddr;
  
  //lock_acquire(&supp_page_lock);
  e = hash_find (&supp_page_table, &entry.hash_elem);
  //lock_release(&supp_page_lock);
  
  return e != NULL ? hash_entry (e, struct supp_page_entry, hash_elem) : NULL;
}

struct supp_page_entry*
supp_page_insert_for_on_stack(tid_t tid, void *vaddr)
{
  lock_acquire(&supp_page_lock);
  struct supp_page_entry *entry = supp_page_lookup (thread_current()->tid, vaddr);
  if(entry != NULL) {
    lock_release(&supp_page_lock);
    return entry;
  }
  entry = (struct supp_page_entry*) 
                            malloc(sizeof(struct supp_page_entry));
  if (entry == NULL) {
    //TODO
  }
  entry->key.tid = tid;
  entry->key.vaddr = vaddr;
  
  //lock_acquire(&supp_page_lock);
  struct hash_elem *e = hash_insert(&supp_page_table, &entry->hash_elem);
  
  struct supp_page_entry *entry_to_set = entry;
  if (e != NULL) {
    entry_to_set = hash_entry (e, struct supp_page_entry, hash_elem);
    free(entry);
  }
  
  entry_to_set->f = NULL;
  entry_to_set->off = 0;
  entry_to_set->bytes_to_read = 0;
  entry_to_set->status = PAGE_IN_MEM;
  entry_to_set->writable = true;
  entry_to_set->is_mmapped = false;
  lock_release(&supp_page_lock);
}

struct supp_page_entry*
supp_page_insert_for_on_disk(tid_t tid, void *vaddr, struct file *f,
    int off, int bytes_to_read, bool writable, bool is_mmapped)
{
  lock_acquire(&supp_page_lock);
  struct supp_page_entry *entry = supp_page_lookup (thread_current()->tid, vaddr);
  if(entry != NULL) {
    lock_release(&supp_page_lock);
    return entry;
  }
  entry = (struct supp_page_entry*) 
                            malloc(sizeof(struct supp_page_entry));
  if (entry == NULL) {
    lock_release(&supp_page_lock);
    PANIC("supp_page_insert_for_on_disk: ran out of space");
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
  entry_to_set->f = f;
  entry_to_set->off = off;
  entry_to_set->bytes_to_read = bytes_to_read;
  entry_to_set->writable = writable;
  entry_to_set->is_mmapped = is_mmapped;
  lock_release(&supp_page_lock);
}

bool 
supp_page_bring_into_memory(void* addr, bool write)
{
  //sema_down(&page_fault_sema);
  void *upage = pg_round_down(addr);
  //printf("Attempting to lookup %p in supp page table..\n", upage);
  lock_acquire(&supp_page_lock);
  struct supp_page_entry *entry = supp_page_lookup(thread_current()->tid, upage);
  lock_release(&supp_page_lock);
  
  if (entry != NULL) {
    //printf("ENTRY IS FOUND WITH STATUS %d\n", entry->status);
    if(entry->status == PAGE_ON_DISK) {
      //printf("\n\n--------------- Reading page from disk ------------------------\n");
      // If we page faulted on writing to a non-writeable location
      // exit the process
      if(write && !entry->writable)
      {
        exit_current_process(-1);
      } 
      //debug();
    /* Get a page of memory. */
     struct frame* frm = frame_get_page (PAL_USER, upage);
     uint8_t *kpage = frm->physical_address;
     
     if (kpage == NULL) {
       exit_current_process(-1); 
     }

     int bytes_to_read = entry->bytes_to_read;

     /* Load this page. Don't read from disk if bytes_to_read is zero. */
     if (bytes_to_read > 0 &&
        safe_file_read_at (entry->f, kpage, bytes_to_read, entry->off) != bytes_to_read)
       {
         frame_free_page (kpage);
         PANIC("DIDNT READ EVERYTHING SUPPOSED TO!");
       }
      //memset (kpage + bytes_to_read, 0, PGSIZE - bytes_to_read);
      
      /* Add the page to the process's address space. */
      if (!install_page (upage, kpage, entry->writable)) 
       {
         frame_free_page (kpage);
         PANIC("DIDNT READ EVERYTHING SUPPOSED TO!");
       }
      entry->status = PAGE_IN_MEM;
      frm->is_evictable = true;
      // printf("Brought page %p from disk into physical memory at %p\n", upage, kpage);
      //       printf("--------------- End reading page from disk ------------------------\n\n");
      //sema_up(&page_fault_sema);
      return true; 
      
    } else if(entry->status == PAGE_IN_SWAP) {
      
      //printf("\n\n--------------- Reading out of swap ------------------------\n");
      /* Get a page of memory. */
      struct frame* frm = frame_get_page (PAL_USER, upage);
      uint8_t *kpage = frm->physical_address;
      if (kpage == NULL) {
       //exit_current_process(-1); // TODO: check if we should be exiting process here
      }
          
      swap_read_from_slot(entry->swap, kpage);
      swap_free_slot(entry->swap);
      
      bool is_dirty = pagedir_is_dirty (thread_current()->pagedir, upage);
      
      /* Add the page to the process's address space. */
      if (!install_page (upage, kpage, entry->writable)) 
       {
      //   printf("COULDNT INSTALL PAGE!\n");
         frame_free_page (kpage);
       }
       
       if(is_dirty)
         pagedir_set_dirty (thread_current()->pagedir, upage, true);
      
      entry->status = PAGE_IN_MEM;
      frm->is_evictable = true;
      
      // printf("Brought page %p from swap into physical memory at %p\n", upage, kpage);
      //       printf("Page is valid up to %p\n", (upage+PGSIZE));
      //       printf("--------------- End reading out of swap ------------------------\n\n");
      //sema_up(&page_fault_sema);
      return true;
    }
  }
  //sema_up(&page_fault_sema);
//  printf("EXITING ON ADDRESS: %p\n", addr);
  return false;
}

