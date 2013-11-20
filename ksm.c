#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "spinlock.h"
#include "ksm.h"
#include "proc.h"

#ifdef LARGESEGS
// Fits in a single 4kb page
struct ksmframe {
  uint pa[1023];        // 4 bytes per address, 1023 addresses
  struct ksmframe* next;  // 4 bytes to store a ksmframe pointer
};
static void freeksmframes(uint id, uint npages);
static void allocksmframes(uint id);
#else
#define MAXPHYSPAGES MAXKSMSZ/PGSIZE
static void freephyspages(uint id, uint npages);
#endif

static char* findStartVa(const struct proc*, const uint);
static int isVaUsed(pde_t* pgdir, const char*);
static int isInvalidId(const int);
static int allocshm(pde_t*, const uint, const uint, const int);
static int deallocshm(pde_t*, const uint, const uint);
static uint minaddress(void**);

extern pde_t* walkpgdir(pde_t *pgdir, const void *va, int alloc);
extern int mappages(pde_t *pgdir, void *va, uint size, uint pa, int perm);
extern int sys_uptime(void);

struct ksminfo_kern_t {
  int key;
  int marked_for_delete;
  struct ksminfo_t info;
#ifdef LARGESEGS
  struct ksmframe* ksmframes;
#else
  char* physpages[MAXPHYSPAGES];
#endif
};

static struct ksmglobalinfo_t ksmglobalinfo = {0, 0};
static struct ksminfo_kern_t segs[MAXKSMIDS];
static struct spinlock ksmlock;

void
ksminit(void)
{
  initlock(&ksmlock, "ksm");

  struct ksminfo_kern_t ksminfo_kern_init = {
    .key = -1,
    .marked_for_delete = 0,
    .info = {0},
#ifdef LARGESEGS
    .ksmframes = 0,
#else
    .physpages = {0},
#endif
  };

  int i;
  for (i = 0; i < MAXKSMIDS; i++)
    segs[i] = ksminfo_kern_init;
}

// From states:
//  - k = -1
//  - k = key
//
// To states:
//  - k = key
//
// Returns:
//  - new id
//  - old id
//
// Errors:
//  - ENOIDS, out of keys
int
ksmget(const int key, const uint size, const int flags)
{
  int i;
  int firstFreeId = -1;
  int page_aligned_size = PGROUNDUP(size);
  
  if (page_aligned_size > MAXKSMSZ || page_aligned_size < MINKSMSZ)
   return EINVAL; 

  acquire(&ksmlock);

  for (i = 0; i < MAXKSMIDS; i++) {
    
    if (segs[i].key == key) {
      if (flags & KSM_MUSTNOTEXIST) {
        release(&ksmlock);
        return EEXISTS;
      } else {
        release(&ksmlock);
        return i+1;
      }
    }

    if (firstFreeId == -1 && segs[i].key == -1)
      firstFreeId = i;
  }

  if (firstFreeId == -1) {
    release(&ksmlock);
    return ENOIDS;
  }

  if (flags & KSM_MUSTEXIST) {
    release(&ksmlock);
    return ENOTEXIST;
  }

  struct ksminfo_kern_t ksminfo_kern_new = {
    .key = key,
    .marked_for_delete = 0,
    .info = {0},
#ifdef LARGESEGS
    .ksmframes = 0,
#else
    .physpages = {0},
#endif
  };

  segs[firstFreeId] = ksminfo_kern_new;

  struct ksminfo_t ksminfo_new = {
    .ksmsz = page_aligned_size, 
    .cpid = proc->pid,
    .mpid = proc->pid,
    .attached_nr = 0,
    .atime = 0,
    .dtime = 0,
    .ksm_global_info = 0,
  };
  segs[firstFreeId].info = ksminfo_new;

  ksmglobalinfo.total_shrg_nr++;

  release(&ksmlock);
  return firstFreeId + 1;
}

void*
ksmattach(const int id, const int rdonly)
{
  return ksmattach_proc(id, proc, 0, rdonly);
}

// key[id] = -1 --> ENOKEY
//
// key[id] = k, physpages = 0
//    --> physpages = kalloc(p1,p2, ...)
//    --> proc->segs[id] -> va = startva
//    --> proc -> map pages 
//
// key[id] = k, physpages = p1,p2,...
//    --> proc->segs[id] -> va = startva --> ENOVM if out of addresses
//    --> proc -> map pages 
//
// key[id] = k, physpages = p1,p2,..., proc->[id]->va = startva
//    --> nop
//
void*
ksmattach_proc(const int id_plus_one, struct proc* p, void* startva, const int rdonly)
{
  const int id = id_plus_one - 1;

  if (isInvalidId(id))
    return 0;

  acquire(&ksmlock);
  
  if (segs[id].key == -1) {
    release(&ksmlock);
    return 0;
  }

  int forceremap = 1;

  if (!startva) {
    startva = p->ksmsegs[id];
    forceremap = 0;
  }

  if (startva && !forceremap) {
    release(&ksmlock);
    return startva;
  }

  uint ksmsz = segs[id].info.ksmsz;
  if (!startva) {
    startva = findStartVa(p, ksmsz);
  }

  if (!startva) {
    release(&ksmlock);
    return 0;
  }

  if (!(allocshm(p->pgdir, (uint)startva, id, rdonly))) {
    release(&ksmlock);
    return 0;
  }

  p->ksmsegs[id] = startva;
  if ((uint)startva < p->ksmstart)
    p->ksmstart = (uint) startva;

  segs[id].info.attached_nr++;
  segs[id].info.mpid = p->pid;
  segs[id].info.atime = sys_uptime();
  segs[id].marked_for_delete = 0;

  release(&ksmlock);
  return startva;
}

