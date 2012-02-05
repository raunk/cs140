#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

static void syscall_handler (struct intr_frame *);
static void syscall_write(void* esp);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  printf ("system call!\n");
  printf ("esp %p\n", f->esp);
  int sys_call = *(int*)f->esp;
  printf("Sys call %d\n", sys_call);

  if(sys_call == SYS_WRITE)
  {
    syscall_write(f->esp);
  }
  thread_exit ();
}


static void
syscall_write(void* esp)
{
  printf("Syscall WRITE\n");


  printf("ESP %p\n", esp);
  printf("+4  %p\n", esp + 4);
  printf("+8  %p\n", esp + 8);
  printf("+12 %p\n", esp + 12);
 
  int fd = *(int*)(esp + 4);
  void* buffer = (void*)(esp + 8);
  unsigned length = *(unsigned*)(esp + 12);

  printf("FD %d\n", fd);
  printf("Len %u\n", length); 


  if(fd == STDOUT_FILENO)
  {
    printf("Write this console using putbuf\n");
    putbuf(buffer, length); 
  }
  
}
