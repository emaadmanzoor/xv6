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

extern char end[]; // first address after kernel loaded from ELF file
uchar* const bitmap = (uchar*) end;

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  int use_lock;
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
  kmem.lastBitIdx = 0;
  memset(bitmap, -1, BITMAPSIZE);
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
  if((uint)v % PGSIZE || v < (end + BITMAPSIZE) || v2p(v) >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(v, 1, PGSIZE);

  uint p = v2p(v);
  uint bitIdx = p / PGSIZE;

  if(kmem.use_lock)
    acquire(&kmem.lock);
  bitmap_clear(bitmap, bitIdx);
  if(kmem.use_lock)
    release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
char*
kalloc(void)
{
  if(kmem.use_lock)
    acquire(&kmem.lock);

  int freeBitIdx = bitmap_ffz(bitmap, kmem.lastBitIdx, BITMAPSIZE);
  if (freeBitIdx < 0) {
    freeBitIdx = bitmap_ffz(bitmap, 0, kmem.lastBitIdx);
  }

  if (freeBitIdx < 0) {
    if(kmem.use_lock)
      release(&kmem.lock);
    return 0;
  }

  bitmap_set(bitmap, freeBitIdx);
  uint p = freeBitIdx * PGSIZE;
  char *v = (char*) p2v(p);
  kmem.lastBitIdx = freeBitIdx + 1;

  if(kmem.use_lock)
    release(&kmem.lock);

  return (char*)v;
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

    uint bitInWordIdx;
    if (wordIdx == startWordIdx)
      bitInWordIdx = startBitIdx % CHAR_BIT;
    else
      bitInWordIdx = 0;

    for (; bitInWordIdx < CHAR_BIT; bitInWordIdx++) {
      if ((uchar)(word & (1 << (CHAR_BIT - bitInWordIdx - 1))) == 0) {
        return wordIdx * CHAR_BIT + bitInWordIdx;
      }
    } 
  } 

  return -1;
}
