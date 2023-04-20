// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end, int cpu_id);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct cpu_mem{
  struct spinlock lock;
  char lock_name[8];
  struct run *freelist;
} kmem[NCPU];

void
kinit()
{ 
  for (uint cpu_id = 0; cpu_id < NCPU; cpu_id++) {
    snprintf(kmem[cpu_id].lock_name, 8, "kmem_%d", cpu_id);
    initlock(&kmem[cpu_id].lock, kmem[cpu_id].lock_name);
    freerange(end, (void*)PHYSTOP, cpu_id);
  }
  
}

void
freerange(void *pa_start, void *pa_end, int cpu_id)
{
  char *p, *start;
  start = (char*)PGROUNDUP((uint64)pa_start);
  for(p = start + cpu_id * PGSIZE; p + PGSIZE <= (char*)pa_end; p += NCPU * PGSIZE) {
    kfree(p);
  }
    
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  char *start;
  start = (char*)PGROUNDUP((uint64)end);

  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  uint cpu_id = ((uint64)pa - (uint64)start) / PGSIZE % NCPU;

  acquire(&kmem[cpu_id].lock);
  r->next = kmem[cpu_id].freelist;
  kmem[cpu_id].freelist = r;
  release(&kmem[cpu_id].lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  int cpu_id;
  push_off();
  cpu_id = cpuid();
  acquire(&kmem[cpu_id].lock);
  r = kmem[cpu_id].freelist;
  if(r) {
    kmem[cpu_id].freelist = r->next;
    release(&kmem[cpu_id].lock);
  } else {
    release(&kmem[cpu_id].lock);
    for (int i = 1; i < NCPU; i++) {
      int next_cpu_id = cpu_id + i;
      if (next_cpu_id >= NCPU) {
        next_cpu_id -= NCPU;
      }
      acquire(&kmem[next_cpu_id].lock);
      r = kmem[next_cpu_id].freelist;
      if(r) {
        kmem[next_cpu_id].freelist = r->next;
        release(&kmem[next_cpu_id].lock);
        break;
      }
      release(&kmem[next_cpu_id].lock);
    }
  }
  pop_off();
  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}
