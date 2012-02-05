#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

static void syscall_handler (struct intr_frame *);

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
      break;
    case SYS_WRITE: 
      printf("Write system call\n");
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
  
  printf ("system call!\n");
  thread_exit ();
}
