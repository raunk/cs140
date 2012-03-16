#include "filesys/directory.h"
#include <stdio.h>
#include <string.h>
#include <list.h>
#include "filesys/filesys.h"
#include "filesys/inode.h"
#include "threads/malloc.h"

/* A directory. */
struct dir 
  {
    struct inode *inode;                /* Backing store. */
    off_t pos;                          /* Current position. */
  };

/* A single directory entry. */
struct dir_entry 
  {
    block_sector_t inode_sector;        /* Sector number of header. */
    char name[NAME_MAX + 1];            /* Null terminated file name. */
    bool in_use;                        /* In use or free? */
  };


void
print_dir(struct dir* dir)
{
  struct dir_entry e;
  size_t ofs;
  printf("directory.c:print_dir--------- DIR ENTRIES FOR -------------------\n");
  printf("\t\tDIR IS %p\n", dir);
  printf("\t\tINODE IS %d\n", inode_get_inumber(dir->inode));
  for (ofs = 0; inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
       ofs += sizeof e) 
  {
    printf("\t\tDir Entry: sector=%d, name=%s, inused=%d\n", 
         e.inode_sector, e.name, e.in_use); 
        
  }
  printf("\t\t------------- END DIR ENTRIES FOR -------------------\n");
  return false;
}

/* Creates a directory with parent directory PARENT with 
   space for ENTRY_CNT entries in the
   given SECTOR.  Returns true if successful, false on failure. */
bool
dir_create (block_sector_t sector, block_sector_t parent)
{
  //printf("CREATING DIRECTORY %d FOR PARENT %d\n", sector, parent);
  bool success = inode_create (sector, 0, true); 
  if(success)
  {
    struct dir* d = dir_open(inode_open(sector)); 
    dir_add(d, ".", sector);
    dir_add(d, "..", parent);   
    dir_close(d);
  }
  return success;
}

/* Opens and returns the directory for the given INODE, of which
   it takes ownership.  Returns a null pointer on failure. */
struct dir *
dir_open (struct inode *inode) 
{
  struct dir *dir = calloc (1, sizeof *dir);
  if (inode != NULL && dir != NULL)
    {
      dir->inode = inode;
      dir->pos = 0;
      return dir;
    }
  else
    {
      inode_close (inode);
      free (dir);
      return NULL; 
    }
}

/* Return a pointer to the directory struct given a full
 * path name NAME. For example, given the pathname
 * a/b/c/d
 * this will open and return the directory for a/b/c
 *
 * Additionally, files with no slashes are assumed to come
 * from the root, for example if name = "hello", the parent is
 * "/"
 *
 * Likewise, for files lke "/hello", the parent is also "/"
 * */
struct dir* 
dir_open_parent(const char* name)
{
  // Make a copy of the pathname so we can modify it
  int len = strlen(name);
  char cpy[len + 1];
  strlcpy(cpy, name, len + 1);

/*  printf("Open parent of=%s\n", name);

  printf("Our cpy = %s\n", cpy);
*/
  char* last_slash = strrchr(cpy, '/');
  // We are in the root directory
  if(last_slash == 0 || last_slash == cpy)
  {
    return thread_get_working_directory();
  }

  // Set the last slash to null, so we can look
  // for the file path without the last component
  last_slash[0] = '\0';

//  printf("Cpy now = %s\n", cpy);

  struct inode* parent_dir = filesys_lookup(cpy);
  
  return dir_open(parent_dir);
}


/* Opens the root directory and returns a directory for it.
   Return true if successful, false on failure. */
struct dir *
dir_open_root (void)
{
  return dir_open (inode_open (ROOT_DIR_SECTOR));
}

/* Opens and returns a new directory for the same inode as DIR.
   Returns a null pointer on failure. */
struct dir *
dir_reopen (struct dir *dir) 
{
  return dir_open (inode_reopen (dir->inode));
}

/* Destroys DIR and frees associated resources. */
void
dir_close (struct dir *dir) 
{
  block_sector_t cwd_inumber = inode_get_inumber(dir_get_inode(thread_get_working_directory()));
  block_sector_t dir_inumber = inode_get_inumber(dir_get_inode(dir));
  
  if(cwd_inumber == dir_inumber) {
    //printf("CAN'T CLOSE WORKING DIR INODE!!\n");
    return;
  }
  
  if (dir != NULL)
    {
      inode_close (dir->inode);
      free (dir);
    }
}

/* Returns the inode encapsulated by DIR. */
struct inode *
dir_get_inode (struct dir *dir) 
{
  return dir->inode;
}

/* Searches DIR for a file with the given NAME.
   If successful, returns true, sets *EP to the directory entry
   if EP is non-null, and sets *OFSP to the byte offset of the
   directory entry if OFSP is non-null.
   otherwise, returns false and ignores EP and OFSP. */
static bool
lookup (const struct dir *dir, const char *name,
        struct dir_entry *ep, off_t *ofsp) 
{
  struct dir_entry e;
  size_t ofs;

  //print_dir(dir);
  
