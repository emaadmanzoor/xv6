#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "spinlock.h"
#include "proc.h"
#include "semaphore.h"

struct semaphore {
  uint name;
  uint used;
  int wakeups;
};

static struct semaphore sems[MAXSEMS]; // static initializes to {0}
static struct spinlock semlock;

static int is_handle_valid(const int handle, struct proc* p);

void
seminit(void)
{
  initlock(&semlock, "sem");
}

int
sem_get(const uint name, const int value)
{
  return sem_get_proc(name, value, proc);
}

int
sem_get_proc(const uint name, const int value, struct proc* p)
{
  int i;
  int firstFreeId = -1;
  
  acquire(&semlock);

  for (i = 0; i < MAXSEMS; i++) {
    if (sems[i].name == name && sems[i].used > 0) {
      if (p->sems[i] == 0) {
        p->sems[i] = 1;
        sems[i].used++;
      } 
      release(&semlock);
      return i;
    }

    if (firstFreeId == -1 && sems[i].used == 0)
      firstFreeId = i;
  }

  if (firstFreeId == -1) {
    release(&semlock);
    return OUT_OF_SEM;
  }

  struct semaphore sem_new = {
    .name = name,
    .wakeups = value,
    .used = 1
  };
  sems[firstFreeId] = sem_new;
  p->sems[firstFreeId] = 1;  

  release(&semlock);
  return firstFreeId;
}

int
sem_delete(const int handle)
{
  return sem_delete_proc(handle, proc);
}

int
sem_delete_proc(const int handle, struct proc* p)
{
  if (!is_handle_valid(handle, p))
    return SEM_INVAL;

  acquire(&semlock);

  if (!sems[handle].used) {
    release(&semlock);
    return SEM_DOES_NOT_EXIST;
  }

  sems[handle].used--;
  p->sems[handle] = 0;
  
  wakeup(&sems[handle]);
  release(&semlock);

  return SEM_OK;
}

int
sem_wait(const int handle)
{
  if (!is_handle_valid(handle, proc))
    return SEM_INVAL;

  acquire(&semlock);

  if (!sems[handle].used) {
    release(&semlock);
    return SEM_DOES_NOT_EXIST;
  }
  
  for(;;) {
    if (sems[handle].wakeups > 0) {
      sems[handle].wakeups--;
      release(&semlock);
      return SEM_OK;
    }
    else {
      sleep(&sems[handle], &semlock);
    }
  }
}

int
sem_signal(const int handle)
{
  if (!is_handle_valid(handle, proc))
    return SEM_INVAL;

  acquire(&semlock);

  if (!sems[handle].used) {
    release(&semlock);
    return SEM_DOES_NOT_EXIST;
  }
  
  sems[handle].wakeups++;
  wakeup(&sems[handle]);

  release(&semlock);
  return SEM_OK;
}

int
sem_get_name(const int handle)
{
  return sems[handle].name;
}

int
is_handle_valid(const int handle, struct proc* p)
{
  if (handle < 0 || handle > MAXSEMS)
    return 0;
  if (p && p->sems[handle] == 0)
    return 0;
  return 1;
}
