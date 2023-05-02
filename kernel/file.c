//
// Support functions for system calls that involve file descriptors.
//

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "fs.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "file.h"
#include "stat.h"
#include "proc.h"
#include "fcntl.h"

struct devsw devsw[NDEV];
struct {
  struct spinlock lock;
  struct file file[NFILE];
} ftable;

void
fileinit(void)
{
  initlock(&ftable.lock, "ftable");
}

// Allocate a file structure.
struct file*
filealloc(void)
{
  struct file *f;

  acquire(&ftable.lock);
  for(f = ftable.file; f < ftable.file + NFILE; f++){
    if(f->ref == 0){
      f->ref = 1;
      release(&ftable.lock);
      return f;
    }
  }
  release(&ftable.lock);
  return 0;
}

// Increment ref count for file f.
struct file*
filedup(struct file *f)
{
  acquire(&ftable.lock);
  if(f->ref < 1)
    panic("filedup");
  f->ref++;
  release(&ftable.lock);
  return f;
}

// Close file f.  (Decrement ref count, close when reaches 0.)
void
fileclose(struct file *f)
{
  struct file ff;

  acquire(&ftable.lock);
  if(f->ref < 1)
    panic("fileclose");
  if(--f->ref > 0){
    release(&ftable.lock);
    return;
  }
  ff = *f;
  f->ref = 0;
  f->type = FD_NONE;
  release(&ftable.lock);

  if(ff.type == FD_PIPE){
    pipeclose(ff.pipe, ff.writable);
  } else if(ff.type == FD_INODE || ff.type == FD_DEVICE){
    begin_op();
    iput(ff.ip);
    end_op();
  }
}

// Get metadata about file f.
// addr is a user virtual address, pointing to a struct stat.
int
filestat(struct file *f, uint64 addr)
{
  struct proc *p = myproc();
  struct stat st;
  
  if(f->type == FD_INODE || f->type == FD_DEVICE){
    ilock(f->ip);
    stati(f->ip, &st);
    iunlock(f->ip);
    if(copyout(p->pagetable, addr, (char *)&st, sizeof(st)) < 0)
      return -1;
    return 0;
  }
  return -1;
}

// Read from file f.
// addr is a user virtual address.
int
fileread(struct file *f, uint64 addr, int n)
{
  int r = 0;

  if(f->readable == 0)
    return -1;

  if(f->type == FD_PIPE){
    r = piperead(f->pipe, addr, n);
  } else if(f->type == FD_DEVICE){
    if(f->major < 0 || f->major >= NDEV || !devsw[f->major].read)
      return -1;
    r = devsw[f->major].read(1, addr, n);
  } else if(f->type == FD_INODE){
    ilock(f->ip);
    if((r = readi(f->ip, 1, addr, f->off, n)) > 0)
      f->off += r;
    iunlock(f->ip);
  } else {
    panic("fileread");
  }

  return r;
}

// Write to file f.
// addr is a user virtual address.
int
filewrite(struct file *f, uint64 addr, int n)
{
  int r, ret = 0;

  if(f->writable == 0)
    return -1;

  if(f->type == FD_PIPE){
    ret = pipewrite(f->pipe, addr, n);
  } else if(f->type == FD_DEVICE){
    if(f->major < 0 || f->major >= NDEV || !devsw[f->major].write)
      return -1;
    ret = devsw[f->major].write(1, addr, n);
  } else if(f->type == FD_INODE){
    // write a few blocks at a time to avoid exceeding
    // the maximum log transaction size, including
    // i-node, indirect block, allocation blocks,
    // and 2 blocks of slop for non-aligned writes.
    // this really belongs lower down, since writei()
    // might be writing a device like the console.
    int max = ((MAXOPBLOCKS-1-1-2) / 2) * BSIZE;
    int i = 0;
    while(i < n){
      int n1 = n - i;
      if(n1 > max)
        n1 = max;

      begin_op();
      ilock(f->ip);
      if ((r = writei(f->ip, 1, addr + i, f->off, n1)) > 0)
        f->off += r;
      iunlock(f->ip);
      end_op();

      if(r != n1){
        // error from writei
        break;
      }
      i += r;
    }
    ret = (i == n ? n : -1);
  } else {
    panic("filewrite");
  }

  return ret;
}


