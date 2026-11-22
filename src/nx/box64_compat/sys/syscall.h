/* Box64 on Switch: no Linux syscalls; numbers only so code compiles. */
#pragma once
#define SYS_gettid 178
#define __NR_gettid SYS_gettid
long syscall(long number, ...); /* os_nx.c: only gettid-style numbers handled */
#define SYS_brk 214
#define __NR_brk SYS_brk
