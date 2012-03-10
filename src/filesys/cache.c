#include "filesys/cache.h"


static struct list cache_list;
static struct hash cache_hash;

static void cache_evict();
static void cache_init();
static int cache_size();
static struct cache_elem* cache_lookup(block_sector_t sector);
static void cache_insert(block_sector_t sector);
static void cache_reinsert(struct cache_elem* elem);

/* Basic setup for the cache */
static void
cache_init()
{

}

/* Return number of elements in the cache */
static int
cache_size()
{

}

/* Lookup sector SECTOR in the cache
   and return NULL if it is not found */
static struct cache_elem*
cache_lookup(block_sector_t sector)
{

}


/* Insert sector SECTOR into the cache
   by putting it in the hash and at
   the front of the list. */
static void
cache_insert(block_sector_t sector)
{

}

/* Since this elem was just accessed
   move it to the front of the list */
static void
cache_reinsert(struct cache_elem* elem)
{

}


/* We are out of room in the cache
   so evict the elemet at the back of the list */
static void
cache_evict()
{

}

/* Get the cache element for this sector */
struct cache_elem* 
cache_get(block_sector_t sector)
{
  return NULL;
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
