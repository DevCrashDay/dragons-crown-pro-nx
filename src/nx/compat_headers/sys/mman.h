#pragma once
#ifndef PROT_NONE
#define PROT_NONE  0
#define PROT_READ  1
#define PROT_WRITE 2
#define PROT_EXEC  4
#endif
#ifndef MAP_FAILED
#define MAP_FAILED ((void *)-1)
#define MAP_SHARED    0x01
#define MAP_PRIVATE   0x02
#define MAP_FIXED     0x10
#define MAP_ANONYMOUS 0x20
#endif
