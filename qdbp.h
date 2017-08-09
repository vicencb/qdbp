#ifndef QDBP_H
#define QDBP_H

#include <errno.h>

typedef void (*qdbp_cb_t)(void *);

#define qdbp_trap(object, callback, argument) \
  qdbp_set_trap(&(object), sizeof(object), callback, argument)

int qdbp_set_trap(void const *addr, int len, qdbp_cb_t cb, void *arg);
int qdbp_del_trap(int id);

#endif
