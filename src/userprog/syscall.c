#include "devices/input.h"
#include "devices/shutdown.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "lib/user/syscall.h"
#include "userprog/syscall.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include "userprog/exception.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/init.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/thread.h"
#include <string.h>
#include "vm/page.h"
#include "vm/frame.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include <stdbool.h>

static void* get_nth_parameter(void* esp, int param_num, int datasize, 
                    struct intr_frame * f);
static void syscall_handler (struct intr_frame *);
static void syscall_check_user_pointer (void *ptr, struct intr_frame * f);
static void syscall_check_buffer_bounds(char* buffer, unsigned length, 
                              struct intr_frame *f);
static void syscall_write(struct intr_frame *f);
static void syscall_exit(struct intr_frame *f);
static void syscall_exec(struct intr_frame *f);
static void syscall_wait(struct intr_frame *f);
static void syscall_create(struct intr_frame * f);
static void syscall_open(struct intr_frame *f);
static void syscall_read(struct intr_frame *f);
static void syscall_filesize(struct intr_frame *f);
static void syscall_remove(struct intr_frame *f);
static void syscall_seek(struct intr_frame *f);
static void syscall_tell(struct intr_frame *f);
static void syscall_close(struct intr_frame *f);
static void syscall_mmap(struct intr_frame *f);
static void syscall_munmap(struct intr_frame *f);
static void syscall_chdir(struct intr_frame *f);
static void syscall_mkdir(struct intr_frame *f);
static void syscall_readdir(struct intr_frame *f);
static void syscall_isdir(struct intr_frame *f);
static void syscall_inumber(struct intr_frame *f);

static void unmap_file_helper(struct mmap_elem* map_elem);
void unmap_file(struct hash_elem* elem, void* aux UNUSED);
void syscall_init (void);

#define MAX_PUTBUF_SIZE 256


/* Checks if a pointer passed by a user program is valid.
   Exits the current process if the pointer found to be invalid. */
void
syscall_check_user_pointer (void *ptr, struct intr_frame * f)
{
  // Check that it is within user memory
  if(is_user_vaddr(ptr)) {
    struct thread *t = thread_current ();
    // Check that memory has been mapped
    
    if(pagedir_get_page (t->pagedir, ptr) != NULL) {
      return;
    }
    
    if(supp_page_bring_into_memory(ptr, false)) {
       return;
    }

    // If it looks like a stack pointer, give them a new
    // stack page and return 
    if(smells_like_stack_pointer(f->esp, ptr))
      {
        install_stack_page(pg_round_down(ptr));
        return;
      }
  }
  
  // Pointer is invalid if we get here
  exit_current_process(-1);
}

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

/* Returns the nth parameter to the system call given the stack pointer. */
static void*
get_nth_parameter(void* esp, int param_num, int datasize, 
                    struct intr_frame * f)
{
  void* param = esp + param_num * sizeof(char*);
  syscall_check_user_pointer(param, f);
  syscall_check_user_pointer(param + datasize - 1, f);
  
  return param;
}

#define MAX_FILE_NAME 14

/* Handle a system call for create. This gets the size
 * and file name, and checks all pointers involved. We
 * also make sure the length of the file name string is
 * in the proper range, and return false if it is not.
 * We set the return value in f->eax.
 */
static void syscall_create(struct intr_frame * f)
{
  unsigned initial_size = *(unsigned*)get_nth_parameter(f->esp, 2, sizeof(unsigned),
                                                          f);
  char* fname = *(char**)get_nth_parameter(f->esp, 1, sizeof(char*), f); 
  syscall_check_user_pointer(fname, f);

  //printf("syscall.c:syscall_create, filename=%s", fname);
  int len = strlen(fname);

  if(len < 1 || len > MAX_FILE_NAME){
    f->eax = false; 
    return;
  }

  bool result = filesys_create(fname, initial_size); 
  f->eax = result; 
}

/* Dispatch method when we need to handle a system call.
 * We read the system call number and call the proper
 * method, and pass it the interrupt frame. */