// Write to file f.
// addr is a user virtual address.
int
write2file(struct file *f, uint64 addr, int n, uint off)
{
  int r, ret = 0;

  if(f->type == FD_INODE){
    // write a few blocks at a time to avoid exceeding
    // the maximum log transaction size, including
    // i-node, indirect block, allocation blocks,
    // and 2 blocks of slop for non-aligned writes.
    // this really belongs lower down, since writei()
    // might be writing a device like the console.
    int max = ((MAXOPBLOCKS-1-1-2) / 2) * BSIZE;
    int i = 0;
    while(i < n){
      int n1 = n - i;
      if(n1 > max)
        n1 = max;

      begin_op();
      ilock(f->ip);
      if ((r = writei(f->ip, 1, addr + i, off, n1)) > 0)
        off += r;
      iunlock(f->ip);
      end_op();

      if(r != n1){
        // error from writei
        break;
      }
      i += r;
    }
    // ret = (i == n ? n : -1);
    ret = i;
  }
  return ret;
}


uint64
filemmap(struct file *f, int len, int prot, int flags)
{
  struct proc *p = myproc();
  uint64 addr;
  acquire(&p->lock);
  if (p->vma_count == MAXMMAP || p->sz + len >= MAXVA) {
    release(&p->lock);
    printf("filemmap failed\n");
    return -1;
  }
  int i = 0;
  for (i = 0; i < MAXMMAP; i++) {
    if (p->vmas[i].alloc == 0) {
      p->vmasp[p->vma_count] = &p->vmas[i];
      p->vmas[i].alloc = 1;
      break;
    }
  }
  addr = p->sz;
  p->vmasp[p->vma_count]->addr = addr;
  p->sz += len;
  p->vmasp[p->vma_count]->f = f;
  filedup(f);
  p->vmasp[p->vma_count]->len = len;
  p->vmasp[p->vma_count]->prot = prot;
  p->vmasp[p->vma_count]->flags = flags;
  p->vma_count++;
  release(&p->lock);
  return addr;
}

struct vma * 
get_vma(uint64 va) {
  struct vma *vp = 0;
  struct proc *p = myproc();
  acquire(&p->lock);
  for (int i = 0; i < p->vma_count; i++) {
    if (va >= p->vmasp[i]->addr && va < p->vmasp[i]->addr + p->vmasp[i]->len) {
      vp = p->vmasp[i];
      break;
    }
  }
  release(&p->lock);
  return vp;
}

int
remove_vma(struct vma *v)
{
  struct proc *p = myproc();

  int i = 0;

  fileclose(v->f);
  acquire(&p->lock);
  v->alloc = 0;
  v->f = 0;
  for (i = 0; i < p->vma_count; i++) {
    if (v == p->vmasp[i]) {
      break;
    }
  }
  if (i == p->vma_count) {
    release(&p->lock);
    return -1;
  }

  p->vmasp[i] = p->vmasp[p->vma_count - 1];
  p->vmasp[p->vma_count - 1] = 0;
  p->vma_count--;
  release(&p->lock);
  return 0;
}

uint64
fileunmap(uint64 addr, int len)
{
  struct proc *p = myproc();
  struct vma *v = get_vma(addr);
  if (v == 0) {
    printf("no vma is found.\n");
    return -1;
  }
  if (addr % PGSIZE != 0 || len % PGSIZE != 0) {
    printf("invalid input. addr: %p, len: %d\n", addr, len);
    return -1;
  }
  if (addr + len > v->addr + v->len) {
    printf("adjust length.\n");
    len = v->addr + v->len - addr;
  }

  if (v->flags & MAP_SHARED) {
    uint64 cur_addr = 0;
    uint off = 0;
    int count = 0;
    for (int i = 0; i < len / PGSIZE; i++) {
      cur_addr = addr + i * PGSIZE;
      off = cur_addr - v->addr;
      pte_t *pte = walk(p->pagetable, cur_addr, 0);
      if(pte == 0 || (*pte & PTE_V) == 0 || (*pte & PTE_U) == 0) {
        continue;
      }
      if ((*pte & PTE_D) == 0) {
        printf("page not dirty: %p\n", cur_addr);
        continue;
      }
      printf("page is dirty: %p\n", cur_addr);
      count = write2file(v->f, cur_addr, PGSIZE, off);
      if (count != PGSIZE) {
        printf("write2file failed %d != %d\n", count, PGSIZE);
      }
    }
  }
 
  uvmunmap(p->pagetable, addr, len / PGSIZE, 1);

  if (addr == v->addr && len == v->len) {
    int rc = remove_vma(v);
    if (rc) {
      printf("remove_vma failed. rc = %d\n", rc);
    }
  } else {
    if (addr == v->addr) {
      // no need to lock, alloc prevents others from accessing this vma
      v->addr = v->addr + len;
    }
    v->len = v->len - len;
  }
  return 0;
}