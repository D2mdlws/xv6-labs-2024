#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "fs.h"
#include "sleeplock.h"
#include "file.h"
#include "net.h"

// xv6's ethernet and IP addresses
static uint8 local_mac[ETHADDR_LEN] = { 0x52, 0x54, 0x00, 0x12, 0x34, 0x56 };
static uint32 local_ip = MAKE_IP_ADDR(10, 0, 2, 15);

// qemu host's ethernet address.
static uint8 host_mac[ETHADDR_LEN] = { 0x52, 0x55, 0x0a, 0x00, 0x02, 0x02 };

static struct spinlock netlock;
static struct spinlock udptablelock;

#define PORT_MAX 65536
#define WAIT_MAX 17

struct queue{
  int head;
  int tail;
  char *buf[WAIT_MAX];
  int size[WAIT_MAX];
  uint32 src_ip[WAIT_MAX];
  uint16 src_port[WAIT_MAX];
};

struct binded_port {
  struct spinlock queuelock;
  struct queue q;
  int binded;
};

static struct binded_port udptable[PORT_MAX];

void
netinit(void)
{
  initlock(&netlock, "netlock");
  initlock(&udptablelock, "udptablelock");
}

int
enqueue(struct queue *q, char *buf, int size, uint32 src_ip, uint16 src_port)
{
  if ((q->tail + 1) % WAIT_MAX == q->head)
    return -1;
  q->buf[q->tail] = buf;
  q->size[q->tail] = size;
  q->src_ip[q->tail] = src_ip;
  q->src_port[q->tail] = src_port;
  q->tail = (q->tail + 1) % WAIT_MAX;
  return 0;
}

void
dequeue(struct queue *q)
{
  if (q->head == q->tail)
    return ;
  q->head = (q->head + 1) % WAIT_MAX;
}

int
isempty(struct queue *q)
{
  if (q->head == q->tail)
    return 1;
  else return 0;
}

int 
queue_size(struct queue *q)
{
  return (q->tail - q->head + WAIT_MAX) % WAIT_MAX;
}

//
// bind(int port)
// prepare to receive UDP packets address to the port,
// i.e. allocate any queues &c needed.
//
uint64
sys_bind(void)
{
  //
  // Your code here.
  //
  int port;
  argint(0, &port);

  // port = ntohs(port);
  printf("bind: port = %d\n", port);

  if (port < 0 || port >= PORT_MAX) {
    printf("bind: port out of range\n");
    return -1;
  }

  acquire(&udptablelock);
  if (udptable[port].binded == 1) {
    release(&udptablelock);
    return -1;
  }

  udptable[port].binded = 1;
  initlock(&udptable[port].queuelock, "queuelock");
  udptable[port].q.head = 0;
  udptable[port].q.tail = 0;
  udptable[port].binded = 1;  

  release(&udptablelock);
  return 0;
}

//
// unbind(int port)
// release any resources previously created by bind(port);
// from now on UDP packets addressed to port should be dropped.
//
uint64
sys_unbind(void)
{
  //
  // Optional: Your code here.
  //
  int port;
  argint(0, &port);

  if (port < 0 || port >= PORT_MAX) {
    printf("unbind: port out of range\n");
    return -1;
  }

  acquire(&udptablelock);
  if (udptable[port].binded == 0) {
    printf("unbind: port not binded\n");
    release(&udptablelock);
    return -1;
  }

  udptable[port].binded = 0;
  udptable[port].q.head = 0;
  udptable[port].q.tail = 0;
  release(&udptablelock);
  
  return 0;
}

