#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"

static void syscall_handler (struct intr_frame *);
static void syscall_check_user_pointer (void *ptr);
static int syscall_write(void* esp);

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
  process_exit();
}



void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f) 
{
  // read sys call number from location pointed to by stack pointer
  int sys_call_number = *((int*)f->esp);
  switch(sys_call_number) {
    case SYS_EXIT:
      printf("Exit system call\n");
      // TODO: I have no idea if this is right.
      void *status = f->esp + sizeof(char*);
      f->eax = *(int*)status;
      break;
    case SYS_WRITE: 
      printf("Write system call\n");
      // TODO: I'm not sure if we can assume that the stack is setup
      //       correctly here???
      
      /* note we need to derefence these before passing since above
         pointers are still in terms of the stack pointer */
      int bytes_written = syscall_write(f->esp);
      f->eax = bytes_written;
      break;
      /*
    case SYS_HALT: case SYS_EXEC: case: SYS_CREATE:
    case SYS_REMOVE: case SYS_OPEN: case SYS_FILESIZE:
    case SYS_READ: case SYS_SEEK: case SYS_READ:
    case SYS_WAIT: case SYS_TELL: case SYS_SEEK:
    case SYS_TELL: case SYS_CLOSE:
      // TODO
      break;
      */
  }
  //thread_exit ();
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
  
  printf("Syscall WRITE\n");
 
  int fd = *(int*)(esp + sizeof(char*));
  char* buffer = *(char**)(esp + 2 * sizeof(char*));
  unsigned length = *(unsigned*)(esp + 3 * sizeof(char*));
  
  // TODO: make sure this check actually is doing something....
  syscall_check_user_pointer(buffer);

  printf("FD %d\n", fd);
  printf("Len %u\n", length); 


  if(fd == STDOUT_FILENO)
  {
    printf("Write this console using putbuf\n");
    putbuf(buffer, length); 
    return length;
  }

  printf("Done\n");
}
