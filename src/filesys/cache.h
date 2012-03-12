#ifndef FILESYS_CACHE_H
#define FILESYS_CACHE_H

#include <list.h>
#include <hash.h>
#include "devices/block.h"
#include <stdbool.h>

#define MAX_CACHE_SIZE 64

struct cache_elem{
  block_sector_t sector; 
  bool is_dirty;
  struct list_elem list_elem; /* List element for cache*/
  struct hash_elem hash_elem; /* Hash elemetn for cache*/
  char data[512]; /* Cache data */ 
};


void cache_read_bytes(block_sector_t sector, void* buffer, 
                      int size, int offset);
void cache_write_bytes(block_sector_t sector, const void* buffer, 
                      int size, int offset);
void cache_read(block_sector_t sector, void* buffer);
void cache_write(block_sector_t sector, const void* buffer);

struct cache_elem* cache_get(block_sector_t sector);
void cache_init(void);
void cache_stats(void);

#endif /* filesys/cache.h */
