#include "devices/input.h"
#include "devices/shutdown.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "userprog/syscall.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/init.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include <string.h>

static void syscall_handler (struct intr_frame *);
static void syscall_check_user_pointer (void *ptr);
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

off_t safe_file_read (struct file *file, void *buffer, off_t size);
off_t safe_file_write (struct file *file, const void *buffer, off_t size);
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
  lock_acquire(&filesys_lock);
  bytes_read = file_read(file, buffer, size);
  lock_release(&filesys_lock);
  return bytes_read;
}

off_t
safe_file_write (struct file *file, const void *buffer, off_t size)
{
  off_t bytes_written;
  lock_acquire(&filesys_lock);
  bytes_written = file_write (file, buffer, size);
  lock_release(&filesys_lock);
  return bytes_written;
}

off_t
safe_file_length (struct file *file)
{
  off_t num_bytes;
  lock_acquire(&filesys_lock);
  num_bytes = file_length(file);
  lock_release(&filesys_lock);
  return num_bytes;
}

bool 
safe_filesys_create(const char* name, off_t initial_size)
{
  lock_acquire(&filesys_lock);
  bool result = filesys_create(name, initial_size); 
  lock_release(&filesys_lock);
  return result; 
}

void
safe_file_seek (struct file *file, off_t new_pos)
{
  lock_acquire(&filesys_lock);
  file_seek(file, new_pos);
  lock_release(&filesys_lock);
}

off_t
safe_file_tell (struct file *file)
{
  lock_acquire(&filesys_lock);
  off_t pos = file_tell(file);
  lock_release(&filesys_lock);
  return pos;
}

void
safe_file_close (struct file *file)
{
  lock_acquire(&filesys_lock);
  file_close(file);
  lock_release(&filesys_lock);
}

struct file *
safe_filesys_open (const char *name)
{
  struct file *f;
  lock_acquire(&filesys_lock);
  f = filesys_open(name);
  lock_release(&filesys_lock);
  return f;
}

bool
safe_filesys_remove (const char *name)
{
  lock_acquire(&filesys_lock);
  bool result = filesys_remove(name);
  lock_release(&filesys_lock);
  return result;
}

/* Checks if a pointer passed by a user program is valid.
   Exits the current process if the pointer found to be invalid. */
void
syscall_check_user_pointer (void *ptr)
{
  // Check that it is within user memory
  if(is_user_vaddr(ptr)) {
    struct thread *t = thread_current ();
    // Check that memory has been mapped
    if(pagedir_get_page (t->pagedir, ptr) != NULL) {
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
  lock_init(&filesys_lock);
}

/* Returns the nth parameter to the system call given the stack pointer. */
static void*
get_nth_parameter(void* esp, int param_num, int datasize)
{
  void* param = esp + param_num * sizeof(char*);
  syscall_check_user_pointer(param);
  syscall_check_user_pointer(param + datasize - 1);
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
  unsigned initial_size = *(unsigned*)get_nth_parameter(f->esp, 2, sizeof(unsigned));
  char* fname = *(char**)get_nth_parameter(f->esp, 1, sizeof(char*)); 
  syscall_check_user_pointer(fname);

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
  syscall_check_user_pointer (f->esp);
  syscall_check_user_pointer (f->esp+sizeof(int*)-1);
  
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
  }
}

/* Exit the current process with status STATUS. Set the exit
 * status of this thead. Also clean up file resources and 
 * singal this thread is dying to any waiting threads using
 * sema_up. */
void
exit_current_process(int status)
{
  printf("EXIT CUR PROCESS CALLED\n");
  
  struct thread* cur = thread_current();
  
  cur->exit_status = status;

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
  int status = *(int*)get_nth_parameter(esp, 1, sizeof(int));
  
  f->eax = status;
  exit_current_process(status);
}

/* Exec system call. Take in the command line arguments
 * and make a call to process_execute. */
static void
syscall_exec(struct intr_frame *f)
{
  void* esp = f->esp;
  char* cmd_line = *(char**)get_nth_parameter(esp, 1, sizeof(char*));
  syscall_check_user_pointer (cmd_line);

  f->eax = process_execute(cmd_line);
}

/* Wait system call. Make a call to process wait */
static void
syscall_wait(struct intr_frame *f)
{
  void* esp = f->esp;
  int pid = *(int*)get_nth_parameter(esp, 1, sizeof(int));
  
  f->eax = process_wait(pid);
}

/* Check the bounds of a buffer that could possibly extend multiple
   pages.  We need to check that each page in the block of pages 
   overlapped by the buffer is valid. */
static void
syscall_check_buffer_bounds(char* buffer, unsigned length)
{
  unsigned length_check = length;
  while(length_check >= PGSIZE) {
    syscall_check_user_pointer(buffer+PGSIZE-1);
    length_check -= PGSIZE;
  }
  syscall_check_user_pointer(buffer+length_check-1);
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
  int fd = *(int*)get_nth_parameter(esp, 1, sizeof(int));
  char* buffer = *(char**)get_nth_parameter(esp, 2, sizeof(char*));
  unsigned length = *(unsigned*)get_nth_parameter(esp, 3, sizeof(unsigned));
  
  /* Make sure beginning and end of buffer from user are valid addresses. */
  syscall_check_user_pointer(buffer);
  syscall_check_buffer_bounds(buffer, length);
  
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
  void* file = get_nth_parameter(esp, 1, sizeof(char*));
  char* fname = *(char**)file;
  syscall_check_user_pointer(fname);
  
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
  int fd = *(int*)get_nth_parameter(esp, 1, sizeof(int));
  char* buffer = *(char**)get_nth_parameter(esp, 2, sizeof(char*));
  unsigned length = *(unsigned*)get_nth_parameter(esp, 3, sizeof(unsigned));
  
  /* Make sure beginning and end of buffer from user are valid addresses. */
  syscall_check_user_pointer(buffer);
  syscall_check_buffer_bounds(buffer, length);
  
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
  int fd = *(int*)get_nth_parameter(esp, 1, sizeof(int));
  
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
  char* fname = *(char**)get_nth_parameter(esp, 1, sizeof(char*));
  syscall_check_user_pointer(fname);
  
  bool result = safe_filesys_remove(fname);
  f->eax = result;
}

/* Seek to a specific point in a file given its file descriptor */
static void
syscall_seek(struct intr_frame *f)
{
  void* esp = f->esp;
  int fd = *(int*)get_nth_parameter(esp, 1, sizeof(int));
  unsigned position = *(unsigned*)get_nth_parameter(esp, 2, sizeof(unsigned));
  
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
  int fd = *(int*)get_nth_parameter(esp, 1, sizeof(int));
  
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
  int fd = *(int*)get_nth_parameter(esp, 1, sizeof(int));
  
  struct file_descriptor_elem* fd_elem = thread_get_file_descriptor_elem(fd);
  if (!fd_elem) {
    return;
  }
  
  safe_file_close(fd_elem->f);
  list_remove(&fd_elem->elem);
  free(fd_elem);
}