  ASSERT (dir != NULL);
  ASSERT (name != NULL);
  //printf("------------- DIR ENTRIES FOR -------------------\n");
  //printf("DIR IS %p\n", dir);
  //printf("INODE IS %d\n", inode_get_inumber(dir->inode));
  for (ofs = 0; inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
       ofs += sizeof e) 
  {
    //printf("Dir Entry: sector=%d, name=%s, inused=%d\n", 
    //     e.inode_sector, e.name, e.in_use); 
        
    if (e.in_use && !strcmp (name, e.name)) 
      {
        if (ep != NULL)
          *ep = e;
        if (ofsp != NULL)
          *ofsp = ofs;
        //printf("------------- END DIR ENTRIES FOR -------------------\n");
        return true;
      }
  }
  //printf("------------- END DIR ENTRIES FOR -------------------\n");
  return false;
}

/* Searches DIR for a file with the given NAME
   and returns true if one exists, false otherwise.
   On success, sets *INODE to an inode for the file, otherwise to
   a null pointer.  The caller must close *INODE. */
bool
dir_lookup (const struct dir *dir, const char *name,
            struct inode **inode) 
{
//  printf("directory.c, dir_lookup: dir inum=%d, find filename=%s\n",
//    inode_get_inumber(dir->inode), name);


  struct dir_entry e;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  if (lookup (dir, name, &e, NULL))
    *inode = inode_open (e.inode_sector);
  else
    *inode = NULL;

  return *inode != NULL;
}

/* Adds a file named NAME to DIR, which must not already contain a
   file by that name.  The file's inode is in sector
   INODE_SECTOR.
   Returns true if successful, false on failure.
   Fails if NAME is invalid (i.e. too long) or a disk or memory
   error occurs. */
bool
dir_add (struct dir *dir, const char *name, block_sector_t inode_sector)
{
  struct dir_entry e;
  off_t ofs;
  bool success = false;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);


  /* Check NAME for validity. */
  if (*name == '\0' || strlen (name) > NAME_MAX)
    return false;

  /* Check that NAME is not in use. */
  if (lookup (dir, name, NULL, NULL))
    goto done;


  /* Set OFS to offset of free slot.
     If there are no free slots, then it will be set to the
     current end-of-file.
     
     inode_read_at() will only return a short read at end of file.
     Otherwise, we'd need to verify that we didn't get a short
     read due to something intermittent such as low memory. */
  for (ofs = 0; inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
       ofs += sizeof e)
  { 
    if (!e.in_use)
      break;
  }
  /* Write slot. */
  e.in_use = true;
  strlcpy (e.name, name, sizeof e.name);
  e.inode_sector = inode_sector;
  /*  
    printf("DIRADD: Dir Entry: sector=%d, name=%s, inused=%d\n", 
        e.inode_sector, e.name, e.in_use); 
  printf("We want to write a dir entry at ofs=%d to inode %p (%d)\n",
      ofs, dir->inode, inode_get_inumber(dir->inode)); 
    */
  success = inode_write_at (dir->inode, &e, sizeof e, ofs) == sizeof e;

  for (ofs = 0; inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
       ofs += sizeof e)
  { 
    /*
    printf("DIRCHECK: Dir Entry: sector=%d, name=%s, inused=%d\n", 
        e.inode_sector, e.name, e.in_use); 
    */
    if (!e.in_use)
      break;
  }

 done:
  return success;
}

/* Removes any entry for NAME in DIR.
   Returns true if successful, false on failure,
   which occurs only if there is no file with the given NAME. */
bool
dir_remove (struct dir *dir, const char *name) 
{
  struct dir_entry e;
  struct inode *inode = NULL;
  bool success = false;
  off_t ofs;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  /* Find directory entry. */
  if (!lookup (dir, name, &e, &ofs))
    goto done;

  /* Open inode. */
  inode = inode_open (e.inode_sector);
  if (inode == NULL)
    goto done;

  /* Erase directory entry. */
  e.in_use = false;
  if (inode_write_at (dir->inode, &e, sizeof e, ofs) != sizeof e) 
    goto done;

  /* Remove inode. */
  inode_remove (inode);
  success = true;

 done:
  inode_close (inode);
  return success;
}

/* Check if directory is empty.  An empty dir only has the
   "." and ".." entries in it so all empty directories have just
   2 entries */
bool
dir_isempty (struct dir *dir)
{
  struct dir_entry e;
  int cur_pos = 0;
  int num_entries = 0;
  while (inode_read_at (dir->inode, &e, sizeof e, cur_pos) == sizeof e) 
  {
    cur_pos += sizeof e;
    
    if (e.in_use)
      {
        num_entries++;
      }
  }
  
  return num_entries == 2;
}


/* Reads the next directory entry in DIR and stores the name in
   NAME.  Returns true if successful, false if the directory
   contains no more entries. */
bool
dir_readdir (struct dir *dir, char name[NAME_MAX + 1])
{
  struct dir_entry e;
  while (inode_read_at (dir->inode, &e, sizeof e, dir->pos) == sizeof e) 
    {
      dir->pos += sizeof e;
      if (e.in_use)
        {
          strlcpy (name, e.name, NAME_MAX + 1);
          return true;
        } 
    }
  return false;
}
