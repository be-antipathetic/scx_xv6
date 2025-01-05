#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "syscall.h"
#include "defs.h"

// Fetch the uint64 at addr from the current process.
int
fetchaddr(uint64 addr, uint64 *ip)
{
  struct proc *p = myproc();
  if(addr >= p->sz || addr+sizeof(uint64) > p->sz)
    return -1;
  if(copyin(p->pagetable, (char *)ip, addr, sizeof(*ip)) != 0)
    return -1;
  return 0;
}

// Fetch the nul-terminated string at addr from the current process.
// Returns length of string, not including nul, or -1 for error.
int
fetchstr(uint64 addr, char *buf, int max)
{
  struct proc *p = myproc();
  int err = copyinstr(p->pagetable, buf, addr, max);
  if(err < 0)
    return err;
  return strlen(buf);
}


//获取用户态传入的参数，a0-a5 寄存器在发起系统调用时用来存储命令行参数
static uint64
argraw(int n)
{
  struct proc *p = myproc();
  switch (n) {
  case 0:
    return p->trapframe->a0;
  case 1:
    return p->trapframe->a1;
  case 2:
    return p->trapframe->a2;
  case 3:
    return p->trapframe->a3;
  case 4:
    return p->trapframe->a4;
  case 5:
    return p->trapframe->a5;
  }
  panic("argraw");
  return -1;
}

// Fetch the nth 32-bit system call argument.
int
argint(int n, int *ip)
{
  *ip = argraw(n);
  return 0;
}

// Retrieve an argument as a pointer.
// Doesn't check for legality, since
// copyin/copyout will do that.
int
argaddr(int n, uint64 *ip)
{
  *ip = argraw(n);
  return 0;
}

// Fetch the nth word-sized system call argument as a null-terminated string.
// Copies into buf, at most max.
// Returns string length if OK (including nul), -1 if error.
int
argstr(int n, char *buf, int max)
{
  uint64 addr;
  if(argaddr(n, &addr) < 0)
    return -1;
  return fetchstr(addr, buf, max);
}

extern uint64 sys_chdir(void);
extern uint64 sys_close(void);
extern uint64 sys_dup(void);
extern uint64 sys_exec(void);
extern uint64 sys_exit(void);
extern uint64 sys_fork(void);
extern uint64 sys_fstat(void);
extern uint64 sys_getpid(void);
extern uint64 sys_kill(void);
extern uint64 sys_link(void);
extern uint64 sys_mkdir(void);
extern uint64 sys_mknod(void);
extern uint64 sys_open(void);
extern uint64 sys_pipe(void);
extern uint64 sys_read(void);
extern uint64 sys_sbrk(void);
extern uint64 sys_sleep(void);
extern uint64 sys_unlink(void);
extern uint64 sys_wait(void);
extern uint64 sys_write(void);
extern uint64 sys_uptime(void);
extern uint64 sys_trace(void); // 全局声明 trace 系统调用处理函数
extern uint64 sys_sysinfo(void); // 全局声明 sysinfo 系统调用处理函数

//函数指针数组 syscalls，数组中存放了相应系统调用函数的地址
static uint64 (*syscalls[])(void) = {
[SYS_fork]    sys_fork,
[SYS_exit]    sys_exit,
[SYS_wait]    sys_wait,
[SYS_pipe]    sys_pipe,
[SYS_read]    sys_read,
[SYS_kill]    sys_kill,
[SYS_exec]    sys_exec,
[SYS_fstat]   sys_fstat,
[SYS_chdir]   sys_chdir,
[SYS_dup]     sys_dup,
[SYS_getpid]  sys_getpid,
[SYS_sbrk]    sys_sbrk,
[SYS_sleep]   sys_sleep,
[SYS_uptime]  sys_uptime,
[SYS_open]    sys_open,
[SYS_write]   sys_write,
[SYS_mknod]   sys_mknod,
[SYS_unlink]  sys_unlink,
[SYS_link]    sys_link,
[SYS_mkdir]   sys_mkdir,
[SYS_close]   sys_close,
[SYS_trace]   sys_trace,  // 将系统调用号与处理函数关联
[SYS_sysinfo]    sys_sysinfo, // 将系统调用号与处理函数关联
};

//调用系统函数名称的字符串指针数组
const  char* scx_syscall_names[] = {
[SYS_fork]   "fork" ,
[SYS_exit]   "exit" ,
[SYS_wait]   "wait" ,
[SYS_pipe]   "pipe" ,
[SYS_read]   "read" ,
[SYS_kill]   "kill" ,
[SYS_exec]   "exec"  ,
[SYS_fstat]  "fstat" ,
[SYS_chdir]  "chdir" ,
[SYS_dup]    "dup"    ,
[SYS_getpid] "getpid" ,
[SYS_sbrk]   "sbrk"  ,
[SYS_sleep]  "sleep" ,
[SYS_uptime]  "uptime" ,
[SYS_open]    "open" ,
[SYS_write]   "write",
[SYS_mknod]   "mknod",
[SYS_link]    "link" ,
[SYS_mkdir]   "mkdir",
[SYS_close]   "close",
[SYS_trace]   "trace",
[SYS_sysinfo] "sysinfo",
};


void
syscall(void)
{
  int num;
  //调用 myproc 函数获取当前正在运行的进程 p 的指针
  struct proc *p = myproc();

  //从当前进程 p 的 trapframe 结构体中的 a7 寄存器获取系统调用号
  //risc-v 架构，用户态将系统调用号放置在 a7 寄存器中
  num = p->trapframe->a7;
  if(num > 0 && num < NELEM(syscalls) && syscalls[num]) {
    //满足条件则从系统调用表 syscalls 中找到对应系统调用号的函数指针执行
    //将结果存储在 a0 寄存器中
    p->trapframe->a0 = syscalls[num]();
    
    //如果当前进程启用了trace跟踪，则打印出进程信息
    //只有系统调用 trace 向掩码中赋值，否则其掩码为0
    if((p->scx_syscall_trace>>num)&1)
    {
      printf("%d : syscall %s -> %d\n",p->pid , scx_syscall_names[num], p->trapframe->a0);
    }
    
  } else {
    printf("%d %s: unknown sys call %d\n",
            p->pid, p->name, num);

    // 系统调用的返回结果存放在 a0 寄存器中
    p->trapframe->a0 = -1;
  }
}
