#include "syscall.h"
#include "defs.h"
#include "loader.h"
#include "syscall_ids.h"
#include "timer.h"
#include "trap.h"
#include "proc.h"
#include "vm.h"

uint64 sys_write(int fd, uint64 va, uint len)
{
	debugf("sys_write fd = %d va = %x, len = %d", fd, va, len);
	if (fd != STDOUT)
		return -1;
	struct proc *p = curr_proc();
	char str[MAX_STR_LEN];
	int size = copyinstr(p->pagetable, str, va, MIN(len, MAX_STR_LEN));
	debugf("size = %d", size);
	for (int i = 0; i < size; ++i) {
		console_putchar(str[i]);
	}
	return size;
}

__attribute__((noreturn)) void sys_exit(int code)
{
	exit(code);
	__builtin_unreachable();
}

uint64 sys_sched_yield()
{
	yield();
	return 0;
}

uint64 sys_gettimeofday(TimeVal *val, int _tz) // TODO: implement sys_gettimeofday in pagetable. (VA to PA)
{
	// YOUR CODE
	struct proc *p = curr_proc();
	uint64 real_second, real_usecond;
	uint64 cycle = get_cycle();
	real_second = cycle / CPU_FREQ;
	real_usecond = (cycle % CPU_FREQ) * 1000000 / CPU_FREQ;
	copyout(p->pagetable, (uint64)&val->sec, (char *)&real_second, sizeof(uint64));
	copyout(p->pagetable, (uint64)&val->usec, (char *)&real_usecond, sizeof(uint64));
	/* The code in `ch3` will leads to memory bugs*/

	// uint64 cycle = get_cycle();
	// val->sec = cycle / CPU_FREQ;
	// val->usec = (cycle % CPU_FREQ) * 1000000 / CPU_FREQ;
	return 0;
}

uint64 sys_sbrk(int n)
{
	uint64 addr;
        struct proc *p = curr_proc();
        addr = p->program_brk;
        if(growproc(n) < 0)
                return -1;
        return addr;	
}


int sys_task_info(TaskInfo *ti){
	struct proc *p = curr_proc();
	for (uint64 i = 0; i < 500; i++)
	{
		copyout(p->pagetable, (uint64)ti->syscall_times[i], (char *)curr_proc()->syscall_times[i], sizeof(unsigned int));
	}
	copyout(p->pagetable, (uint64)&ti->status, (char *)&curr_proc()->status, sizeof(TaskStatus));
	int time = (get_cycle()- curr_proc()->runtime) * 1000 / CPU_FREQ;
	copyout(p->pagetable, (uint64)&ti->time, (char *)&time, sizeof(int));
	return 0;
}
// TODO: add support for mmap and munmap syscall.
// hint: read through docstrings in vm.c. Watching CH4 video may also help.
// Note the return value and PTE flags (especially U,X,W,R)
/*
* LAB1: you may need to define sys_task_info here
*/

int mmap(void* start, unsigned long long len, int port, int flag, int fd){
	pte_t* pte;
	uint64 pa;
	uint64 a = (uint64) start;
	uint64 last = PGROUNDDOWN(a + len - 1);
	pagetable_t pg = uvmcreate();
	if(PGALIGNED(a) != 0 || len > (1 << 30)) //addr未对齐或者len超过1GB
		return -1;
	else if((port & ~0x7) != 0 || (port & 0x7) == 0)   //port不合法时报错
		return -1;
	else if(len == 0)//len=0直接返回
		return 0;
	else{
		pa = (uint64) kalloc();
		if(pa + len > PHYSTOP)   //物理内存不足时报错
			return -1;
		for (;;) {
		if ((pte = walk(pg, a, 1)) == 0)
			return -1;
		if (*pte & PTE_V) {   //存在已映射页时报错
			errorf("remap");
			return -1;
		}
		*pte = PA2PTE(pa) | port | PTE_V | PTE_U;
		if (a == last)
			break;
		a += PGSIZE;
		pa += PGSIZE;
		}//[addr, addr + len)存在已经被映射的页报错
	}
	return 0;
}

int munmap(void* start, unsigned long long len){
	uint64 a;
	pte_t* pte;
	pagetable_t pagetable = curr_proc()->pagetable;
	uint64 npages = (len -1) / PGSIZE + 1;
	for (a = (uint64)start; a < (uint64)start + npages * PGSIZE; a += PGSIZE) {
		if ((pte = walk(pagetable, a, 1)) == 0)
			continue;
		if ((*pte & PTE_V) != 0) {
			if (PTE_FLAGS(*pte) == PTE_V)
				panic("uvmunmap: not a leaf");
				uint64 pa = PTE2PA(*pte);
				kfree((void *)pa);
		}
		*pte = 0;
	}
	return 0;
}


extern char trap_page[];

void syscall()
{
	struct trapframe *trapframe = curr_proc()->trapframe;
	int id = trapframe->a7, ret;
	uint64 args[6] = { trapframe->a0, trapframe->a1, trapframe->a2,
			   trapframe->a3, trapframe->a4, trapframe->a5 };
	tracef("syscall %d args = [%x, %x, %x, %x, %x, %x]", id, args[0],
	       args[1], args[2], args[3], args[4], args[5]);
	/*
	* LAB1: you may need to update syscall counter for task info here
	*/
	curr_proc()->syscall_times[id]++;
	switch (id) {
	case SYS_write:
		ret = sys_write(args[0], args[1], args[2]);
		break;
	case SYS_exit:
		sys_exit(args[0]);
		// __builtin_unreachable();
	case SYS_sched_yield:
		ret = sys_sched_yield();
		break;
	case SYS_gettimeofday:
		ret = sys_gettimeofday((TimeVal *)args[0], args[1]);
		break;
	case SYS_sbrk:
		ret = sys_sbrk(args[0]);
		break;
	case SYS_mmap:
		ret = mmap((void *)args[0], args[1], args[2], args[3], args[4]);
		break;
	case SYS_munmap:
		ret = munmap((void *)args[0], args[1]);
		break;
	/*
	* LAB1: you may need to add SYS_taskinfo case here
	*/
	case SYS_task_info:
		ret = sys_task_info((TaskInfo *)args[0]);
		break;
	default:
		ret = -1;
		errorf("unknown syscall %d", id);
	}
	trapframe->a0 = ret;
	tracef("syscall ret %d", ret);
}
