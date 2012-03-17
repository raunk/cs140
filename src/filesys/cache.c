#include "filesys/cache.h"
#include <debug.h>
#include "threads/malloc.h"
#include "filesys/filesys.h"
#include <string.h>
#include <stdio.h>
#include "threads/synch.h"


/* Represents one element in the LRU cache. */
struct cache_elem {
  block_sector_t sector;        /* Sector number for this cache element */ 
  bool is_dirty;                /* Whether this element has been written to */
  int num_operations;           /* Tracks how many other processes are currently
                                   reading to or writing from this elem */
  struct list_elem list_elem;   /* List element for cache*/
  struct hash_elem hash_elem;   /* Hash elemetn for cache*/
  char data[512];               /* Cache data */ 
};

/* Lock to synchronize cache_done with the read ahead thread */
static struct lock done_lock;

/* Flag to alert that cache_done has been called */
static int cache_stop = 0;


/* Doubly linked list to allow us to easily find an element to evict */
static struct list cache_list;

/* Hash to quickly lookup elements in the cache */
static struct hash cache_hash;

/* Lock to prevent destructive concurrent modifications to cache 
 * data structures */
static struct lock cache_lock;

/* Conditions to syncrhonize io in the cache */
static struct condition io_finished;
static struct condition operations_finished;
static struct list sectors_under_io;

/* The largest valid sector, given the disk size */
static block_sector_t sector_max;

/* A struct to maintain the list of which elements are currently
 * under I/O */
struct sector_under_io {
  block_sector_t sector;
  struct list_elem elem;
};

static void cache_evict(void);
static int cache_size(void);
static struct cache_elem* cache_lookup(block_sector_t sector);
static struct cache_elem* cache_insert(block_sector_t sector);
static void cache_reinsert(struct cache_elem* elem);
static struct cache_elem* cache_get(block_sector_t sector);
static unsigned cache_hash_fn (const struct hash_elem *p_, 
                                  void *aux UNUSED);
static bool cache_less_fn (const struct hash_elem *a_, 
                          const struct hash_elem *b_,
                          void *aux UNUSED);
                          
static struct cache_elem* get_oldest_cache_elem(void);

static void mark_finished_operation(struct cache_elem *c);
static void mark_sector_under_io(block_sector_t sector);
static void unmark_sector_under_io(block_sector_t sector);
static bool is_sector_under_io(block_sector_t sector);

/* Maintain statistics about the cache */
static int cache_hits;
static int cache_misses;

