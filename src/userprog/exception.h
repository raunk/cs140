#ifndef USERPROG_EXCEPTION_H
#define USERPROG_EXCEPTION_H

/* Page fault error code bits that describe the cause of the exception.  */
#define PF_ERR_P 0x1    /* 0: not-present page. 1: access rights violation. */
#define PF_ERR_W 0x2    /* 0: read, 1: write. */
#define PF_ERR_U 0x4    /* 0: kernel, 1: user process. */

#include <stdbool.h>

void exception_init (void);
void exception_print_stats (void);

bool smells_like_stack_pointer(void* esp, void* ptr);
void install_stack_page(void* upage);

#endif /* userprog/exception.h */
