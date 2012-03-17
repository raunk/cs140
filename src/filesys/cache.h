#ifndef FILESYS_CACHE_H
#define FILESYS_CACHE_H

#include <list.h>
#include <hash.h>
#include "devices/block.h"
#include <stdbool.h>

#define MAX_CACHE_SIZE 64

void cache_read_bytes(block_sector_t sector, void* buffer, 
                      int size, int offset);
void cache_write_bytes(block_sector_t sector, const void* buffer, 
                      int size, int offset);
void cache_read(block_sector_t sector, void* buffer);
void cache_write(block_sector_t sector, const void* buffer);
void cache_set_to_zero(block_sector_t sector);

void cache_perform_read_ahead(block_sector_t sector);

void cache_init(void);
void cache_stats(void);

void cache_done(void);
void cache_flush(void);

void cache_set_dirty(block_sector_t sector);

#endif /* filesys/cache.h */