static void
syscall_handler (struct intr_frame *f) 
{
  syscall_check_user_pointer (f->esp, f);  
  syscall_check_user_pointer (f->esp+sizeof(int*)-1, f);
  
  /* Read sys call number from location pointed to by stack pointer */
  int sys_call_number = *((int*)f->esp);
  if(sys_call_number == SYS_EXIT) {
    syscall_exit(f);
  } else if(sys_call_number == SYS_EXEC) {
    syscall_exec(f);
  } else if(sys_call_number == SYS_WAIT) {
    syscall_wait(f);
  } else if(sys_call_number == SYS_WRITE) {
    syscall_write(f);
  } else if(sys_call_number == SYS_READ) {
    syscall_read(f);
  } else if(sys_call_number == SYS_HALT) {
    shutdown_power_off();
  } else if(sys_call_number == SYS_CREATE){
    syscall_create(f);
  } else if(sys_call_number == SYS_OPEN) {
    syscall_open(f);
  } else if(sys_call_number == SYS_FILESIZE) {
    syscall_filesize(f);
  } else if(sys_call_number == SYS_REMOVE) {
    syscall_remove(f);
  } else if(sys_call_number == SYS_SEEK) {
    syscall_seek(f);
  } else if(sys_call_number == SYS_TELL) {
    syscall_tell(f);
  } else if(sys_call_number == SYS_CLOSE) {
    syscall_close(f);
  } else if(sys_call_number == SYS_MMAP) {
    syscall_mmap(f);
  } else if(sys_call_number == SYS_MUNMAP) {
    syscall_munmap(f);
  } else if(sys_call_number == SYS_CHDIR) {
    syscall_chdir(f);
  } else if(sys_call_number == SYS_MKDIR) {
    syscall_mkdir(f);
  } else if(sys_call_number == SYS_READDIR) {
    syscall_readdir(f);
  } else if(sys_call_number == SYS_ISDIR) {
    syscall_isdir(f);
  } else if(sys_call_number == SYS_INUMBER) {
    syscall_inumber(f);
  }
}

/* Unmaps this mapping element. This means that if this memory
 * has been modified, it needs to be written back to disk at the proper
 * location, and this memory should be freed and reset for this thread,
 * and this entry removed from the supplemental page table */
static void
unmap_file_helper(struct mmap_elem* map_elem)
{
  void* cur_addr = map_elem->vaddr;
  int write_bytes = map_elem->length;

  int cur_tid = thread_current()->tid;
  int offset = 0; 
  struct file* f = file_open(map_elem->inode);
  
  while(write_bytes > 0)
  {
    lock_acquire(&supp_page_lock);
    struct supp_page_entry* sp_entry = supp_page_lookup(cur_tid, cur_addr);
    lock_release(&supp_page_lock);
    
    int page_write_bytes = write_bytes < PGSIZE ? write_bytes : PGSIZE;
    if(pagedir_is_dirty(thread_current()->pagedir, cur_addr))
    {
      file_write_at(f, cur_addr, page_write_bytes, 
                         sp_entry->off); 
    }
    if(sp_entry->status == PAGE_IN_MEM)
      {
          frame_free_user_page(cur_addr);
          pagedir_clear_page(thread_current()->pagedir, cur_addr);
      }  
    supp_remove_entry(cur_tid, cur_addr);

    write_bytes -= page_write_bytes;    
    offset += PGSIZE;
    cur_addr += PGSIZE;
  }
  
  hash_delete(&thread_current()->map_hash, &map_elem->elem); 

  file_close(f);
}

/* Callback function to unmap files on process exit */
void
unmap_file(struct hash_elem* elem, void* aux UNUSED)
{
  struct mmap_elem* e = hash_entry(elem, struct mmap_elem, elem);
  unmap_file_helper(e);
}

/* Unmap all of the files memory mapped by this thread. */
void
handle_unmapped_files(void)
{
  struct thread* cur = thread_current();
  if(hash_empty(&cur->map_hash)) return;

  hash_apply(&cur->map_hash, unmap_file);
  hash_clear(&cur->map_hash, NULL);  
}

/* Exit the current process with status STATUS. Set the exit
 * status of this thead. Also clean up file resources and 
 * signal this thread is dying to any waiting threads using
 * sema_up. */
