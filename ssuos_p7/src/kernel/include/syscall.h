#ifndef     __SYSCALL_H__
#define     __SYSCALL_H__

enum SYS_LIST {
	SYS_FORK = 0,
	SYS_EXIT,
	SYS_WAIT,
	SYS_SSUREAD,
	SYS_SHUTDOWN,
	SYS_OPEN,
	SYS_READ,
	SYS_WRITE,
	SYS_LSEEK, //SYS_LSEEK추가
	SYS_NUM,
};

#include <proc/proc.h>
void init_syscall(void);

void exit(int status);
pid_t fork(proc_func func, void* aux1, void *aux2);
pid_t wait(int *status);
int ssuread(void);
void shutdown(void);
int open(const char *pathname, int flags);
int read(int fd, char *buf, size_t len);
int write(int fd, const char *buf, size_t len);
int lseek(int fd, int offset, int whence);

extern int syscall_tbl[SYS_NUM][2];
int e_opt, a_opt, re_opt, c_opt, opt_int, test;//옵션처리 및 옵션테스트 시 예외처리를 위한 변수 선언
#endif
