// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct kmem{
  struct spinlock lock;
  struct run *freelist;
};

struct kmem kmemcpu[NCPU];

void
kinit()
{
  char lockname[32];
  for (int i = 0; i < NCPU; i++) {
    snprintf(lockname, sizeof(lockname), "kmemcpu%d", i);
    initlock(&kmemcpu[i].lock, lockname);
  }
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  int cpu = 0;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE) {
    struct run *r = (struct run*)p;
    acquire(&kmemcpu[cpu].lock);
    r->next = kmemcpu[cpu].freelist;
    kmemcpu[cpu].freelist = r;
    release(&kmemcpu[cpu].lock);
    cpu = (cpu + 1) % NCPU;
  }
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  push_off();
  int cpu = cpuid();
  acquire(&kmemcpu[cpu].lock);
  r->next = kmemcpu[cpu].freelist;
  kmemcpu[cpu].freelist = r;
  release(&kmemcpu[cpu].lock);
  pop_off();
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  push_off();
  int cpu = cpuid();
  acquire(&kmemcpu[cpu].lock);
  r = kmemcpu[cpu].freelist;
  if(r) {
    kmemcpu[cpu].freelist = r->next;
    release(&kmemcpu[cpu].lock);
    goto memclear;
  } else {
    release(&kmemcpu[cpu].lock);
    for (int i = 0; i < NCPU; i++) {
      if (i == cpu) 
        continue;
      acquire(&kmemcpu[i].lock);
      r = kmemcpu[i].freelist;
      if(r) {
        kmemcpu[i].freelist = r->next;
        release(&kmemcpu[i].lock);
        goto memclear;
      }
      release(&kmemcpu[i].lock); // i's cpu has no free pages, search next
    }
  }

memclear:
  pop_off();
  if (r) 
    memset((char*)r, 5, PGSIZE);
  return (void*)r;
}
