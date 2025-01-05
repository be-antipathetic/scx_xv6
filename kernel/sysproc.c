#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "sysinfo.h" // sysinfo结构体声明的文件

uint64
sys_exit(void)
{
  int n;
  if(argint(0, &n) < 0)
    return -1;
  exit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return fork();
}

uint64
sys_wait(void)
{
  uint64 p;
  if(argaddr(0, &p) < 0)
    return -1;
  return wait(p);
}

uint64
sys_sbrk(void)
{
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

uint64
sys_sleep(void)
{
  int n;
  uint ticks0;

  if(argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(myproc()->killed){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}


//读取用户态传入的系统调用掩码，该掩码决定了在处理系统调用时有哪些需要被追踪
uint64
sys_trace(void)
{
  int mask ; // 掩码
  if(argint(0,&mask)<0)  //将寄存器 a0 的值存入 mask.
    return -1 ;

  myproc()->scx_syscall_trace = mask ; //设置调用进程的 kama_syscall_trace 演码 mask

  return 0 ;

}

//获取内存中空间的内存、已经创建出的进程的数量
uint64
sys_sysinfo(void)
{
  struct sysinfo info; //定义一个 sysinfo 变量
  scx_freebytes(&info.freemem); //获取空闲内存
  scx_procnum(&info.nproc); //获取进程数量

  //获取用户虚拟地址
  uint64 dstaddr;
  argaddr(0,&dstaddr);

  //从内核空间拷贝数据到用户空间
  if(copyout(myproc()->pagetable,dstaddr,(char*)&info,sizeof info) <0)
  {
    return -1;
  }
  return 0 ;
}