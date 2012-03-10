#ifndef FILESYS_CACHE_H
#define FILESYS_CACHE_H

#include <list.h>
#include <hash.h>
#include "devices/block.h"

struct cache_elem{
  block_sector_t sector; 
  
  struct list_elem list_elem; /* List element for cache*/
  struct hash_elem hash_elem; /* Hash elemetn for cache*/
  char data[128]; /* Cache data */ 
};


struct cache_elem* cache_get(block_sector_t sector);

#endif /* filesys/cache.h */