//
// recv(int dport, int *src, short *sport, char *buf, int maxlen)
// if there's a received UDP packet already queued that was
// addressed to dport, then return it.
// otherwise wait for such a packet.
//
// sets *src to the IP source address.
// sets *sport to the UDP source port.
// copies up to maxlen bytes of UDP payload to buf.
// returns the number of bytes copied,
// and -1 if there was an error.
//
// dport, *src, and *sport are host byte order.
// bind(dport) must previously have been called.
//
uint64
sys_recv(void)
{
  //
  // Your code here.
  //

  int dport;
  uint64 srcaddr;
  uint64 sportaddr;
  uint64 bufaddr;
  int maxlen;

  struct proc* p = myproc();

  argint(0, &dport);
  argaddr(1, &srcaddr);
  argaddr(2, &sportaddr);
  argaddr(3, &bufaddr);
  argint(4, &maxlen);

  
  printf("recv: dport = %d\n", dport);

  if (dport < 0 || dport >= PORT_MAX) {
    printf("recv: port out of range\n");
    return -1;
  }

  acquire(&udptablelock);
  if (udptable[dport].binded == 0) {
    printf("recv: port not binded\n");
    release(&udptablelock);
    return -1;
  }
  
  struct binded_port *bp = &udptable[dport];

  acquire(&bp->queuelock);
  release (&udptablelock);

  while (isempty(&bp->q)) {
    if (p->killed) {
      release(&bp->queuelock);
      return -1;
    }
    sleep(&bp->q, &bp->queuelock);
  }

  int len = 0;

  if (srcaddr != 0) {
    if (copyout(p->pagetable, srcaddr, (char *)&bp->q.src_ip[bp->q.head], sizeof(bp->q.src_ip[bp->q.head])) < 0) {
      goto bad;
    }
  }

  if (sportaddr != 0) {
    if (copyout(p->pagetable, sportaddr, (char *)&bp->q.src_port[bp->q.head], sizeof(bp->q.src_port[bp->q.head])) < 0) {
      goto bad;
    }
  }

  len = bp->q.size[bp->q.head];
  if (len > maxlen)
    len = maxlen;

  if (copyout(p->pagetable, bufaddr, bp->q.buf[bp->q.head], len) < 0) {
    goto bad;
  }

bad:
  kfree(bp->q.buf[bp->q.head]);
  dequeue(&bp->q);
  release(&bp->queuelock);
  return len;
}

// This code is lifted from FreeBSD's ping.c, and is copyright by the Regents
// of the University of California.
static unsigned short
in_cksum(const unsigned char *addr, int len)
{
  int nleft = len;
  const unsigned short *w = (const unsigned short *)addr;
  unsigned int sum = 0;
  unsigned short answer = 0;

  /*
   * Our algorithm is simple, using a 32 bit accumulator (sum), we add
   * sequential 16 bit words to it, and at the end, fold back all the
   * carry bits from the top 16 bits into the lower 16 bits.
   */
  while (nleft > 1)  {
    sum += *w++;
    nleft -= 2;
  }

  /* mop up an odd byte, if necessary */
  if (nleft == 1) {
    *(unsigned char *)(&answer) = *(const unsigned char *)w;
    sum += answer;
  }

  /* add back carry outs from top 16 bits to low 16 bits */
  sum = (sum & 0xffff) + (sum >> 16);
  sum += (sum >> 16);
  /* guaranteed now that the lower 16 bits of sum are correct */

  answer = ~sum; /* truncate to 16 bits */
  return answer;
}

//
// send(int sport, int dst, int dport, char *buf, int len)
//
uint64
sys_send(void)
{
  struct proc *p = myproc();
  int sport;
  int dst;
  int dport;
  uint64 bufaddr;
  int len;

  argint(0, &sport);
  argint(1, &dst);
  argint(2, &dport);
  argaddr(3, &bufaddr);
  argint(4, &len);

  int total = len + sizeof(struct eth) + sizeof(struct ip) + sizeof(struct udp);
  if(total > PGSIZE)
    return -1;

  char *buf = kalloc();
  if(buf == 0){
    printf("sys_send: kalloc failed\n");
    return -1;
  }
  memset(buf, 0, PGSIZE);

  struct eth *eth = (struct eth *) buf;
  memmove(eth->dhost, host_mac, ETHADDR_LEN);
  memmove(eth->shost, local_mac, ETHADDR_LEN);
  eth->type = htons(ETHTYPE_IP);

  struct ip *ip = (struct ip *)(eth + 1);
  ip->ip_vhl = 0x45; // version 4, header length 4*5
  ip->ip_tos = 0;
  ip->ip_len = htons(sizeof(struct ip) + sizeof(struct udp) + len);
  ip->ip_id = 0;
  ip->ip_off = 0;
  ip->ip_ttl = 100;
  ip->ip_p = IPPROTO_UDP;
  ip->ip_src = htonl(local_ip);
  ip->ip_dst = htonl(dst);
  ip->ip_sum = in_cksum((unsigned char *)ip, sizeof(*ip));

  struct udp *udp = (struct udp *)(ip + 1);
  udp->sport = htons(sport);
  udp->dport = htons(dport);
  udp->ulen = htons(len + sizeof(struct udp));

  char *payload = (char *)(udp + 1);
  if(copyin(p->pagetable, payload, bufaddr, len) < 0){
    kfree(buf);
    printf("send: copyin failed\n");
    return -1;
  }

  e1000_transmit(buf, total);

  return 0;
}

