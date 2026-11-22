/* Box64 on Switch: POSIX mmap family implemented by os_nx.c over libnx virtmem/heap. */
#pragma once
#include_next <sys/mman.h>
#include <stddef.h>
#include <sys/types.h>
#ifndef MAP_NORESERVE
#define MAP_NORESERVE 0x4000
#endif
#ifndef MAP_32BIT
#define MAP_32BIT 0x40
#endif
#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x100000
#endif
#ifndef MADV_DONTNEED
#define MADV_DONTNEED 4
#endif
void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset);
int munmap(void *addr, size_t length);
int mprotect(void *addr, size_t len, int prot);
int madvise(void *addr, size_t len, int advice);
int shm_open(const char *name, int oflag, mode_t mode);
int shm_unlink(const char *name);
