#include <proc/sched.h>
#include <proc/proc.h>
#include <device/device.h>
#include <interrupt.h>
#include <device/kbd.h>

pid_t do_fork(proc_func func, void* aux)
{
	pid_t pid;
	struct proc_option opt;

	opt.priority = cur_process-> priority;
	pid = proc_create(func, &opt, aux);

	return pid;
}

void do_exit(int status)
{
	cur_process->exit_status = status; 	//종료 상태 저장
	proc_free();						//프로세스 자원 해제
	do_sched_on_return();				//인터럽트 종료시 스케줄링
}

pid_t do_wait(int *status)
{
	while(cur_process->child_pid != -1)
		schedule();
	//SSUMUST : schedule 제거.

	int pid = cur_process->child_pid;
	cur_process->child_pid = -1;

	extern struct process procs[];
	procs[pid].state = PROC_UNUSED;

	if(!status)
		*status = procs[pid].exit_status;

	return pid;
}

void do_shutdown(void)
{
	dev_shutdown();
	return;
}

int do_ssuread(void)
{
	return kbd_read_char();
}
