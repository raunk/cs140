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


/* Copy the last path component of PATHNAME into the buffer DEST.
 * Return a boolean of whether or not a valid pathname was copied.
 * If the path is something like:
 *    /a/b/cde         
 * then "cde" will be copied, and true returned. 
 * If it is like
 *    /a/b/cde/
 * then false will be returned. 
 * If there is no slash then we simply copy the full path name */
bool
last_path_component(const char* pathname, char* dest)
{
  int path_len = strlen(pathname);
  char* last_slash = strrchr(pathname, '/');
  if(last_slash != NULL)
  {
    int len = path_len - ( last_slash - pathname);
    strlcpy(dest, last_slash + 1, len); 
    return len != 1;
  }
  // Then there is no slash so the full path is the last 
  // path component.
  strlcpy(dest, pathname, path_len + 1);
  return true;
}


/* Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool
filesys_create (const char *name, off_t initial_size) 
{
  block_sector_t inode_sector = 0;
  // Open the parent directory
  struct dir* dir = dir_open_parent(name);
  
  // Extract the last part of the pathname, which should 
  // be the filename
  char file_name[NAME_MAX + 1]; 
  bool is_file = last_path_component(name, file_name); 
  
  bool success = (dir != NULL && is_file
                  && free_map_allocate (1, &inode_sector)
                  && inode_create (inode_sector, initial_size, false)
                  && dir_add (dir, file_name, inode_sector));
  if (!success && inode_sector != 0) 
    free_map_release (inode_sector, 1);
  dir_close (dir);

/*
  printf("FILESYS CREATE: Wantd file name=%s, got sector=%d\n",
      name, inode_sector);
*/
//

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


/* Recursively lookup pathname starting at directory CUR.
 * We find the first path component, and if that is also
 * the last component, open the file. Otherwise we continue
 * the recursive search */
struct inode*
filesys_lookup_recursive(const char* pathname, struct dir* cur)
{
  int pathlen = strlen(pathname);
  if(pathlen == 0)
    return NULL;

  char component[NAME_MAX + 1];

  while(pathname[0] == '/')
    pathname++;

  //printf("Lookup in pathname %s\n", pathname);

  bool is_last_component = first_path_component(pathname, component);
  
  /* Base case */
  if(strlen(component) == 0) {
    struct inode* inode = dir_get_inode(cur);
    dir_close(cur);
    return inode;
  }

  //printf("First component '%s'\n", component);

  struct inode* inode = NULL;

  bool found = dir_lookup(cur, component, &inode);
  
  /* Close the directory if we opened it (working dir is already open) */
  dir_close(cur);
  
  //printf("Found component? %d\n", found);

  if(!found)
    return NULL;


  if(is_last_component)
  {
    //printf("Was last component\n");
    return inode; 
  }else{
    struct dir* next = dir_open(inode);
    return filesys_lookup_recursive(pathname + strlen(component), next);    
  }
}

/* Return whether it is a relative path. It is absolute if it
 * starts with / */
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
//  printf("Lookup %s\n", pathname);

  struct dir* start_dir = NULL; 
  if(is_relative_path(pathname))
  {
    //printf("Relative\n");
    start_dir = thread_get_working_directory();
  }else{
    //printf("Absolute\n");
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
  struct dir *dir = dir_open_parent (name);
  struct inode* inode = filesys_lookup(name);

  if(inode == NULL) {
    return false;
  }
  
  //printf("Removing inumber %d\n", inode_get_inumber(inode));
  //printf("-----------------------------------------------\n");
 
  if(inode_isdir(inode)) {
    
    // if is current working dir
    if(dir_get_inode(thread_get_working_directory()) == inode) {
      //printf("Failing inode is workind dir...\n");
      return false;
    }
    
    // if its already open
    //printf("Going to remove %d\n", inode_get_inumber(inode));
    inode_close(inode);
    if(inode_isopen(inode)) {
      //printf("Failing inode is open...\n");
      return false;
    }
     
    /* it's not already open so open it, we need to
       look it up again to make sure the inode is open */
    inode = filesys_lookup(name);
    struct dir *child = dir_open(inode);
    if(!dir_isempty(child)) {
      //printf("Failing dir is not empty...\n");
      dir_close(child);
      return false;
    }
    dir_close(child);
      
  }
  char filename[NAME_MAX+1];
  last_path_component(name, filename);
  
  bool success = dir != NULL && dir_remove (dir, filename);
  
  // only close dir if we successfully removed it
  dir_close (dir); 
  //printf("-----------------------------------------------\n");

  return success;
}

/* Formats the file system. */
static void
do_format (void)
{
  printf ("Formatting file system...");
  free_map_create ();
  if (!dir_create (ROOT_DIR_SECTOR, ROOT_DIR_SECTOR))
    PANIC ("root directory creation failed");
  free_map_close ();
  printf ("done.\n");
}
