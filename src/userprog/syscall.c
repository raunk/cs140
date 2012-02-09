#include "devices/input.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "userprog/syscall.h"
#include "userprog/pagedir.h"
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
static void syscall_create(struct intr_frame * f);
static void syscall_open(struct intr_frame *f);
static void syscall_read(struct intr_frame *f);
static void syscall_filesize(struct intr_frame *f);

static void exit_current_process(int status);
static struct file_descriptor_elem *get_file_descriptor_elem(int fd);

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
safe_file_read_at (struct file *file, void *buffer, off_t size, off_t file_ofs)
{
  off_t bytes_read;
  lock_acquire(&filesys_lock);
  bytes_read = file_read_at (file, buffer, size, file_ofs);
  lock_release(&filesys_lock);
  return bytes_read;
}

off_t
safe_file_write_at (struct file *file, const void *buffer, off_t size, off_t file_ofs)
{
  off_t bytes_written;
  lock_acquire(&filesys_lock);
  bytes_written = file_write_at (file, buffer, size, file_ofs);
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
  exit_current_process(-1);
}

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  lock_init(&filesys_lock);
}


static void*
get_nth_parameter(void* esp, int param_num)
{
  void* param = esp + param_num * sizeof(char*);
  syscall_check_user_pointer(param);
  return param;
}


/* Handle a system call for create. This gets the size
 * and file name, and checks all pointers involved. We
 * also make sure the length of the file name string is
 * in the proper range, and return false if it is not.
 * We set the return value in f->eax.
 */
static void syscall_create(struct intr_frame * f)
{
  unsigned initial_size = *(unsigned*)get_nth_parameter(f->esp, 2);
  char* fname = *(char**)get_nth_parameter(f->esp, 1); 
  syscall_check_user_pointer(fname);

  int len = strlen(fname);

  if(len < 1 || len > 14){
    f->eax = false; 
    return;
  }

  bool result = safe_filesys_create(fname, initial_size); 
  f->eax = result; 
}

static void
syscall_handler (struct intr_frame *f) 
{
  syscall_check_user_pointer (f->esp);
  
  // read sys call number from location pointed to by stack pointer
  int sys_call_number = *((int*)f->esp);
  
  if(sys_call_number == SYS_EXIT) {
    syscall_exit(f);
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
exit_current_process(int status)
{
  struct thread* cur = thread_current();
  
  cur->exit_status = status;
  
  lock_acquire(&cur->status_lock);
  cond_signal(&cur->is_dying, &cur->status_lock);
  lock_release(&cur->status_lock);
  printf("%s: exit(%d)\n", thread_name(), cur->exit_status);
  
  thread_exit();
}

static void
syscall_exit(struct intr_frame *f)
{
  void* status_ptr = f->esp + sizeof(char*);
  syscall_check_user_pointer (status_ptr);
  int status = *(int*)status_ptr;
  f->eax = status;
  
  exit_current_process(status);
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
  int fd = *(int*)get_nth_parameter(esp, 1);
  char* buffer = *(char**)get_nth_parameter(esp, 2);
  unsigned length = *(unsigned*)get_nth_parameter(esp, 3);

  
  syscall_check_user_pointer(buffer);
  if (fd == STDOUT_FILENO) {
    /* Fd 1 writes to the console */
    // TODO: address this part in the handout: 'Your code to write to the console 
    //      should write all of buffer in one call to putbuf(), at least as long 
    //      as size is not bigger than a few hundred bytes.'
    putbuf(buffer, length); 
    return;
  }
  
  struct file_descriptor_elem* fd_elem = get_file_descriptor_elem(fd);
  if (!fd_elem) {
    f->eax = 0;
    return;
  }
  
  off_t bytes_written = safe_file_write_at(fd_elem->f, buffer, length, fd_elem->pos);
  fd_elem->pos += bytes_written;
  f->eax = bytes_written;
}

static void
syscall_open(struct intr_frame *f)
{
  void* esp = f->esp;
  void* file = get_nth_parameter(esp, 1);
  syscall_check_user_pointer(file);
  char* fname = *(char**)file;
  syscall_check_user_pointer(fname);
  struct file *fi = safe_filesys_open (fname);
  if (!fi) {
    f->eax = -1;
    return;
  }
  struct thread* cur = thread_current();
  struct file_descriptor_elem* fd_elem =
      (struct file_descriptor_elem*) malloc( sizeof(struct file_descriptor_elem));
  fd_elem->fd = cur->next_fd++;
  fd_elem->f = fi;
  fd_elem->pos = 0;
  list_push_front (&cur->file_descriptors, &fd_elem->elem);
  f->eax = fd_elem->fd;
}

static struct file_descriptor_elem*
get_file_descriptor_elem(int fd)
{
  struct list* l = &thread_current()->file_descriptors;
  struct list_elem *e;
  for (e = list_begin (l); e != list_end (l); e = list_next (e)) {
    struct file_descriptor_elem *fd_elem =
        list_entry (e, struct file_descriptor_elem, elem);
    if (fd_elem->fd == fd)
      return fd_elem;
  }
  return NULL;
}

static void
syscall_read(struct intr_frame *f)
{
  void* esp = f->esp;
  int fd = *(int*)get_nth_parameter(esp, 1);
  char* buffer = *(char**)get_nth_parameter(esp, 2);
  unsigned length = *(unsigned*)get_nth_parameter(esp, 3);
  
  syscall_check_user_pointer(buffer);
  if (fd == STDIN_FILENO) {
    /* Fd 0 reads from the keyboard using input_getc() */
    buffer[0] = input_getc();
    f->eax = sizeof(uint8_t);
    return;
  }
  
  struct file_descriptor_elem* fd_elem = get_file_descriptor_elem(fd);
  if (!fd_elem) {
    f->eax = -1;
    return;
  }
  
  off_t bytes_read = safe_file_read_at(fd_elem->f, buffer, length, fd_elem->pos);
  fd_elem->pos += bytes_read;
  f->eax = bytes_read;
}

static void
syscall_filesize(struct intr_frame *f)
{
  void* esp = f->esp;
  int fd = *(int*)get_nth_parameter(esp, 1);
  
  struct file_descriptor_elem* fd_elem = get_file_descriptor_elem(fd);
  if (!fd_elem) {
    f->eax = 0;
    return;
  }
  
  f->eax = safe_file_length(fd_elem->f);
}
