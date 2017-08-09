#include "qdbp.h"
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

#ifndef QDBP_NUM_TRAPS
#define QDBP_NUM_TRAPS 8
#endif

struct qdbp_trap {
  qdbp_cb_t  cb        ;
  void      *arg       ;
  void      *add       ;
  int        len       ;
  int        range_idx ;
};

struct qdbp_range {
  void *page  ;
  int   count ;
};

struct qdbp_ctx {
  struct qdbp_trap  trap      [QDBP_NUM_TRAPS] ;
  struct qdbp_range range     [QDBP_NUM_TRAPS] ;
  int               trap_num                   ;
  int               range_num                  ;
  int               pg_sz                      ;
};

static struct qdbp_ctx qdbp_ctx = {};

static void *align_page(void const *addr) {
  uintptr_t pg_msk = qdbp_ctx.pg_sz - 1;
  return (void *)((uintptr_t)addr & ~pg_msk);
}

static void lock_page(void *addr) {
  if (mprotect(addr, qdbp_ctx.pg_sz, PROT_READ)) {
    perror("mprotect");
    exit(1);
  }
}

static void unlock_page(void *addr) {
  if (mprotect(addr, qdbp_ctx.pg_sz, PROT_READ | PROT_WRITE)) {
    perror("mprotect");
    exit(1);
  }
}

static int get_free_trap(void) {
  int t;
  for (t = QDBP_NUM_TRAPS - 1; t >= 0; --t) {
    if (!qdbp_ctx.trap[t].len)
      return t;
  }
  return t;
}

static int get_trap(void *at) {
  int t;
  for (t = QDBP_NUM_TRAPS - 1; t >= 0; --t) {
    void *beg = qdbp_ctx.trap[t].add;
    int   len = qdbp_ctx.trap[t].len;
    if (len && beg <= at && at < beg + len)
      return t;
  }
  return t;
}

static void del_trap(int t) { qdbp_ctx.trap[t].len = 0; }

static int overlap(void const *addr, int len) {
  void const *end = addr + len;
  int t;
  for (t = QDBP_NUM_TRAPS - 1; t >= 0; --t) {
    void *beg = qdbp_ctx.trap[t].add;
    int   len = qdbp_ctx.trap[t].len;
    if (len && beg < end && addr < beg + len) {
      fprintf(stderr, "Overlapping trap\n");
      return 1;
    }
  }
  return 0;
}

static int get_free_range(void) {
  int p;
  for (p = QDBP_NUM_TRAPS - 1; p >= 0; --p) {
    if (!qdbp_ctx.range[p].count)
      return p;
  }
  return p; // unreachable
}

static int get_range(void *at) {
  int p;
  for (p = QDBP_NUM_TRAPS - 1; p >= 0; --p) {
    if (qdbp_ctx.range[p].count && at == qdbp_ctx.range[p].page)
      return p;
  }
  return p;
}

static int new_range(void *addr) {
  int p = get_range(addr);
  if (p < 0) {
    p = get_free_range();
    qdbp_ctx.range[p].page = addr;
    lock_page(addr);
  }
  ++qdbp_ctx.range[p].count;
  return p;
}

static void del_range(int p) {
  --qdbp_ctx.range[p].count;
  if (!qdbp_ctx.range[p].count)
    unlock_page(qdbp_ctx.range[p].page);
}

static void segv_handler(int signo, siginfo_t *info, void *context) {
  void *laddr = align_page(info->si_addr);
  qdbp_ctx.range_num = get_range(laddr);
  if (qdbp_ctx.range_num < 0) {
    fprintf(stderr, "Segmentation fault\n");
    exit(1);
    (void)signo;
    (void)context;
  }
  qdbp_ctx.trap_num = get_trap(info->si_addr);
  unlock_page(laddr);
}

static void trap_handler(int signo) {
  int t = qdbp_ctx.trap_num ;
  int p = qdbp_ctx.range_num;
  qdbp_ctx.trap_num  = -1;
  qdbp_ctx.range_num = -1;
  if (p < 0) {
    fprintf(stderr, "Unexpected signal\n");
    exit(1);
    (void)signo;
  }
  if (t >= 0)
    qdbp_ctx.trap[t].cb(qdbp_ctx.trap[t].arg);
  lock_page(qdbp_ctx.range[p].page);
}

static void initialize(void) {
  struct sigaction segv_sa = {
    .sa_sigaction = segv_handler ,
    .sa_flags     = SA_SIGINFO   ,
  };
  struct sigaction trap_sa = {
    .sa_handler = trap_handler ,
  };
  sigaction(SIGSEGV, &segv_sa, NULL);
  sigaction(SIGUSR1, &trap_sa, NULL);
  qdbp_ctx.pg_sz = sysconf(_SC_PAGESIZE);
}

int qdbp_set_trap(void const *addr, int len, qdbp_cb_t cb, void *arg) {
  void *laddr;
  int t;
  if (!qdbp_ctx.pg_sz)
    initialize();
  laddr = align_page(addr);
  if (!laddr || len <= 0 || len > qdbp_ctx.pg_sz || !cb)
    return -EINVAL;
  if (overlap(addr, len))
    return -EBUSY;
  t = get_free_trap();
  if (t < 0)
    return -ENOSPC;
  qdbp_ctx.trap[t] = (struct qdbp_trap){
    .cb  = cb  ,
    .arg = arg ,
    .add = (void*)addr,
    .len = len ,
    .range_idx = new_range(laddr)
  };
  return t;
}

int qdbp_del_trap(int t) {
  if (0 > t || t >= QDBP_NUM_TRAPS || !qdbp_ctx.trap[t].len)
    return -EINVAL;
  del_range(qdbp_ctx.trap[t].range_idx);
  del_trap(t);
  return 0;
}
