#include "types.h"
#include "x86.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "ksm.h"

int
sys_fork(void)
{
  return fork();
}

int
sys_exit(void)
{
  exit();
  return 0;  // not reached
}

int
sys_wait(void)
{
  return wait();
}

int
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

int
sys_getpid(void)
{
  return proc->pid;
}

int
sys_sbrk(void)
{
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;
  addr = proc->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

int
sys_sleep(void)
{
  int n;
  uint ticks0;
  
  if(argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(proc->killed){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

// return how many clock tick interrupts have occurred
// since start.
int
sys_uptime(void)
{
  uint xticks;
  
  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

int
sys_pgused(void)
{
  return pgused();
}

int
sys_ksmget(void)
{
  int key;
  int size;
  int flags;

  if(argint(0, &key) < 0)
    return -1;

  if(argint(1, &size) < 0)
    return -1;

  if(argint(2, &flags) < 0)
    return -1;

  return ksmget(key, size, flags);
}

int
sys_ksmattach(void)
{
  int handle;
  int flags;

  if(argint(0, &handle) < 0)
    return -1;

  if(argint(1, &flags) < 0)
    return -1;

  return (int) ksmattach(handle, flags);
}

int
sys_ksmdetach(void)
{
  int handle;

  if(argint(0, &handle) < 0)
    return -1;

  return ksmdetach(handle);
}

int
sys_ksmdelete(void)
{
  int handle;

  if(argint(0, &handle) < 0)
    return -1;

  return ksmdelete(handle);
}

int
sys_ksminfo(void)
{
  int handle;
  struct ksminfo_t* uksminfo;

  if(argint(0, &handle) < 0)
    return -1;

  if(argptr(1, (char**) &uksminfo, sizeof(struct ksminfo_t)) < 0)
    return -1;

  ksminfo(handle, uksminfo);

  return 0;
}

int
sys_sem_get(void)
{
  int name;
  int value;
  if(argint(0, &name) < 0)
    return -1;
  if(argint(1, &value) < 0)
    return -1;
  return sem_get((uint)name, value);
}

int
sys_sem_delete(void)
{
  int handle;
  if(argint(0, &handle) < 0)
    return -1;
  return sem_delete(handle);
}

int
sys_sem_wait(void)
{
  int handle;
  if(argint(0, &handle) < 0)
    return -1;
  return sem_wait(handle);
}

int
sys_sem_signal(void)
{
  int handle;
  if(argint(0, &handle) < 0)
    return -1;
  return sem_signal(handle);
}
