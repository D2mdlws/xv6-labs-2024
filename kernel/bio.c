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

#define NBUCKET 13

struct hashbucket {
  struct spinlock lock;   // lock to protect this bucket
  struct buf head;        // dummy head of the list
};

struct bcache {
  struct hashbucket buckets[NBUCKET];  // hash bucket
  struct buf buf[NBUF];                // the buffer cache 
};

struct bcache bcache;

static uint
hash(uint dev, uint blockno)
{
  uint key = (dev * 31 + blockno);
  key = ((key >> 16) ^ key) * 0x45d9f3b;
  key = ((key >> 16) ^ key) * 0x45d9f3b;
  key = (key >> 16) ^ key;
  return key % NBUCKET;
}

void
binit(void)
{
  struct buf *b;

  char lockname[32];

  for (int i = 0; i < NBUCKET; i++) {
    snprintf(lockname, sizeof(lockname), "bcache.bucket[%d]", i);
    initlock(&bcache.buckets[i].lock, lockname);
    bcache.buckets[i].head.prev = &bcache.buckets[i].head;
    bcache.buckets[i].head.next = &bcache.buckets[i].head;
  }

  // init all bufs
  for (int i = 0; i < NBUF; i++) {
    b = &bcache.buf[i];
    int bucket = i % NBUCKET;
    b->next = bcache.buckets[bucket].head.next;
    b->prev = &bcache.buckets[bucket].head;
    initsleeplock(&b->lock, "buffer");
    bcache.buckets[bucket].head.next->prev = b;
    bcache.buckets[bucket].head.next = b;
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;
  uint bucket = hash(dev, blockno);

  acquire(&bcache.buckets[bucket].lock);

  // Is the block already cached?
  for(b = bcache.buckets[bucket].head.next; b != &bcache.buckets[bucket].head; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.buckets[bucket].lock);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // Not cached.
  for(b = bcache.buckets[bucket].head.next; b != &bcache.buckets[bucket].head; b = b->next){
    if(b->refcnt == 0) {
      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0;
      b->refcnt = 1;
      release(&bcache.buckets[bucket].lock);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // If current bucket has no free buffer, try to find a free buffer in other buckets.
  for (int i = 0; i < NBUCKET; i++) {
    if (i == bucket) {
      continue;
    }
    acquire(&bcache.buckets[i].lock);
    for(b = bcache.buckets[i].head.next; b != &bcache.buckets[i].head; b = b->next){
      if(b->refcnt == 0) {
        b->next->prev = b->prev;
        b->prev->next = b->next;

        release(&bcache.buckets[i].lock);

        b->dev = dev;
        b->blockno = blockno;
        b->valid = 0;
        b->refcnt = 1;

        b->next = bcache.buckets[bucket].head.next;
        b->prev = &bcache.buckets[bucket].head;
        bcache.buckets[bucket].head.next->prev = b;
        bcache.buckets[bucket].head.next = b;

        release(&bcache.buckets[bucket].lock);
        acquiresleep(&b->lock);
        return b;
      }
    }
    release(&bcache.buckets[i].lock); // i_th bucket has no free buffer, search next bucket
  }
  release(&bcache.buckets[bucket].lock);
  panic("bget: no buffers");
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

  uint bucket = hash(b->dev, b->blockno);
  acquire(&bcache.buckets[bucket].lock);
  b->refcnt--;
  release(&bcache.buckets[bucket].lock);
}

void
bpin(struct buf *b)
{
  uint bucket = hash(b->dev, b->blockno);
  acquire(&bcache.buckets[bucket].lock);
  b->refcnt++;
  release(&bcache.buckets[bucket].lock);
}

void
bunpin(struct buf *b)
{
  uint bucket = hash(b->dev, b->blockno);
  acquire(&bcache.buckets[bucket].lock);
  b->refcnt--;
  release(&bcache.buckets[bucket].lock);
}


