#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H
#include <stdint.h>  // uint64_t
#include <list.h>    // struct list_elem

void syscall_init (void);

struct FD_elem{ //FD_list 안에 들어갈 정보들
   uint64_t FID;
   struct file *file;
   struct list_elem elem;
};

#endif /* userprog/syscall.h */