void
exit_current_process(int status)
{
  struct thread* cur = thread_current();

  cur->exit_status = status;

  /* Unmap any files that were not explicitly unmapped */
  handle_unmapped_files();
  
  /* Clean up all resources (frames, swap slots) held by the thread. */
  frame_cleanup_for_thread(cur);
  
  /* Allow writes for the executing file and close it */
  file_allow_write(cur->executing_file);
  file_close(cur->executing_file);

  sema_up(&cur->is_dying);

  printf("%s: exit(%d)\n", thread_name(), cur->exit_status);
  
  thread_exit();
}


/* Exit system call. Set the exit status and call
 * exit_current_process for final cleanup */ 
static void
syscall_exit(struct intr_frame *f)
{
  void* esp = f->esp;
  int status = *(int*)get_nth_parameter(esp, 1, sizeof(int), f);
  
  f->eax = status;
  exit_current_process(status);
}

static void syscall_chdir(struct intr_frame *f)
{
  void* esp = f->esp;
  char* dir = *(char**)get_nth_parameter(esp, 1, sizeof(char*), f);
  syscall_check_user_pointer(dir, f);
  bool success = true;
  
  // todo: synchronization??
  struct dir* cur_dir = thread_get_working_directory();
  //printf("CUR DIR IS %p CHANGING TO %s\n", cur_dir, dir);
  
  // open new directory
  struct inode* inode = filesys_lookup(dir);

  if(inode == NULL)
  {
    success = false;
  }else{
    thread_current()->working_directory = dir_open(inode);
  }
  // close directory we were just in
  dir_close(cur_dir);
  
  f->eax = success;
}

static void syscall_mkdir(struct intr_frame *f)
{
  void* esp = f->esp;
  char* dir = *(char**)get_nth_parameter(esp, 1, sizeof(char*), f);
  syscall_check_user_pointer(dir, f);
  
  // Make sure passed filename is well-formed
  if(strlen(dir) == 0)
  {
    f->eax = false;
    return;
  }

  // This directory should not exist
  struct inode* inode = filesys_lookup(dir);
  if(inode != NULL)
  {
    f->eax = false;
    inode_close(inode);
    return;
  }
  
  //printf("syscall.c:syscall_mkdir: opening parent dir for path %s\n", dir);
  // The parent directory must already exist
  struct dir* parent_dir = dir_open_parent(dir);
  if(parent_dir == NULL)
  {
    f->eax = false;
    inode_close(inode);
    return;
  }
  
  // Allocate an inode
  block_sector_t result;

  bool allocated = free_map_allocate(1, &result);
  if(!allocated) {
    f->eax = false;
    return; 
  }
  inode = inode_open(result);
  
  //printf("syscall.c:syscall_mkdir: allocated inode %d\n", result);

  // Create a new directory
  struct inode* parent_inode = dir_get_inode(parent_dir);
  dir_create(result, inode_get_inumber(parent_inode));

  // Add this directory to its parent 
  char name[NAME_MAX + 1];
  last_path_component(dir, name); 

  dir_add(parent_dir, name, result);
  
  dir_close(parent_dir);
  //printf("OK GOING TO CLOSE CHILD INODE NOW!!\n");
  inode_close(inode);
  
  //printf("JUST ADDED %s TO PARENT %d\n", name, inode_get_inumber(parent_inode));
  
  f->eax = true;
  return;
}

static void syscall_readdir(struct intr_frame *f)
{
  void* esp = f->esp;
  int fd = *(int*)get_nth_parameter(esp, 1, sizeof(int), f);
  char* name = *(char**)get_nth_parameter(esp, 2, sizeof(char*), f);
  
  syscall_check_user_pointer(name, f);
  syscall_check_user_pointer(name + READDIR_MAX_LEN + 1, f);
  
  struct file_descriptor_elem* fd_elem = thread_get_file_descriptor_elem(fd);
  struct dir* cur_dir = fd_elem->dir;
  
  bool success = false;
  while(true) {
    success = dir_readdir(cur_dir, name);
    if(!success)
      break;
    if(strcmp(name, ".") != 0 && strcmp(name, "..") != 0) {
      break;
    } else {
      // set success to false since we still didn't find a
      // valid dir entry to read
      success = false;
    }
  }
  
  f->eax = success;
}

