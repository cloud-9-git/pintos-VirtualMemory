#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include <stdbool.h> 
#include <stdlib.h>

void syscall_init (void);
bool validate_mmap_area(const void *va, size_t length); 

#endif /* userprog/syscall.h */
