// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.


#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"


struct buf * get_buf(int index);

struct buf buf[NBUF];
struct buf *table[HASH_SIZE];
struct spinlock table_locks[HASH_SIZE];
char lock_names[HASH_SIZE][16];


void
binit(void)
{
  for (int i = 0; i < HASH_SIZE; i++) {
    snprintf(lock_names[i], 8, "bcache_%d", i);
    initlock(&table_locks[i], lock_names[i]);
  }

  int index = 0;
  for (int i = 0; i < NBUF; i++) {
    index = i % HASH_SIZE;
    if (!table[index]) {
      table[index] = &buf[i];
    } else {
      table[index]->next = &buf[i];
    }
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{ 
  struct buf *b;
  int index = blockno % HASH_SIZE;
  acquire(&table_locks[index]);

  for (b = table[index]; b != 0; b = b->next) {
    if (b->dev == dev && b->blockno == blockno) {
      b->refcnt++;
      release(&table_locks[index]);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // check its bucket first to find an unused one, 
  // avoids deadlocks
  for (b = table[index]; b != 0; b = b->next) {
    if(b->refcnt == 0) {
      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0;
      b->refcnt = 1;
      release(&table_locks[index]);
      acquiresleep(&b->lock);
      return b;
    }
  }

  int next_index = 0;
  for (int i = 1; i < HASH_SIZE; i++) {
    next_index = index + i;
    if (next_index >= HASH_SIZE) {
      next_index -= HASH_SIZE;
    }
    b = get_buf(next_index);
    if (b)
      break;
  }

  struct buf *cur = table[index];

  if (b) {
    b->dev = dev;
    b->blockno = blockno;
    b->valid = 0;
    b->refcnt = 1;

    table[index] = b;
    b->next = cur;

    release(&table_locks[index]);
    acquiresleep(&b->lock);
    return b;
  }

  panic("bget: no buffers");
}

struct buf *
get_buf(int index) 
{
  struct buf *prev, *cur;
  acquire(&table_locks[index]);
  cur = table[index];
  if (cur == 0) {
    release(&table_locks[index]);
    return 0;
  }
  
  prev = table[index];
  cur = prev->next;
  while (cur != 0) {
    if (cur->refcnt == 0) {
      prev->next = cur->next;
      release(&table_locks[index]);
      return cur;
    }
    prev = cur;
    cur = cur->next;
  }
  release(&table_locks[index]);
  return 0;
}


// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  int index = b->blockno % HASH_SIZE;
  acquire(&table_locks[index]);
  b->refcnt--;
  release(&table_locks[index]);
}

void
bpin(struct buf *b) {
  int index = b->blockno % HASH_SIZE;
  acquire(&table_locks[index]);
  b->refcnt++;
  release(&table_locks[index]);
}

void
bunpin(struct buf *b) {
  int index = b->blockno % HASH_SIZE;
  acquire(&table_locks[index]);
  b->refcnt--;
  release(&table_locks[index]);
}


