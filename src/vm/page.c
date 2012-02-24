#include "lib/kernel/hash.h"
#include "vm/page.h"

unsigned supp_page_hash (const struct hash_elem *p_, void *aux UNUSED);
bool supp_page_less (const struct hash_elem *a_, const struct hash_elem *b_,
  void *aux UNUSED);

static struct hash supp_page_table;

unsigned
supp_page_hash (const struct hash_elem *p_, void *aux UNUSED)
{
  const struct supp_page_entry *entry = hash_entry (p_, struct supp_page_entry, hash_elem);
  return hash_bytes (&entry->key, sizeof(struct supp_page_key));
}

bool
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
supp_page_init()
{
  hash_init(&supp_page_table, supp_page_hash, supp_page_less, NULL);
}

