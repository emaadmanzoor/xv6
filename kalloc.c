// Physical memory allocator, intended to allocate
// memory for user processes, kernel stacks, page table pages,
// and pipe buffers. Allocates 4096-byte pages.

#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "spinlock.h"
#include "limits.h"

void freerange(void *vstart, void *vend);
void bitmap_set(uchar* const, uint);
void bitmap_clear(uchar* const, uint);
int bitmap_ffz(uchar* const, uint, uint);
uint log2(uint);

extern char end[]; // first address after kernel loaded from ELF file

struct run {
  struct run *next;
};

/*struct {
  struct spinlock lock;
  int use_lock;
  struct run *freelist;
} kmem;*/

struct {
  struct spinlock lock;
  int use_lock;
  uchar* bitmap;
  uint lastBitIdx;
} kmem;

// Initialization happens in two phases.
// 1. main() calls kinit1() while still using entrypgdir to place just
// the pages mapped by entrypgdir on free list.
// 2. main() calls kinit2() with the rest of the physical pages
// after installing a full page table that maps them on all cores.
void
kinit1(void *vstart, void *vend)
{
  kmem.bitmap = (uchar*) end;
  kmem.lastBitIdx = 0;
  memset(kmem.bitmap, -1, BITMAPSIZE);
  initlock(&kmem.lock, "kmem");
  kmem.use_lock = 0;
  freerange(vstart, vend);
}

void
kinit2(void *vstart, void *vend)
{
  freerange(vstart, vend);
  kmem.use_lock = 1;
}

void
freerange(void *vstart, void *vend)
{
  char *p;
  p = (char*)PGROUNDUP((uint)vstart);
  for(; p + PGSIZE <= (char*)vend; p += PGSIZE)
    kfree(p);
}

//PAGEBREAK: 21
// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(char *v)
{
  //struct run *r;

  if((uint)v % PGSIZE || v < (end + BITMAPSIZE) || v2p(v) >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(v, 1, PGSIZE);

  uint p = v2p(v);
  uint bitIdx = p / PGSIZE;

  //cprintf("Free v: %x p: %x\n", v, (char*)p);
  if(kmem.use_lock)
    acquire(&kmem.lock);
  //cprintf("Before Free: Bitmap[%d] = %x\n", bitIdx / 8, kmem.bitmap[bitIdx/8]);
  bitmap_clear(kmem.bitmap, bitIdx);
  //cprintf("After Free: Bitmap[%d] = %x\n", bitIdx / 8, kmem.bitmap[bitIdx/8]);
  if(kmem.use_lock)
    release(&kmem.lock);

  /*if(kmem.use_lock)
    acquire(&kmem.lock);
  r = (struct run*)v;
  r->next = kmem.freelist;
  kmem.freelist = r;
  if(kmem.use_lock)
    release(&kmem.lock);*/
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
char*
kalloc(void)
{
  //struct run *r;

  if(kmem.use_lock)
    acquire(&kmem.lock);

  int freeBitIdx = bitmap_ffz(kmem.bitmap, kmem.lastBitIdx, BITMAPSIZE);
  //cprintf("Alloc got freeBitIdx from last=%d to BITMAPSIZE as %d\n", kmem.lastBitIdx, freeBitIdx);
  if (freeBitIdx < 0) {
    freeBitIdx = bitmap_ffz(kmem.bitmap, 0, kmem.lastBitIdx);
    //cprintf("Alloc got freeBitIdx from 0 to last-1=%d as %d\n", kmem.lastBitIdx-1, freeBitIdx);
  }

  if (freeBitIdx < 0) {
    cprintf("Alloc got freeBitIdx last=%d got=%d\n", kmem.lastBitIdx-1, freeBitIdx);
    cprintf("No memory, returning\n");
    if(kmem.use_lock)
      release(&kmem.lock);
    return 0;
  }

  bitmap_set(kmem.bitmap, freeBitIdx);
  uint p = freeBitIdx * PGSIZE;
  char *v = (char*) p2v(p);
  kmem.lastBitIdx = freeBitIdx + 1;
  
  if(kmem.use_lock)
    release(&kmem.lock);
  //cprintf("Alloc v: %x p: %x\n", v, (char*)p);
  return (char*)v;

  /*if(kmem.use_lock)
    acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
    kmem.freelist = r->next;
  if(kmem.use_lock)
    release(&kmem.lock);
  return (char*)r;*/
}

void
bitmap_set(uchar* const bitmap, uint bitIdx)
{
  bitmap[bitIdx / CHAR_BIT] |= (1 << (CHAR_BIT - (bitIdx % CHAR_BIT) - 1));
}

void
bitmap_clear(uchar* const bitmap, uint bitIdx)
{
  bitmap[bitIdx / CHAR_BIT] &= ~(1 << (CHAR_BIT - (bitIdx % CHAR_BIT) - 1));
}

int
bitmap_ffz(uchar* const bitmap, uint startBitIdx, uint endBitIdx)
{
  uint startWordIdx = startBitIdx / CHAR_BIT;
  uint endWordIdx = endBitIdx / CHAR_BIT;
  
  uint wordIdx;
  for (wordIdx = startWordIdx; wordIdx < endWordIdx; wordIdx++) {
    uchar word = bitmap[wordIdx];
    if ((uchar)~word == 0) continue;

    uint bitIdx;
    if (wordIdx == startWordIdx)
      bitIdx = startBitIdx;
    else
      bitIdx = 0
    for (; bitIdx < CHAR_BIT; bitIdx++) {
      if ((uchar)(word & (1 << (CHAR_BIT - bitIdx - 1))) == 0) {
        return wordIdx * CHAR_BIT + bitIdx;
      }
    } 
  } 

  return -1;
}

uint
log2(uint x)
{
  uint log = 0;
  while (x >>= 1)
    log++;
  return log;
}