static void syscall_isdir(struct intr_frame *f)
{
  void* esp = f->esp;
  int fd = *(int*)get_nth_parameter(esp, 1, sizeof(int), f);
  
  
  struct file_descriptor_elem* fd_elem = thread_get_file_descriptor_elem(fd);
  f->eax = file_isdir (fd_elem->f);
}

static void syscall_inumber(struct intr_frame *f)
{
  void* esp = f->esp;
  int fd = *(int*)get_nth_parameter(esp, 1, sizeof(int), f);
  
  struct file_descriptor_elem* fd_elem = thread_get_file_descriptor_elem(fd);
  struct inode* inode = file_get_inode(fd_elem->f);
  f->eax = inode_get_inumber(inode);
}

/* Memory map system call. We take a file descriptor and 
 * address, and map this into memory for the current thread,
 * and do error checking on this locations validity before
 * saving information in the supplemental page table */
static void 
syscall_mmap(struct intr_frame *f)
{
  void* esp = f->esp;
  int fd = *(int*)get_nth_parameter(esp, 1, sizeof(int), f);
  void* addr = *(char**)get_nth_parameter(esp, 2, sizeof(char*), f);

  // File descriptors 0 and 1 are not mappable
  if(fd == 0 || fd == 1)
  {
    f->eax = -1;
    return;
  }

  struct file_descriptor_elem* fd_elem = thread_get_file_descriptor_elem(fd);
  if (!fd_elem) {
    f->eax = -1;
    return;
  }

  int length = file_length(fd_elem->f);

  // Fail if the file had 0 bytes in length
  if(length == 0)
  {
    f->eax = -1;
    return;
  }

  // Fail if addr is not page aligned
  if((void*)addr != pg_round_down(addr))
  {
    f->eax = -1;
    return;
  }

  // Fail if addr is 0
  if(addr == 0)
  {
    f->eax = -1;
    return;
  }

  // Make sure we dont overlap any other mappings
  struct hash_iterator i;
  hash_first(&i, &thread_current()->map_hash);
  while(hash_next(&i))
    {
      struct mmap_elem* e = hash_entry(hash_cur(&i), struct mmap_elem, elem);
      void* map_end = pg_round_up(e->vaddr + e->length);

      // This overlaps another mapping
      if(addr >= e->vaddr && addr <= map_end)
        {
          f->eax = -1;
          return;
        }
    }

  lock_acquire(&supp_page_lock);
  struct supp_page_entry* spe = supp_page_lookup(thread_current()->tid,
                                                  addr);
  lock_release(&supp_page_lock);
  // Error if we are writing over a location that is already in the 
  // supplementary page table
  if(spe != NULL)
    {
      f->eax = -1;
      return;
    }

  // Error if we are trying to map over a stack location
  void* esp_page = pg_round_down(esp);
  if(addr >= esp_page)
    {
      f->eax = -1;
      return;
    }

  int map_id = thread_add_mmap_entry(addr, length, file_get_inode(fd_elem->f));
  int read_bytes = length;
  void* cur_page = (void*)addr;
  int offset = 0;

  struct file* saved_file = file_reopen(fd_elem->f);
  // Save entries in the supplemental page table for this file
  while(read_bytes > 0)
  {
    int page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
    supp_page_insert_for_on_disk(thread_current()->tid, cur_page,
            saved_file, offset, page_read_bytes, true, true);

    read_bytes -= page_read_bytes;
    cur_page += PGSIZE;
    offset += page_read_bytes;
  } 

  f->eax = map_id; 
}

/* The unmap system call. This takes a mapping id, 
 * unmaps this memory for this process */
static void 
syscall_munmap(struct intr_frame *f)
{
  void* esp = f->esp;
  int map_id = *(int*)get_nth_parameter(esp, 1, sizeof(int), f);
  struct mmap_elem* map_elem = thread_lookup_mmap_entry(map_id);

  // Trying to unmap an invalid mapping
  if(map_elem == NULL)
  {
    exit_current_process(-1);
  }

  unmap_file_helper(map_elem);
  free(map_elem);
}


