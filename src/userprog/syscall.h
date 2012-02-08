#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include "filesys/file.h"

off_t safe_file_read (struct file *, void *, off_t);
off_t safe_file_length (struct file *);
void safe_file_seek (struct file *, off_t);
void safe_file_close (struct file *);
struct file *safe_filesys_open (const char *);
bool safe_filesys_create(const char* name, off_t initial_size);

void syscall_init (void);

#endif /* userprog/syscall.h */