/* Always keep a cache element around for the free map meta-data */
struct cache_elem free_map_cache;


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
void
cache_init()
{
  list_init(&cache_list);
  hash_init(&cache_hash, cache_hash_fn, cache_less_fn, NULL); 
  cache_hits = 0;
  cache_misses = 0;
  
  list_init(&sectors_under_io);
  lock_init(&cache_lock);
  lock_init(&done_lock);
  cond_init(&io_finished);
  cond_init(&operations_finished);
  
  sector_max = block_size(fs_device) - 1;

  // Free map always available
  free_map_cache.sector = FREE_MAP_SECTOR;
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
  if (c == NULL) {
    PANIC("cache_insert(): out of heap memory");
  }

  c->sector = sector;
  c->is_dirty = false;
  c->num_operations = 0;
  
  list_push_front(&cache_list, &c->list_elem);
  hash_insert(&cache_hash, &c->hash_elem);
  mark_sector_under_io(sector);
  lock_release(&cache_lock);
  
  /* I/O action, so don't block operations on other sectors*/
  block_read(fs_device, sector, &c->data);
  
  lock_acquire(&cache_lock);
  unmark_sector_under_io(sector);
  cond_broadcast(&io_finished, &cache_lock);
  
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

/* Mark sector SECTOR as currently being under I/O by putting it in the
 * sectors_under_io list */
static void
mark_sector_under_io(block_sector_t sector)
{
  struct sector_under_io *s =
      (struct sector_under_io*) malloc(sizeof(struct sector_under_io));
  s->sector = sector;
  list_push_front(&sectors_under_io, &s->elem);
}

/* Unmark sector as being under I/O by remving it from the sectors_under_io
 * list */
static void
unmark_sector_under_io(block_sector_t sector)
{
  struct list_elem *e;
  for (e = list_begin (&sectors_under_io); e != list_end (&sectors_under_io);
       e = list_next (e)) {
    struct sector_under_io *s = list_entry(e, struct sector_under_io, elem);
    if (s->sector == sector) {
      list_remove(&s->elem);
      free(s);
      return;
    }
  }
}

/* Determine if a sector is currently undergoing I/O */
static bool
is_sector_under_io(block_sector_t sector)
{
  struct list_elem *e;
  for (e = list_begin (&sectors_under_io); e != list_end (&sectors_under_io);
       e = list_next (e)) {
    struct sector_under_io *s = list_entry(e, struct sector_under_io, elem);
    if (s->sector == sector) {
      return true;
    }
  }
  return false;
}

/* Retrieve the oldest element in the cache, which is just at the back
 * of the list */
static struct cache_elem*
get_oldest_cache_elem(void)
{
  struct list_elem *back_elem = list_back(&cache_list); 
  return list_entry(back_elem, struct cache_elem, list_elem);
}

/* We are out of room in the cache
   so evict the element at the back of the list */
static void
cache_evict()
{
  /* Find first cache_elem that is not under I/O */
  struct cache_elem *c = get_oldest_cache_elem();
  while (is_sector_under_io(c->sector)) {
    cond_wait(&io_finished, &cache_lock);
    c = get_oldest_cache_elem();
  }
  int sector_being_evicted = c->sector;
  mark_sector_under_io(sector_being_evicted);
  
  /* Wait until no other processes are reading from/writing to this cache_elem */
  while (c->num_operations > 0) {
    cond_wait(&operations_finished, &cache_lock);
  }
  
  lock_release(&cache_lock);
  
  /* I/O action, so don't block operations on other sectors*/
  if (c->is_dirty) {
    block_write(fs_device, c->sector, c->data);
  }

  lock_acquire(&cache_lock);
  
  list_remove(&c->list_elem);
  hash_delete(&cache_hash, &c->hash_elem);
  free(c);
  unmark_sector_under_io(sector_being_evicted);
  cond_broadcast(&io_finished, &cache_lock);
}

/* Read a full sector from the cache */
void 
cache_read(block_sector_t sector, void* buffer)
{
  cache_read_bytes(sector, buffer, BLOCK_SECTOR_SIZE, 0);
}

/* Write a full sector to the cache */
void 
cache_write(block_sector_t sector, const void* buffer)
{
  cache_write_bytes(sector, buffer, BLOCK_SECTOR_SIZE, 0);
}

/* When an operation finishes we decrement the number of pending
 * operations on this cache element. If we are at 0 operations
 * on this element, we brodcast so that an eviction of this 
 * block can continue */
static void
mark_finished_operation(struct cache_elem *c)
{
  lock_acquire(&cache_lock);
  c->num_operations--;
  if (c->num_operations == 0) {
    cond_broadcast(&operations_finished, &cache_lock);
  }
  lock_release(&cache_lock);
}

/* Perform read ahead for sector SECTOR. This operation has races
 * with cache_done, because we cannot get an element from the cache
 * after the cache elements have been freed. To prevent this race
 * we have a done_lock, which must be held in order to get
 * from the cache. */
void
cache_perform_read_ahead(block_sector_t sector)
{

  lock_acquire(&cache_lock);
  lock_acquire(&done_lock);
  if(cache_is_done()) {
    lock_release(&done_lock);
    lock_release(&cache_lock);
    return;
  }
  struct cache_elem* c = cache_get(sector);
  lock_release(&done_lock);
  lock_release(&cache_lock);
  
  mark_finished_operation(c);
}

/* Read SIZE bytes from SECTOR into BUFFER */
void 
cache_read_bytes(block_sector_t sector, void* buffer, int size,
                        int offset)
{
  lock_acquire(&cache_lock);
  struct cache_elem* c = cache_get(sector);
  lock_release(&cache_lock);

  /* Don't block other processes here because non-I/O action */
  memcpy(buffer, c->data + offset, size);

  mark_finished_operation(c);
}

/* Write SIZE bytes from BUFFER into SECTOR */
void 
cache_write_bytes(block_sector_t sector, const void* buffer, 
      int size, int offset)
{
  lock_acquire(&cache_lock);
  struct cache_elem* c = cache_get(sector);
  c->is_dirty = true;
  lock_release(&cache_lock);

  /* Don't block other processes here because non-I/O action */
  if (offset == 0 && size == BLOCK_SECTOR_SIZE) {
    memset(c->data, 0, BLOCK_SECTOR_SIZE);
  }
  memcpy(c->data + offset, buffer, size);
  
  mark_finished_operation(c);
}

/* Set sector SECTOR to zeros */
void 
cache_set_to_zero(block_sector_t sector)
{
  lock_acquire(&cache_lock);
  struct cache_elem *c = cache_get(sector);
  c->is_dirty = true;
  lock_release(&cache_lock);

  /* Don't block other processes here because non-I/O action */
  memset(c->data, 0, BLOCK_SECTOR_SIZE);
  
  mark_finished_operation(c);
}

/* Mark this cache block as dirty */
void 
cache_set_dirty(block_sector_t sector)
{
  lock_acquire(&cache_lock);
  struct cache_elem* c = cache_get(sector);
  c->is_dirty = true;
  lock_release(&cache_lock);
}

/* Get the cache element for this sector */
static struct cache_elem*
cache_get(block_sector_t sector)
{
  if (sector > sector_max)
    PANIC("cache_get(): INVALID SECTOR REQUESTED");
  if (sector == FREE_MAP_SECTOR)
    return &free_map_cache;
  
  /* Block if sector is under I/O. The block is either being read into
     the cache from disk or being written from the cache to disk. */
  while (is_sector_under_io(sector)) {
    cond_wait(&io_finished, &cache_lock);
  }

  struct cache_elem *c = cache_lookup(sector);

  if(cache_is_done()) return NULL;

  if (c) {
    /* Block is already in cache, so move it to the front */
    cache_reinsert(c);
    cache_hits++;
  } else {
    if (cache_size() == MAX_CACHE_SIZE) {
      cache_evict();
    } 
    c = cache_insert(sector);
    cache_misses++;
  }
  
  c->num_operations++;
  
  return c; 
}

/* Print cache hits and misses at the end of the run */
void
cache_stats(void)
{
  printf("Cache hits=%d, misses=%d\n", cache_hits, cache_misses);
}


/* Find out if the cache done flag has been set */
int
cache_is_done(void)
{
  return cache_stop;
}

/* The filesystem is done, so we should write all the
 * dirty cache blocks back to disk, and free the malloc'd
 * cache elements */
void
cache_done(void)
{
  cache_flush(); 

  lock_acquire(&done_lock);
  cache_stop = 1;  
  /* Free cache elements */
  struct list_elem *e;
  for (e = list_begin (&cache_list); e != list_end (&cache_list);
    /* must get next before free */)
    {
      struct cache_elem* c = 
          list_entry(e, struct cache_elem, list_elem);
       e = list_next (e);
  
      free(c);
    }
  
  lock_release(&done_lock);
}

/* Write any dirty blocks back to disk */
void
cache_flush(void)
{
  lock_acquire(&cache_lock);
  struct list_elem *e;
  for (e = list_begin (&cache_list); e != list_end (&cache_list);
       e = list_next (e))
    {
      struct cache_elem* c = 
          list_entry(e, struct cache_elem, list_elem);

      if(c->is_dirty && !is_sector_under_io(c->sector))
      {
        c->num_operations++;
        lock_release(&cache_lock);
        
        /* Treat block flush as an operation, not an I/O action. Because
           we are not evicting the block, other processes should be allowed
           to read from/write to the block while it is being written to disk.
           Like other operations, this should block I/O actions. */
        block_write(fs_device, c->sector, c->data);
        
        lock_acquire(&cache_lock);
        c->num_operations--;
        c->is_dirty = false;
        if (c->num_operations == 0) {
          cond_broadcast(&operations_finished, &cache_lock);
        }
      }
    }
  lock_release(&cache_lock);
  
  block_write(fs_device, FREE_MAP_SECTOR, free_map_cache.data);
}