/* Exec system call. Take in the command line arguments
 * and make a call to process_execute. */
static void
syscall_exec(struct intr_frame *f)
{
  void* esp = f->esp;
  char* cmd_line = *(char**)get_nth_parameter(esp, 1, sizeof(char*), f);
  syscall_check_user_pointer (cmd_line, f);

  f->eax = process_execute(cmd_line);
}

/* Wait system call. Make a call to process wait */
static void
syscall_wait(struct intr_frame *f)
{
  void* esp = f->esp;
  int pid = *(int*)get_nth_parameter(esp, 1, sizeof(int), f);
  
  f->eax = process_wait(pid);
}

/* Check the bounds of a buffer that could possibly extend multiple
   pages.  We need to check that each page in the block of pages 
   overlapped by the buffer is valid. */
static void
syscall_check_buffer_bounds(char* buffer, unsigned length, 
                              struct intr_frame *f)
{
  unsigned length_check = length;
  while(length_check >= PGSIZE) {
    syscall_check_user_pointer(buffer+length_check-1, f);
    length_check -= PGSIZE;
  }
  syscall_check_user_pointer(buffer+length_check-1, f);
}

/* Write size bytes from buffer to the open file fd.  Return
   the number of bytes actually written, which may be less than
   size if some bytes could not be written.
   
   If size would extend past file size, write up to EOF and 
   return number written.  Return 0 if no bytes could be written.
   
   FD 1 writes to console.
    */
static void
syscall_write(struct intr_frame *f)
{
  void* esp = f->esp;
  int fd = *(int*)get_nth_parameter(esp, 1, sizeof(int), f);
  char* buffer = *(char**)get_nth_parameter(esp, 2, sizeof(char*), f);
  unsigned length = *(unsigned*)get_nth_parameter(esp, 3, sizeof(unsigned), f);

//  printf("syscall.c:syscall_write: Write len=%d to fd=%d\n", length, fd);
  
  /* Make sure beginning and end of buffer from user are valid addresses. */
  syscall_check_user_pointer(buffer, f);
  syscall_check_buffer_bounds(buffer, length, f);
  

  if (fd == STDOUT_FILENO) {
    /* Write to the console. Should write all of buffer in one call to putbuf(),
       at least as long as size is not bigger than a few hundred bytes. */
    char *buf_tmp = buffer;
    unsigned bytes_left = length;
    while (bytes_left > MAX_PUTBUF_SIZE) {
      putbuf(buf_tmp, MAX_PUTBUF_SIZE);
      buf_tmp += MAX_PUTBUF_SIZE;
      bytes_left -= MAX_PUTBUF_SIZE;
    }
    putbuf(buf_tmp, bytes_left); 
    return;
  }
  
  struct file_descriptor_elem* fd_elem = thread_get_file_descriptor_elem(fd);
  if (!fd_elem) {
    f->eax = 0;
    return;
  }

  // Make sure we are writing to something writeable
  if(file_isdir(fd_elem->f))
    exit_current_process(-1);
  /*
  printf("syscall.c:syscall_write: file pointer = %p\n",
      fd_elem->f);


  printf("syscall.c:syscall_write  file inum = %d\n",
    inode_get_inumber(file_get_inode(fd_elem->f)));
    */
  off_t bytes_written = file_write(fd_elem->f, buffer, length);
  
  /*
  printf("syscall.c:syscall_write  wrote bytes = %d\n",
    bytes_written);
  */
  
  f->eax = bytes_written;
}

/* Open system call. Get the filename and check that it is valid. Open
 * the file and add the file descriptor element to the current thread
 * for bookkeeping */
static void
syscall_open(struct intr_frame *f)
{
  void* esp = f->esp;
  void* file = get_nth_parameter(esp, 1, sizeof(char*), f);
  char* fname = *(char**)file;
  syscall_check_user_pointer(fname, f);

  struct file *fi = filesys_open (fname);
  if (!fi) {
    f->eax = -1;
    return;
  }
  int fd = thread_add_file_descriptor_elem(fi)->fd;

/*
  printf("syscall.c:syscall_open: fd=%d\n", fd);
  printf("syscall.c:syscall_open  file inum = %d\n",
    inode_get_inumber(file_get_inode(fi)));
  */
  /* If a directory we need to open the dir */
  if(file_isdir(fi)) {
    struct dir* dir = dir_open(file_get_inode(fi));
    struct file_descriptor_elem* fd_elem = thread_get_file_descriptor_elem(fd);
    
    fd_elem->dir = dir;
  }
  
  f->eax = fd;
}

