#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "filesys/cache.h"
#include <stdio.h>

static block_sector_t read_indirect_block_pointer(block_sector_t sector,
    int ptr_index);
static void write_indirect_block_pointer(block_sector_t sector, int ptr_index,
    block_sector_t ptr_val);
static block_sector_t read_inode_disk_pointer(block_sector_t sector,
    int ptr_index);
static void write_inode_disk_pointer(block_sector_t sector, int ptr_index,
    block_sector_t ptr_val);
static bool read_inode_disk_is_dir(block_sector_t sector);
static off_t read_inode_disk_length(block_sector_t sector);
static void write_inode_disk_length(block_sector_t sector, off_t length);

static block_sector_t handle_direct_block(block_sector_t base_sector,
    int file_sector_idx);
static block_sector_t handle_indirect_block(block_sector_t base_sector,
    int file_sector_idx);
static block_sector_t handle_doubly_indirect_block(block_sector_t base_sector,
    int file_sector_idx);
    
static void execute_read_ahead(void *aux UNUSED);
static void handle_read_ahead(struct inode *inode, int inode_len,
    off_t size, off_t offset);
    
void init_indirect_block(block_sector_t sector);
void print_index(block_sector_t* b);
void print_indirect(block_sector_t sec);
void free_inode_used_blocks(struct inode* inode);
void check_length(struct inode* inode, off_t new_length);

bool free_map_setup = false;

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44
#define INODE_DIRECT_BLOCK_COUNT 12
#define INDIRECT_BLOCK_INDEX 12
#define DOUBLY_INDIRECT_BLOCK_INDEX 13 
#define INODE_INDEX_COUNT 14
#define NUM_BLOCK_POINTERS (BLOCK_SECTOR_SIZE / sizeof(uint32_t))

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
  {
    block_sector_t start;               /* First data sector. */
    off_t length;                       /* File size in bytes. */
    unsigned magic;                     /* Magic number. */
    block_sector_t index[INODE_INDEX_COUNT];         
    bool is_dir;
    uint32_t unused[110];               /* Not used. */
  };

/* A struct which represents pointers to disk blocks for single
 * and doubly indirect blocks for the multilevel index */
struct indirect_block
  {
    uint32_t pointers[NUM_BLOCK_POINTERS];
  };

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}

/* In-memory inode. */
struct inode 
  {
    struct list_elem elem;              /* Element in inode list. */
    block_sector_t sector;              /* Sector number of disk location. */
    int open_cnt;                       /* Number of openers. */
    bool removed;                       /* True if deleted, false otherwise. */
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
//    struct inode_disk data;             /* Inode content. */
  };


void
init_indirect_block(block_sector_t sector)
{
  // struct indirect_block ib;
  // memset(&ib, 0, NUM_BLOCK_POINTERS);
  // cache_write(sector, &ib);
  cache_set_to_zero(sector);
}



void
print_index(block_sector_t* b)
{
  int i;
  printf("=====\n");
  for(i = 0; i < 12; i++)
    printf("%d ", b[i]);
  printf("\n~~~~\n");
}

void
print_indirect(block_sector_t sec)
{
  struct indirect_block ib;
  cache_read(sec, &ib);
  int i;
  printf("\n");
  for(i = 0; i < 128; i++)
  {
    printf("%d ", ib.pointers[i]);
    if(i % 32 == 0) printf("\n");
  }
  printf("\n");
}

static block_sector_t
read_indirect_block_pointer(block_sector_t sector, int ptr_index)
{
  block_sector_t ptr;
  cache_read_bytes(sector, &ptr, sizeof(block_sector_t),
      ptr_index * sizeof(block_sector_t));
  return ptr;
}

static void
write_indirect_block_pointer(block_sector_t sector, int ptr_index,
    block_sector_t ptr_val)
{
  cache_write_bytes(sector, &ptr_val, sizeof(block_sector_t),
      ptr_index * sizeof(block_sector_t));
}

#define INODE_DISK_LENGTH_OFFSET (sizeof(block_sector_t))

#define INODE_DISK_PTRS_OFFSET \
(INODE_DISK_LENGTH_OFFSET + sizeof(off_t) + sizeof(unsigned))

#define INODE_DISK_ISDIR_OFFSET \
(INODE_DISK_PTRS_OFFSET + INODE_INDEX_COUNT*sizeof(block_sector_t))

