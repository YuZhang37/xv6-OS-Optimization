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

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

struct {
  struct spinlock lock;
  uint counts[NUM_PAGES];
} page_refs;


void
kinit()
{
  initlock(&kmem.lock, "kmem");
  freerange(end, (void*)PHYSTOP);
}

// call to initialize the allocator
void
kfree_init(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree_init");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree_init(p);
}

uint
get_index(uint64 pa) 
{
  if((pa % PGSIZE) != 0 || pa < (uint64)end || pa >= PHYSTOP)
    panic("get_index");
  uint index = (pa - PGROUNDUP((uint64)end)) / PGSIZE;
  return index;
}

void
increment_ref_count(uint64 pa) 
{
  uint index = get_index(pa);
  acquire(&page_refs.lock);
  if (page_refs.counts[index] == 0) {
     panic("increment_ref_count");
  }
  page_refs.counts[index]++;
  release(&page_refs.lock);
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree: invalid access");

  // check if the reference count has gone to zero after this freeing
  uint index = get_index((uint64)pa);
  acquire(&page_refs.lock);
  if (page_refs.counts[index] == 0) {
    printf("index: %d\n", index);
    panic("kfree: page_refs error");
  }
  page_refs.counts[index]--;
  if (page_refs.counts[index] > 0) {
    release(&page_refs.lock);
    return;
  }
  release(&page_refs.lock);

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r) {
    kmem.freelist = r->next;
  }
  release(&kmem.lock);
  
  if(r) {
    memset((char*)r, 5, PGSIZE); // fill with junk

    // kalloc only called in parent process, 
    // child process only maps to it, not calling kalloc, no need to lock.
    acquire(&page_refs.lock);
    uint index = get_index((uint64)r);
    page_refs.counts[index] = 1;
    release(&page_refs.lock);
  }
  return (void*)r;
}
