#ifndef MY_MMAN_HPP
#define MY_MMAN_HPP

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <my_malloc/syscall.hpp> 

#define PROT_READ       0x1
#define PROT_WRITE      0x2
#define PROT_EXEC       0x4
#define PROT_NONE       0x0
#define MAP_SHARED      0x01
#define MAP_PRIVATE     0x02
#define MAP_ANONYMOUS   0x20
#define MAP_ANON        MAP_ANONYMOUS
#define MAP_FAILED      ((void *) -1)


static inline void* mmap(void* addr, size_t length, int prot, int flags, int fd, off_t offset) {
    long ret = SYSCALL6(__NR_mmap, addr, length, prot, flags, fd, offset);
    return (void*)ret;
}

static inline int munmap(void* addr, size_t length) {
    return (int)SYSCALL2(__NR_munmap, addr, length);
}


#ifdef __cplusplus
} // extern "C"
#endif

#endif // MY_MMAN_HPP