static block_sector_t
read_inode_disk_pointer(block_sector_t sector, int ptr_index)
{
  block_sector_t ptr;
  cache_read_bytes(sector, &ptr, sizeof(block_sector_t),
      INODE_DISK_PTRS_OFFSET + ptr_index * sizeof(block_sector_t));
  return ptr;
}

static void
write_inode_disk_pointer(block_sector_t sector, int ptr_index,
    block_sector_t ptr_val)
{
  cache_write_bytes(sector, &ptr_val, sizeof(block_sector_t),
      INODE_DISK_PTRS_OFFSET + ptr_index * sizeof(block_sector_t));
}

static bool
read_inode_disk_is_dir(block_sector_t sector)
{
  /* The free map sector is a special case */
/*  if(sector == FREE_MAP_SECTOR)
     return false;
*/
  bool is_dir;
  cache_read_bytes(sector, &is_dir, sizeof(bool), INODE_DISK_ISDIR_OFFSET);
  return is_dir & 0x1;
//  return is_dir;
}

static off_t
read_inode_disk_length(block_sector_t sector)
{
  off_t length;
  cache_read_bytes(sector, &length, sizeof(off_t), INODE_DISK_LENGTH_OFFSET);
  return length;
}

static void
write_inode_disk_length(block_sector_t sector, off_t length)
{
  cache_write_bytes(sector, &length, sizeof(off_t),
      INODE_DISK_LENGTH_OFFSET);
}

static block_sector_t
handle_direct_block(block_sector_t base_sector, int file_sector_idx) 
{
  block_sector_t result = read_inode_disk_pointer(base_sector,
                                                  file_sector_idx);

  if(base_sector == FREE_MAP_SECTOR)
  {
    return FREE_MAP_DATA_SECTOR;
  }

  if(result == 0)//  && base_sector != FREE_MAP_SECTOR)
  {
    bool allocated = free_map_allocate(1, &result);
    if(!allocated) {
      return -1; 
    }
    write_inode_disk_pointer(base_sector, file_sector_idx, result);
    cache_set_to_zero(result);
  }  
  
  return result;
}

static block_sector_t
handle_indirect_block(block_sector_t base_sector, int file_sector_idx)
{
  int idx = file_sector_idx - INODE_DIRECT_BLOCK_COUNT;

  block_sector_t ib_sector =
      read_inode_disk_pointer(base_sector, INDIRECT_BLOCK_INDEX);
  if(ib_sector == 0)
    {
      bool allocated = free_map_allocate(1, &ib_sector);
      if(!allocated) {
        return -1; 
      }
      write_inode_disk_pointer(base_sector, INDIRECT_BLOCK_INDEX, ib_sector);
      init_indirect_block(ib_sector);
    }

  block_sector_t result = read_indirect_block_pointer(ib_sector, idx);
  if(result == 0)
  {
    bool allocated = free_map_allocate(1, &result);
    if(!allocated) {
      return -1; 
    }
    write_indirect_block_pointer(ib_sector, idx, result);
    cache_set_to_zero(result);
  }

  return result;
}

static block_sector_t
handle_doubly_indirect_block(block_sector_t base_sector, int file_sector_idx)
{
  int offset = INODE_DIRECT_BLOCK_COUNT + NUM_BLOCK_POINTERS;
  int adjusted = file_sector_idx - offset;
  
  int first_idx = adjusted / NUM_BLOCK_POINTERS;
  int second_idx = adjusted % NUM_BLOCK_POINTERS;
  
  block_sector_t ib_sector =
      read_inode_disk_pointer(base_sector, DOUBLY_INDIRECT_BLOCK_INDEX);
  if(ib_sector == 0)
    {
      bool allocated = free_map_allocate(1, &ib_sector);
      if(!allocated) {
        return -1; 
      }
      write_inode_disk_pointer(base_sector,
          DOUBLY_INDIRECT_BLOCK_INDEX, ib_sector);
      init_indirect_block(ib_sector);
    }

  block_sector_t next_block =
      read_indirect_block_pointer(ib_sector, first_idx);
  if(next_block == 0)
    {
      bool allocated = free_map_allocate(1, &next_block);
      if(!allocated) {
        return -1; 
      }
      write_indirect_block_pointer(ib_sector, first_idx, next_block);
      init_indirect_block(next_block);
    }

  block_sector_t result = read_indirect_block_pointer(next_block, second_idx);
  if(result == 0)
  {
    bool allocated = free_map_allocate(1, &result);
    if(!allocated) {
      return -1; 
    }
    write_indirect_block_pointer(next_block, second_idx, result);
    cache_set_to_zero(result);
  }
  
  return result;
}



