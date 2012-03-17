#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include "filesys/file.h"

void syscall_init (void);
void exit_current_process(int status);
void handle_unmapped_files(void);

#endif /* userprog/syscall.h */
