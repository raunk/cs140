#include "devices/input.h"
#include "devices/shutdown.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
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

static void unmap_file_helper(struct mmap_elem* map_elem);
void unmap_file(struct hash_elem* elem, void* aux UNUSED);
void syscall_init (void);
off_t safe_file_read (struct file *file, void *buffer, off_t size);
off_t safe_file_read_at (struct file *file, void *buffer, off_t size, 
      off_t file_ofs);
off_t safe_file_length (struct file *file);
bool safe_filesys_create(const char* name, off_t initial_size);
void safe_file_seek (struct file *file, off_t new_pos);
off_t safe_file_tell (struct file *file);
void safe_file_close (struct file *file);
struct file *safe_filesys_open (const char *name);
bool safe_filesys_remove (const char *name);

#define MAX_PUTBUF_SIZE 256

/* Lock used for accessing file system code. It is not safe for multiple
   thread to access the code in the /filesys directory. */
static struct lock filesys_lock;

/* The following safe_* functions simply acquire the filesys lock before
   and release the lock after invoking their analogous functions from the
   filesys/ directory. */
off_t
safe_file_read (struct file *file, void *buffer, off_t size) 
{
  off_t bytes_read;
  lock_acquire_if_not_held(&filesys_lock);
  bytes_read = file_read(file, buffer, size);
  lock_release_if_held(&filesys_lock);
  return bytes_read;
}

off_t
safe_file_read_at (struct file *file, void *buffer, off_t size, off_t file_ofs) 
{
  off_t bytes_read;
  lock_acquire_if_not_held(&filesys_lock);
  bytes_read = file_read_at(file, buffer, size, file_ofs);
  lock_release_if_held(&filesys_lock);
  return bytes_read;
}

off_t
safe_file_write_at (struct file *file, const void *buffer, off_t size, 
                      off_t file_ofs)
{
  off_t bytes_written;
  lock_acquire_if_not_held(&filesys_lock);
  bytes_written = file_write_at(file, buffer, size, file_ofs);
  lock_release_if_held(&filesys_lock);
  return bytes_written;
}

off_t
safe_file_write (struct file *file, const void *buffer, off_t size)
{
  off_t bytes_written;
  lock_acquire_if_not_held(&filesys_lock);
  bytes_written = file_write (file, buffer, size);
  lock_release_if_held(&filesys_lock);
  return bytes_written;
}

off_t
safe_file_length (struct file *file)
{
  off_t num_bytes;
  lock_acquire_if_not_held(&filesys_lock);
  num_bytes = file_length(file);
  lock_release_if_held(&filesys_lock);
  return num_bytes;
}

bool 
safe_filesys_create(const char* name, off_t initial_size)
{
  lock_acquire_if_not_held(&filesys_lock);
  bool result = filesys_create(name, initial_size); 
  lock_release_if_held(&filesys_lock);
  return result; 
}

void
safe_file_seek (struct file *file, off_t new_pos)
{
  lock_acquire_if_not_held(&filesys_lock);
  file_seek(file, new_pos);
  lock_release_if_held(&filesys_lock);
}

off_t
safe_file_tell (struct file *file)
{
  lock_acquire_if_not_held(&filesys_lock);
  off_t pos = file_tell(file);
  lock_release_if_held(&filesys_lock);
  return pos;
}

void
safe_file_close (struct file *file)
{
  lock_acquire_if_not_held(&filesys_lock);
  file_close(file);
  lock_release_if_held(&filesys_lock);
}

struct file *
safe_filesys_open (const char *name)
{
  struct file *f;
  lock_acquire_if_not_held(&filesys_lock);
  f = filesys_open(name);
  lock_release_if_held(&filesys_lock);
  return f;
}

bool
safe_filesys_remove (const char *name)
{
  lock_acquire_if_not_held(&filesys_lock);
  bool result = filesys_remove(name);
  lock_release_if_held(&filesys_lock);
  return result;
}

/* Checks if a pointer passed by a user program is valid.
   Exits the current process if the pointer found to be invalid. */
