#include "filesys/cache.h"
#include <debug.h>
#include "threads/malloc.h"


static struct list cache_list;
static struct hash cache_hash;

static void cache_evict(void);
static void cache_init(void);
static int cache_size(void);
static struct cache_elem* cache_lookup(block_sector_t sector);
static struct cache_elem* cache_insert(block_sector_t sector);
static void cache_reinsert(struct cache_elem* elem);
static unsigned cache_hash_fn (const struct hash_elem *p_, 
                                  void *aux UNUSED);
static bool cache_less_fn (const struct hash_elem *a_, 
                          const struct hash_elem *b_,
                          void *aux UNUSED);


/* Hash function for cache hash */
static unsigned 
cache_hash_fn (const struct hash_elem *p_, void *aux UNUSED)
{
  const struct cache_elem* e = 
      hash_entry(p_, struct cache_elem, hash_elem);
  return hash_int(e->sector); 
}

/* Less function for cache hash */
static bool 
cache_less_fn (const struct hash_elem *a_, 
               const struct hash_elem *b_,
                  void *aux UNUSED)
{
  const struct cache_elem* a = 
      hash_entry(a_, struct cache_elem, hash_elem);
  const struct cache_elem* b = 
      hash_entry(b_, struct cache_elem, hash_elem);

  return a->sector < b->sector;
}

/* Basic setup for the cache */
static void
cache_init()
{
  list_init(&cache_list);
  hash_init(&cache_hash, cache_hash_fn, cache_less_fn, NULL); 
}

/* Return number of elements in the cache */
static int
cache_size()
{
  return hash_size(&cache_hash);
}

/* Lookup sector SECTOR in the cache
   and return NULL if it is not found */
static struct cache_elem*
cache_lookup(block_sector_t sector)
{
  struct cache_elem c;
  c.sector = sector;

  struct hash_elem* e = hash_find(&cache_hash, &c.hash_elem);
  return e != NULL ? hash_entry(e, struct cache_elem, hash_elem) :NULL; 
}


/* Insert sector SECTOR into the cache
   by putting it in the hash and at
   the front of the list. */
static struct cache_elem* 
cache_insert(block_sector_t sector)
{
  struct cache_elem* c = (struct cache_elem*)
                            malloc(sizeof(struct cache_elem));

  if(c == NULL) return;

  c->sector = sector;
  list_push_front(&cache_list, &c->list_elem);
  hash_insert(&cache_hash, &c->hash_elem);
  return c;
}

/* Since this elem was just accessed
   move it to the front of the list */
static void
cache_reinsert(struct cache_elem* elem)
{
  list_remove(&elem->list_elem);
  list_push_front(&cache_list, &elem->list_elem);
}


/* We are out of room in the cache
   so evict the elemet at the back of the list */
static void
cache_evict()
{
  struct list_elem* to_evict = list_pop_back(&cache_list);
  struct cache_elem* c = 
      list_entry(to_evict, struct cache_elem, list_elem); 
  hash_delete(&cache_hash, &c->hash_elem);
  free(c); 
}

/* Get the cache element for this sector */
struct cache_elem* 
cache_get(block_sector_t sector)
{
  struct cache_elem* c = cache_lookup(sector);
  // If it was already in the cache, move it to the front
  if(c)
   {
    cache_reinsert(c);
    return c;
   }

  if(cache_size() == MAX_CACHE_SIZE)
   {
      cache_evict();  
   } 
 
  //look it up
  c = cache_insert(sector);
  return c; 

  /*
    If it is in the cache
      move it to the front
        and return it
    otherwise
      if the cache is not full
        look it up
        put it in cache
        move to front
      else
        evict from cache
        insert in cache

  V2

    If in cache
      reinsert to front
      return elem
    else
      if cache is full
        evict elem
      insert elem
    return elem
  */
}
