#include "lib/kernel/hash.h"
#include "threads/malloc.h"
#include "vm/page.h"
#include <stdbool.h>

static unsigned supp_page_hash (const struct hash_elem *p_, void *aux UNUSED);
static bool supp_page_less (const struct hash_elem *a_, const struct hash_elem *b_,
  void *aux UNUSED);
  

static struct hash supp_page_table;

static unsigned
supp_page_hash (const struct hash_elem *p_, void *aux UNUSED)
{
  const struct supp_page_entry *entry = hash_entry (p_, struct supp_page_entry, hash_elem);
  return hash_bytes (&entry->key, sizeof(struct supp_page_key));
}

static bool
supp_page_less (const struct hash_elem *a_, const struct hash_elem *b_,
           void *aux UNUSED)
{
  const struct supp_page_entry *a = hash_entry (a_, struct supp_page_entry, hash_elem);
  const struct supp_page_entry *b = hash_entry (b_, struct supp_page_entry, hash_elem);
  
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
  struct supp_page_entry *entry = (struct supp_page_entry*) malloc(sizeof(struct supp_page_entry));
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
  entry_to_set->f = f;
  entry_to_set->off = off;
  entry_to_set->bytes_to_read = bytes_to_read;
  entry_to_set->writable = writable;
}