void
syscall_check_user_pointer (void *ptr, struct intr_frame * f)
{
  // Check that it is within user memory
  if(is_user_vaddr(ptr)) {
    struct thread *t = thread_current ();
    // Check that memory has been mapped
    
    //return;
    if(pagedir_get_page (t->pagedir, ptr) != NULL) {
      return;
    }
    
    if(supp_page_bring_into_memory(ptr, false)) {
       return;
    }
  }
  // If it looks like a stack pointer, give them a new
  // stack page and return 
  if(smells_like_stack_pointer(f->esp, ptr))
    {
      install_stack_page(pg_round_down(ptr));
      return;
    }

  // Pointer is invalid if we get here
  exit_current_process(-1);
}

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  lock_init(&filesys_lock);
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

  int len = strlen(fname);

  if(len < 1 || len > MAX_FILE_NAME){
    f->eax = false; 
    return;
  }

  bool result = safe_filesys_create(fname, initial_size); 
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
  }
}


static void
unmap_file_helper(struct mmap_elem* map_elem)
{
  void* cur_addr = map_elem->vaddr;
  int write_bytes = map_elem->length;

  int cur_tid = thread_current()->tid;
  int offset = 0; 
  
  while(write_bytes > 0)
  {
    struct supp_page_entry* sp_entry = supp_page_lookup(cur_tid, cur_addr);
    int page_write_bytes = write_bytes < PGSIZE ? write_bytes : PGSIZE;
    if(pagedir_is_dirty(thread_current()->pagedir, cur_addr))
    {
      struct file* f = file_open(map_elem->inode);
      safe_file_write_at(f, cur_addr, page_write_bytes, 
                         sp_entry->off); 
    }
    // Remove supp page entry??
    if(sp_entry->status == PAGE_IN_MEM)
      {
          frame_free_user_page(cur_addr);
          pagedir_clear_page(thread_current()->pagedir, cur_addr);
      }  
    supp_remove_entry(sp_entry);

    write_bytes -= page_write_bytes;    
    offset += PGSIZE;
    cur_addr += PGSIZE;
  }
  
  hash_delete(&thread_current()->map_hash, &map_elem->elem); 
}

/* Callback function to unmap files on process exit */
void
unmap_file(struct hash_elem* elem, void* aux UNUSED)
{
  struct mmap_elem* e = hash_entry(elem, struct mmap_elem, elem);
  unmap_file_helper(e);
}

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
 * singal this thread is dying to any waiting threads using
 * sema_up. */
void
exit_current_process(int status)
{
  struct thread* cur = thread_current();

  cur->exit_status = status;

  /* Unmap any files that were not explicitly unmapped */
  handle_unmapped_files();
  
//  frame_cleanup_for_thread(cur);

  /* Allow writes for the executing file and close it */
  file_allow_write(cur->executing_file);
  safe_file_close(cur->executing_file);

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


  struct supp_page_entry* spe = supp_page_lookup(thread_current()->tid,
                                                  addr);
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

  // Save entries in the supplemental page table for this file
  while(read_bytes > 0)
  {
    int page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
    struct file* saved_file = file_reopen(fd_elem->f);
    supp_page_insert_for_on_disk(thread_current()->tid, cur_page,
            saved_file, offset, page_read_bytes, true, true);

    read_bytes -= page_read_bytes;
    cur_page += PGSIZE;
    offset += page_read_bytes;
  } 

  f->eax = map_id; 
}

static void 
syscall_munmap(struct intr_frame *f)
{
  void* esp = f->esp;
  int map_id = *(int*)get_nth_parameter(esp, 1, sizeof(int), f);
  struct mmap_elem* map_elem = thread_lookup_mmap_entry(map_id);

  // Tryin to unmap an invalid mapping
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
    syscall_check_user_pointer(buffer+PGSIZE-1, f);
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
  
  off_t bytes_written = safe_file_write(fd_elem->f, buffer, length);
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
  
  struct file *fi = safe_filesys_open (fname);
  if (!fi) {
    f->eax = -1;
    return;
  }
  f->eax = thread_add_file_descriptor_elem(fi)->fd;
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
  
  off_t bytes_read = safe_file_read(fd_elem->f, buffer, length);
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
  
  f->eax = safe_file_length(fd_elem->f);
}


/* Remove a file from the file system */
static void
syscall_remove(struct intr_frame *f)
{
  void* esp = f->esp;
  char* fname = *(char**)get_nth_parameter(esp, 1, sizeof(char*), f);
  syscall_check_user_pointer(fname, f);
  
  bool result = safe_filesys_remove(fname);
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
  
  safe_file_seek(fd_elem->f, position);
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
  
  f->eax = safe_file_tell(fd_elem->f);
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
  
  safe_file_close(fd_elem->f);
  list_remove(&fd_elem->elem);
  free(fd_elem);
}