/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t
byte_to_sector (const struct inode *inode, off_t pos) 
{

    ASSERT (inode != NULL);
    // TODO: Fail if bigger pos > 8MB

    block_sector_t file_sector_idx = pos / BLOCK_SECTOR_SIZE;

    if(pos > inode_length(inode))
      return -1;

    block_sector_t base_sector = inode->sector;
    if(file_sector_idx < INODE_DIRECT_BLOCK_COUNT)
    {
      return handle_direct_block(base_sector, file_sector_idx); 
    }else if(file_sector_idx < INODE_DIRECT_BLOCK_COUNT + NUM_BLOCK_POINTERS)
    {
      return handle_indirect_block(base_sector, file_sector_idx);
    } else {
      // We are offset 140 block pointers because of the direct 
      // block and singly indirect block, so the 140th block of the 
      // file will correspond to the 0th index in the 0th doubly 
      // indirect block 
      return handle_doubly_indirect_block(base_sector, file_sector_idx);
    }
}

/* Free all of the blocks used by this inode */
void
free_inode_used_blocks(struct inode* inode)
{
  off_t len = inode_length(inode);
  off_t cur = 0;
  while(len > 0)
  {
    block_sector_t sec = byte_to_sector(inode, cur);
    free_map_release (sec, 1);
    cur += BLOCK_SECTOR_SIZE;
    len -= BLOCK_SECTOR_SIZE; 
  }

  free_map_release(inode->sector, 1);
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

static struct thread *read_ahead_thread;
static struct list read_ahead_queue;
static struct lock read_ahead_lock;
static struct condition do_read_ahead;

struct read_ahead_sector {
  block_sector_t sector;
  struct list_elem elem;
};

/* Initializes the inode module. */
void
inode_init (void) 
{
  list_init (&open_inodes);
  
  list_init(&read_ahead_queue);
  lock_init(&read_ahead_lock);
  cond_init(&do_read_ahead);
  
  tid_t tid = thread_create("read_ahead", PRI_DEFAULT, execute_read_ahead, NULL);
  read_ahead_thread = thread_get_by_tid(tid);
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (block_sector_t sector, off_t length, bool is_dir)
{
//  printf("\n\nCreate inode %d with size=%d\n\n", sector, length);
  struct inode_disk *disk_inode = NULL;
  bool success = false;

  ASSERT (length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);

  disk_inode = calloc (1, sizeof *disk_inode);
  if (disk_inode != NULL)
    {
      disk_inode->length = length;
      disk_inode->is_dir = is_dir;
      disk_inode->magic = INODE_MAGIC;
      disk_inode->start = sector;
      
      size_t j;
      for(j = 0; j < INODE_INDEX_COUNT; j++)
        {
          disk_inode->index[j] = 0; // Mark this block as unused
        }

      cache_write(sector, disk_inode);
      
      // Just created inode santiy check
      success = true;
      free (disk_inode);
    }

  return success;
}

/* Determine if the passed in inode is a directory */
bool
inode_isdir(struct inode* inode)
{
  return read_inode_disk_is_dir(inode->sector);
}

bool
inode_isopen (struct inode* inode)
{
  struct list_elem *e;
  struct inode *cur_inode;
  /* Check whether this inode is already open. */
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e)) 
    {
      cur_inode = list_entry (e, struct inode, elem);
      //printf("open inode sector: %d\n", cur_inode->sector);
      if (cur_inode == inode) 
        {
          return true; 
        }
    }
  return false;
}


/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (block_sector_t sector)
{
  struct list_elem *e;
  struct inode *inode;

  //printf("INODE OPEN - sector %d\n", sector);

  /* Check whether this inode is already open. */
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e)) 
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector) 
        {
          inode_reopen (inode);
          return inode; 
        }
    }

  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL)
    return NULL;
    

  /* Initialize. */
  list_push_front (&open_inodes, &inode->elem);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;

  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  if (inode != NULL)
    inode->open_cnt++;
  return inode;
}