void
ip_rx(char *buf, int len)
{
  // don't delete this printf; make grade depends on it.
  static int seen_ip = 0;
  if(seen_ip == 0)
    printf("ip_rx: received an IP packet\n");
  seen_ip = 1;

  //
  // Your code here.
  //
  struct eth *ineth = (struct eth *) buf;
  struct ip *inip = (struct ip *)(ineth + 1);

  // if (inip->ip_p != IPPROTO_UDP)
  //   return;
  if (len < sizeof(struct eth) + sizeof(struct ip) + sizeof(struct udp)) {
    printf("ip_rx: packet too short for udp\n");
    return;
  }
  
  struct udp *inudp = (struct udp *)(inip + 1);

  int udp_len = ntohs(inudp->ulen);

  if (udp_len < sizeof(struct udp) ||
      len < sizeof(struct eth) + sizeof(struct ip) + udp_len)
    return;

  uint16 dport = ntohs(inudp->dport);
  printf("ip_rx: dport after ntohs = %d\n", dport);

  acquire(&udptablelock);
  if (udptable[dport].binded == 0) {
    printf("ip_rx: port not binded\n");
    kfree(buf);
    release(&udptablelock);
    return;
  }
  struct binded_port *bp = &udptable[dport];
  acquire (&bp->queuelock);
  if (queue_size(&bp->q) == WAIT_MAX - 1) {
    printf("ip_rx: queue full\n");
    release(&bp->queuelock);
    kfree(buf);
    release(&udptablelock);
    return;
  }

  release(&udptablelock);

  // copy the payload to a new buffer
  int payload_len = udp_len - sizeof(struct udp);
  char *payload = (char *)(inudp + 1);

  char *payload_buf = kalloc();
  if (payload_buf == 0) {
    printf("ip_rx: kalloc failed\n");
    goto bad;
  }

  memmove(payload_buf, payload, payload_len);
  if (enqueue(&bp->q, payload_buf, payload_len, ntohl(inip->ip_src), ntohs(inudp->sport)) < 0) {
    printf("ip_rx: queue full\n");
    kfree(payload_buf);
    goto bad;
  }
  wakeup(&bp->q);

bad:
  release(&bp->queuelock);
  kfree(buf); // free the original packet
}

//
// send an ARP reply packet to tell qemu to map
// xv6's ip address to its ethernet address.
// this is the bare minimum needed to persuade
// qemu to send IP packets to xv6; the real ARP
// protocol is more complex.
//
void
arp_rx(char *inbuf)
{
  static int seen_arp = 0;

  if(seen_arp){
    kfree(inbuf);
    return;
  }
  printf("arp_rx: received an ARP packet\n");
  seen_arp = 1;

  struct eth *ineth = (struct eth *) inbuf;
  struct arp *inarp = (struct arp *) (ineth + 1);

  char *buf = kalloc();
  if(buf == 0)
    panic("send_arp_reply");
  
  struct eth *eth = (struct eth *) buf;
  memmove(eth->dhost, ineth->shost, ETHADDR_LEN); // ethernet destination = query source
  memmove(eth->shost, local_mac, ETHADDR_LEN); // ethernet source = xv6's ethernet address
  eth->type = htons(ETHTYPE_ARP);

  struct arp *arp = (struct arp *)(eth + 1);
  arp->hrd = htons(ARP_HRD_ETHER);
  arp->pro = htons(ETHTYPE_IP);
  arp->hln = ETHADDR_LEN;
  arp->pln = sizeof(uint32);
  arp->op = htons(ARP_OP_REPLY);

  memmove(arp->sha, local_mac, ETHADDR_LEN);
  arp->sip = htonl(local_ip);
  memmove(arp->tha, ineth->shost, ETHADDR_LEN);
  arp->tip = inarp->sip;

  e1000_transmit(buf, sizeof(*eth) + sizeof(*arp));

  kfree(inbuf);
}

void
net_rx(char *buf, int len)
{
  struct eth *eth = (struct eth *) buf;

  if(len >= sizeof(struct eth) + sizeof(struct arp) &&
     ntohs(eth->type) == ETHTYPE_ARP){
    arp_rx(buf);
  } else if(len >= sizeof(struct eth) + sizeof(struct ip) &&
     ntohs(eth->type) == ETHTYPE_IP){
    ip_rx(buf, len);
  } else {
    kfree(buf);
  }
}
