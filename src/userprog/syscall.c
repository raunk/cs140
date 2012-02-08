#include "filesys/file.h"
#include "filesys/filesys.h"
#include "userprog/syscall.h"
#include "userprog/pagedir.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/init.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "threads/vaddr.h"

static void syscall_handler (struct intr_frame *);
static void syscall_check_user_pointer (void *ptr);
static int syscall_write(void* esp);
static void syscall_exit(int status);

/* Lock used for accessing file system code. It is not safe for multiple
   thread to access the code in the /filesys directory. */
static struct lock filesys_lock;

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
safe_file_length (struct file *file)
{
  off_t num_bytes;
  lock_acquire(&filesys_lock);
  num_bytes = file_length(file);
  lock_release(&filesys_lock);
  return num_bytes;
}

void
safe_file_seek (struct file *file, off_t new_pos)
{
  lock_acquire(&filesys_lock);
  file_seek(file, new_pos);
  lock_release(&filesys_lock);
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

void
syscall_check_user_pointer (void *ptr)
{
  // check that it is within user memory
  if(is_user_vaddr(ptr)) {
    // TODO: is this the right thread?? or are we executing from a different process now?
    struct thread *t = thread_current ();
    // check that memory has been mapped
    if(pagedir_get_page (t->pagedir, ptr) != NULL) {
      return;
    }
  }
  
  // pointer is invalid if we get here
  // TODO: is this all we need to call?
  syscall_exit(-1);
  thread_exit();
}

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  lock_init(&filesys_lock);
}

static void
syscall_handler (struct intr_frame *f) 
{
  syscall_check_user_pointer (f->esp);
  
  // read sys call number from location pointed to by stack pointer
  int sys_call_number = *((int*)f->esp);
  
  if(sys_call_number == SYS_EXIT) {
    void* status = f->esp + sizeof(char*);
    syscall_check_user_pointer (status);
    f->eax = *(int*)status;
    syscall_exit(*(int*)status);
    thread_exit();
    
  } else if(sys_call_number == SYS_WRITE) {
    int bytes_written = syscall_write(f->esp);
    f->eax = bytes_written;
  } else if(sys_call_number == SYS_READ) {
    
  } else if(sys_call_number == SYS_HALT) {
    shutdown_power_off();
  }
  /*
  switch(sys_call_number) {
    int bytes_written;
    void* status;

    case SYS_EXIT:
      break;
    case SYS_WRITE: 
      
      break;
    case SYS_READ:
      printf("Read system call\n");
      break;
    case SYS_HALT:
      printf("Sys halt called\n");
      break; 
      
    case SYS_HALT: case SYS_EXEC: case: SYS_CREATE:
    case SYS_REMOVE: case SYS_OPEN: case SYS_FILESIZE:
    case SYS_READ: case SYS_SEEK: 
    case SYS_WAIT: case SYS_TELL: case SYS_SEEK:
    case SYS_TELL: case SYS_CLOSE:
      // TODO
      break;
      
  }
  */
  //thread_exit ();
}

static void
syscall_exit(int status)
{
  struct thread* cur = thread_current();
  
  cur->exit_status = status;
  
  lock_acquire(&cur->status_lock);
  cond_signal(&cur->is_dying, &cur->status_lock);
  lock_release(&cur->status_lock);
  printf("%s: exit(%d)\n", thread_name(), cur->exit_status);
}

/* Write size bytes from buffer to the open file fd.  Return
   the number of bytes actually written, which may be less than
   size if some bytes could not be written.
   
   If size would extend past file size, write up to EOF and 
   return number written.  Return 0 if no bytes could be written.
   
   FD 1 writes to console.
    */
static int
syscall_write(void* esp)
{
  int fd = *(int*)(esp + sizeof(char*));
  char* buffer = *(char**)(esp + 2 * sizeof(char*));
  unsigned length = *(unsigned*)(esp + 3 * sizeof(char*));
  
  syscall_check_user_pointer(buffer);
  if(fd == STDOUT_FILENO)
  {
    putbuf(buffer, length); 
    return length;
  }

  return 0;
}