/* Returns INODE's inode number. */
block_sector_t
inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
}

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode) 
{
  /* Ignore null pointer. */
  if (inode == NULL)
    return;
  
  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0)
    {
      /* Remove from inode list and release lock. */
      list_remove (&inode->elem);

      /* Deallocate blocks if removed. */
      if (inode->removed) 
        {
            free_inode_used_blocks(inode);
        }

      free (inode); 
    }
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode) 
{
  ASSERT (inode != NULL);
  inode->removed = true;
}

/* To be executed by the read-ahead thread. Continuously pops read-ahead
   requests from the queue and reads the specified block in to the cache. */
static void
execute_read_ahead(void *aux UNUSED)
{
  while (true) {
    lock_acquire(&read_ahead_lock);
    if (list_empty(&read_ahead_queue)) {
      cond_wait(&do_read_ahead, &read_ahead_lock);
    }
    struct list_elem *e = list_pop_front(&read_ahead_queue);
    lock_release(&read_ahead_lock);
    struct read_ahead_sector *s = list_entry(e, struct read_ahead_sector, elem);
    cache_perform_read_ahead(s->sector);
    free(s);
  }
}

/* If there is a sector left in the file immediately following the 
   requested sectors, send a read-ahead request for that sector.  */
static void
handle_read_ahead(struct inode *inode, int inode_len, off_t size, off_t offset)
{
  int last_sector_bytes_left =
      BLOCK_SECTOR_SIZE - ((offset + size) % BLOCK_SECTOR_SIZE);
  int total_sector_bytes_covered = (offset + size) + last_sector_bytes_left;
  if (inode_len > total_sector_bytes_covered) {
    struct read_ahead_sector *s =
        (struct read_ahead_sector *) malloc(sizeof(struct read_ahead_sector));
    s->sector = byte_to_sector(inode, total_sector_bytes_covered + 1);
    lock_acquire(&read_ahead_lock);
    bool was_empty = list_empty(&read_ahead_queue);
    list_push_back(&read_ahead_queue, &s->elem);
    if (was_empty) {
      cond_signal(&do_read_ahead, &read_ahead_lock);
    }
    lock_release(&read_ahead_lock);
  }
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset) 
{
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;

  off_t inode_len = inode_length(inode);
  
  /* Check if bytes requested go past length of file */
  if(offset >= inode_len) {
    return 0;
  }
  if(offset + size > inode_len) {
    size = inode_len - offset;
  }
  
  handle_read_ahead(inode, inode_len, size, offset);

  while (size > 0) 
    {
      /* Disk sector to read, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      // printf("sector being read: %d\n", sector_idx);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < sector_left ? size : sector_left;

      if (chunk_size <= 0)
        break;

      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          /* Read full sector directly into caller's buffer. */
          cache_read(sector_idx, buffer + bytes_read);
        }
      else 
        {
          cache_read_bytes(sector_idx, buffer + bytes_read, chunk_size,
              sector_ofs);
        }
      
      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }
        
  return bytes_read;
}


void
check_length(struct inode* inode, off_t new_length)
{
  struct inode_disk id;
  cache_read(inode->sector, &id);
//  printf("Old len=%d, New len=%d\n", id->length, new_length);
  if(new_length > id.length)
    {
      // Write the new length back to the buffer cache block.
      write_inode_disk_length(inode->sector, new_length);
    }  
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset) 
{
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;

  if (inode->deny_write_cnt)
    return 0;

//  printf("inode.c:inode_write_at: Writing inode=%d\n", inode_get_inumber(inode));
  check_length(inode, offset + size);
  
  while (size > 0) 
    {
      /* Sector to write, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
 //     printf("inode.c:inode_write_at: sector being written: %d\n", sector_idx);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int chunk_size = size < sector_left ? size : sector_left;

      if (chunk_size <= 0)
        break;

      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          /* Write full sector directly to disk. */
          cache_write(sector_idx, buffer + bytes_written);
        }
      else 
        {
          cache_write_bytes(sector_idx, buffer + bytes_written,
                        chunk_size, sector_ofs);
        }

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }

  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode) 
{
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode) 
{
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{
  if(inode->sector == FREE_MAP_SECTOR)
    return BLOCK_SECTOR_SIZE;
    
  return read_inode_disk_length(inode->sector);
}