/* Read system call. Read the file descriptor, buffer, and length.
 * If we are using the special STDIN file descriptor, then read 
 * from the keyboard. Otherwise make a call to the file system
 * read function and return the bytes read */
static void
syscall_read(struct intr_frame *f)
{
  void* esp = f->esp;
  int fd = *(int*)get_nth_parameter(esp, 1, sizeof(int), f);
  char* buffer = *(char**)get_nth_parameter(esp, 2, sizeof(char*), f);
  unsigned length = *(unsigned*)get_nth_parameter(esp, 3, sizeof(unsigned), f);
  
  /* Make sure beginning and end of buffer from user are valid addresses. */
  syscall_check_user_pointer(buffer, f);
  syscall_check_buffer_bounds(buffer, length, f);
  
  if (fd == STDIN_FILENO) {
    /* Fd 0 reads from the keyboard using input_getc() */
    buffer[0] = input_getc();
    f->eax = sizeof(uint8_t);
    return;
  }
  
  struct file_descriptor_elem* fd_elem = thread_get_file_descriptor_elem(fd);
  if (!fd_elem) {
    f->eax = -1;
    return;
  }
  
  off_t bytes_read = file_read(fd_elem->f, buffer, length);
  f->eax = bytes_read;
}

/* File size system call. Return the size of the file descriptor passed
 * as the first parameter */
static void
syscall_filesize(struct intr_frame *f)
{
  void* esp = f->esp;
  int fd = *(int*)get_nth_parameter(esp, 1, sizeof(int), f);
  
  struct file_descriptor_elem* fd_elem = thread_get_file_descriptor_elem(fd);
  if (!fd_elem) {
    f->eax = 0;
    return;
  }
  
  f->eax = file_length(fd_elem->f);
}


/* Remove a file from the file system */
static void
syscall_remove(struct intr_frame *f)
{
  void* esp = f->esp;
  char* fname = *(char**)get_nth_parameter(esp, 1, sizeof(char*), f);
  syscall_check_user_pointer(fname, f);
  
  bool result = filesys_remove(fname);
  f->eax = result;
}

/* Seek to a specific point in a file given its file descriptor */
static void
syscall_seek(struct intr_frame *f)
{
  void* esp = f->esp;
  int fd = *(int*)get_nth_parameter(esp, 1, sizeof(int), f);
  unsigned position = *(unsigned*)get_nth_parameter(esp, 2, sizeof(unsigned), f);
  
  struct file_descriptor_elem* fd_elem = thread_get_file_descriptor_elem(fd);
  if (!fd_elem) {
    return;
  }
  
  file_seek(fd_elem->f, position);
}

/* Report the current position for this file descriptor */
static void
syscall_tell(struct intr_frame *f)
{
  void* esp = f->esp;
  int fd = *(int*)get_nth_parameter(esp, 1, sizeof(int), f);
  
  struct file_descriptor_elem* fd_elem = thread_get_file_descriptor_elem(fd);
  if (!fd_elem) {
    f->eax = 0;
    return;
  }
  
  f->eax = file_tell(fd_elem->f);
}

/* Close the file given by the current file descriptor and remove it from 
 * the file descriptor element list on a a thread */
static void
syscall_close(struct intr_frame *f)
{
  void* esp = f->esp;
  int fd = *(int*)get_nth_parameter(esp, 1, sizeof(int), f);
  
  struct file_descriptor_elem* fd_elem = thread_get_file_descriptor_elem(fd);
  if (!fd_elem) {
    return;
  }
  
  if(file_isdir(fd_elem->f)) {
    dir_close(fd_elem->dir);
  }
  
  file_close(fd_elem->f);
  list_remove(&fd_elem->elem);
  free(fd_elem);
}