static char*
findStartVa(const struct proc* p, const uint ksmsz)
{
  char* i = (char*) KERNBASE;
  char *j = (char*) KERNBASE - PGSIZE;

  while ((uint)j >= p->sz) {
    if (isVaUsed(p->pgdir, j)) {
        i = j;
        j -= PGSIZE;
    } else {
      if ((uint)i - (uint)j == ksmsz) return j;
      j -= PGSIZE;
    }
  }

  return 0;
}

static int
isVaUsed(pde_t* pgdir, const char* va)
{
  pte_t* pte = walkpgdir(pgdir, va, 0);
  
  if (!pte) {
    return 0;
  }

  if ((*pte & PTE_P) != 0) {
    return 1;
  }
  else
    return 0;
}

// ksmlock expected to be held
static int
allocshm(pde_t *pgdir, const uint startva, const uint id, const int rdonly)
{
  char *mem;
  uint a, i;

  uint ksmsz = segs[id].info.ksmsz;
  uint endva = startva + ksmsz - 1; 

  if(endva >= KERNBASE || endva < startva)
    panic("allocshm");

  int alloc = 0;

#ifdef LARGESEGS
  struct ksmframe* k = segs[id].ksmframes;
  if (k == 0) {
    allocksmframes(id);
    k = segs[id].ksmframes;
#else
  if (segs[id].physpages[0] == 0) {
#endif
    alloc = 1;
  }
  
  a = PGROUNDUP(startva);

  for(i = 0; a < endva; a += PGSIZE, i++){
     
#ifdef LARGESEGS
    if (i % 1023 == 0 && i != 0) {
      k = k->next;
    }
#endif

    if (!alloc) {
#ifdef LARGESEGS
      mem = (char*) k->pa[i % 1023];
#else
      mem = segs[id].physpages[i];
#endif
      if (mem == 0)
        panic("mapping phys addr 0 to ksm");
    } else {
      mem = kalloc();
      ksmglobalinfo.total_shpg_nr++;

      if(mem == 0){ 
        cprintf("allocuvm out of memory\n");
        deallocuvm(pgdir, endva, startva);
        
#ifdef LARGESEGS
        freeksmframes(id, 0);
#else
        freephyspages(id, 0);
#endif
        return 0;
      }

#ifdef LARGESEGS
      k->pa[i % 1023] = (uint) mem;
#else
      segs[id].physpages[i] = mem;
#endif
      memset(mem, 0, PGSIZE);
    }

    int perm = PTE_U|PTE_S;
    if (!rdonly)
      perm = perm | PTE_W;

    mappages(pgdir, (char*)a, PGSIZE, v2p(mem), perm);
  }

  return 1;
}

int
ksmdetach(const int id)
{
  return ksmdetach_proc(id, proc);
}

// key[id] = -1 --> ENOKEY
//
// key[id] = k, physpages = 0, proc->[id] = 0 --> ENOTAT
//
// key[id] = k, physpages = p1,p2,..., proc->[id] = 0 --> ENOTAT
//
// key[id] = k, physpages = p1,p2,..., proc->[id]->va = startva
//    --> unmap pages from proc
//
int
ksmdetach_proc(const int id_plus_one, struct proc* p)
{
  const int id = id_plus_one - 1;

  if (isInvalidId(id))
    return EINVAL;

  acquire(&ksmlock);

  if (segs[id].key == -1) {
    release(&ksmlock);
    return ENOKEY;
  }

  char* startva = p->ksmsegs[id];

  if (!startva) {
    release(&ksmlock);
    return ENOTAT;
  }

  deallocshm(p->pgdir, (uint)startva + segs[id].info.ksmsz, (uint)startva);

  p->ksmsegs[id] = 0;
  if ((uint)startva == p->ksmstart)
    p->ksmstart = minaddress(p->ksmsegs);

  segs[id].info.attached_nr--;
  segs[id].info.mpid = p->pid;
  segs[id].info.dtime = sys_uptime();

  if (segs[id].marked_for_delete) {
    // Releasing this lock will not cause a race,
    // because delete re-checks attached_nr before deleting
    release(&ksmlock);
    return ksmdelete(id+1);
  }

  release(&ksmlock);
  return 0;
}

// ksmlock expected to be held
static int
deallocshm(pde_t *pgdir, const uint oldsz, const uint newsz)
{
  pte_t *pte;
  uint a, pa; 

  if(newsz >= oldsz)
    panic("deallocshm");

  a = PGROUNDUP(newsz);
  for(; a  < oldsz; a += PGSIZE){
    pte = walkpgdir(pgdir, (char*)a, 0); 
    if(!pte)
      a += (NPTENTRIES - 1) * PGSIZE;
    else if((*pte & PTE_P) != 0){ 
      pa = PTE_ADDR(*pte);
      if(pa == 0)
        panic("kfree");
      *pte = 0;
    }   
  }

  return newsz;
}

// key[id] = -1 --> ENOKEY
//
// key[id] = k, physpages = 0, proc->[id] = startva --> ENOTDT
//
// key[id] = k, physpages = 0, proc->[id] = 0 --> key[id] = -1
//
// key[id] = k, physpages = p1,p2,..., proc->[id]->va = startva --> ENOTDT
// 
// key[id] = k, physpages = p1,p2,..., proc->[id] = startva
//    --> free physpages
//    --> key[id] = -1
//
int
ksmdelete(const int id_plus_one)
{
  const int id = id_plus_one - 1;

  if (isInvalidId(id))
    return EINVAL;

  acquire(&ksmlock);

  if (segs[id].key == -1) {
    release(&ksmlock);
    return ENOKEY;
  }

  if (segs[id].info.attached_nr > 0) {
    segs[id].marked_for_delete = 1;
    release(&ksmlock);
    return 0;
  }
  
  segs[id].key = -1;

#ifdef LARGESEGS
  freeksmframes(id, segs[id].info.ksmsz / PGSIZE);
#else
  freephyspages(id, segs[id].info.ksmsz / PGSIZE);
#endif

  ksmglobalinfo.total_shrg_nr--;

  release(&ksmlock);
  return 0;
}

#ifdef LARGESEGS
static void
allocksmframes(uint id)
{
  int i;
  uint num_ksmframes = ((segs[id].info.ksmsz / PGSIZE) / 1023) + 1;
  for (i = 0; i < num_ksmframes; i++) {
    struct ksmframe* k = (struct ksmframe*) kalloc();
    memset(k, 0, PGSIZE);
    k->next = segs[id].ksmframes;
    segs[id].ksmframes = k; 
  }
}

static void
freeksmframes(uint id, uint npages)
{
  struct ksmframe* k = segs[id].ksmframes;
  if (k == 0) return;

  int i = 0;
  while (i < npages) {
    char* physpage = (char*) k->pa[i % 1023];
    if (!physpage)
      panic("freeksmframes");
    kfree(physpage);
    ksmglobalinfo.total_shpg_nr--;
    i++;
    if (i % 1023 == 0) {
      k = k->next;
    }
  }
  
  k = segs[id].ksmframes;
  while(k) {
    struct ksmframe* temp = k->next;
    kfree((char*) k);
    k = temp;
  }

  segs[id].ksmframes = 0;
}
#else
static void
freephyspages(uint id, uint npages)
{
  int i;
  for (i = 0; i < npages; i++) {
    char* physpage = segs[id].physpages[i];
    if (physpage) {
      kfree(segs[id].physpages[i]);
      ksmglobalinfo.total_shpg_nr--;
    }
  }
  for (i = 0; i < MAXPHYSPAGES; i++) {
    segs[id].physpages[i] = 0;
  }
}
#endif

static int
isInvalidId(const int id)
{
  if (id < 0 || id >= MAXKSMIDS)
    return 1;
  return 0;
}

void
ksminfo(const int id_plus_one, struct ksminfo_t* uksminfo)
{
  const int id = id_plus_one - 1;

  struct ksmglobalinfo_t* kgtmp =
    (struct ksmglobalinfo_t*) uksminfo->ksm_global_info;

  if (id >= 0) {
    *uksminfo = segs[id].info;
    uksminfo->ksm_global_info = kgtmp;
  }

  *kgtmp = ksmglobalinfo;  
}

void
copyksmperms(int id_plus_one, void* startva,
             pde_t* parentpgdir, pde_t* childpgdir)
{
  const int id = id_plus_one - 1;

  uint ksmsz = segs[id].info.ksmsz;
  pte_t* pteparent;
  pte_t* ptechild;
  uint i;
  int flags;

  for(i = (uint) startva; i < ksmsz; i += PGSIZE){
    if((pteparent = walkpgdir(parentpgdir, (void *) i, 0)) == 0)
      panic("copyksm: pte should exist");
    if(!(*pteparent & PTE_P) || !(*pteparent & PTE_S))
      panic("copyksm: page either not present or not shared");
    if((ptechild = walkpgdir(childpgdir, (void *) i, 0)) == 0)
      panic("copyksm: pte should exist");
    if(!(*ptechild & PTE_P) || !(*ptechild & PTE_S))
      panic("copyksm: page either not present or not shared");
    
    flags = PTE_FLAGS(*pteparent);

    if (!(flags & PTE_W))
      *ptechild = *ptechild & (!PTE_W);
  }
}

static uint
minaddress(void* addresses[])
{
  uint min = KERNBASE;

  int i;
  for (i = 0; i < MAXKSMIDS; i++) {
    uint address = (uint) addresses[i];
    if (address != 0 && address < min)
      min = (uint) address;
  }

  return min;
}
