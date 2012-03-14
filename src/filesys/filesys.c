#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "filesys/cache.h"
#include "threads/thread.h"

/* Partition that contains the file system. */
struct block *fs_device;

static void do_format (void);

/* Initializes the file system module.
   If FORMAT is true, reformats the file system. */
void
filesys_init (bool format) 
{
  fs_device = block_get_role (BLOCK_FILESYS);

  if (fs_device == NULL)
    PANIC ("No file system device found, can't initialize file system.");

  cache_init();

  inode_init ();
  free_map_init ();

  if (format) 
    do_format ();


  free_map_open ();
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void
filesys_done (void) 
{
  free_map_close ();
  cache_stats();
  cache_flush();
}

/* Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool
filesys_create (const char *name, off_t initial_size) 
{
  block_sector_t inode_sector = 0;
  struct dir *dir = dir_open_root ();
  bool success = (dir != NULL
                  && free_map_allocate (1, &inode_sector)
                  && inode_create (inode_sector, initial_size)
                  && dir_add (dir, name, inode_sector));
  if (!success && inode_sector != 0) 
    free_map_release (inode_sector, 1);
  dir_close (dir);

  /*printf("FILESYS CREATE: Wantd file name=%s, got sector=%d\n",
      name, inode_sector);
*/
  return success;
}

/* Copy the first path component to DEST and return 
   whether or not this was the last component in the path
  */   
bool
first_path_component(const char* pathname, char* dest)
{
  char* next_slash = strchr(pathname, '/');
  int first_component_length;
  bool is_last;
  if(next_slash == NULL)
  {
    first_component_length = strlen(pathname);      
    is_last = true;
  }else{
    first_component_length = next_slash - pathname; 
    is_last = false;
  }
  strlcpy(dest, pathname, first_component_length + 1); 

  return is_last;
}


struct inode*
filesys_lookup_recursive(const char* pathname, struct dir* cur)
{
  char component[NAME_MAX + 1];

  while(pathname[0] == '/')
    pathname++;

  bool is_last_component = first_path_component(pathname, component);

  struct inode* inode = NULL;

  dir_lookup(cur, component, &inode);
  dir_close(cur);
  if(is_last_component)
  {
    return inode; 
  }else{
    return filesys_lookup_recursive(pathname + strlen(component), inode);    
  }
}

bool
is_relative_path(const char* pathname)
{
  return pathname[0] != '/';
}

/* Lookup an inode given a pathname. We use a different inode
   to start depending on whether this is an absolute or relative
   path */
struct inode* 
filesys_lookup(const char* pathname)
{
  struct dir* start_dir = NULL; 
  if(is_relative_path(pathname))
  {
    start_dir = dir_open(inode_open(thread_get_working_directory_inumber())); 
  }else{
    start_dir = dir_open(inode_open(ROOT_DIR_SECTOR)); 
  }

  return filesys_lookup_recursive(pathname, start_dir); 
}



/* Opens the file with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
struct file *
filesys_open (const char *name)
{
 /* struct dir *dir = dir_open_root ();
  struct inode *inode = NULL;

  //printf("Dir = %p\n", dir);

  if (dir != NULL)
    dir_lookup (dir, name, &inode);
  dir_close (dir);
*/
  //printf("Call file_open(%p)\n", inode);
  struct inode* inode = filesys_lookup(name);
  return file_open (inode);
}

/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool
filesys_remove (const char *name) 
{
  struct dir *dir = dir_open_root ();
  bool success = dir != NULL && dir_remove (dir, name);
  dir_close (dir); 

  return success;
}

/* Formats the file system. */
static void
do_format (void)
{
  printf ("Formatting file system...");
  free_map_create ();
  if (!dir_create (ROOT_DIR_SECTOR, 0))
    PANIC ("root directory creation failed");
  free_map_close ();
  printf ("done.\n");
}
