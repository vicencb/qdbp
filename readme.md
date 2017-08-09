qdbp
=============
Debug your program with data break-points.
Your callback will be executed whenever a variable is written to.
Example usage:
```c
#include <stdio.h>
#include <stdlib.h>
#include "qdbp.h"

void callback(void *arg) {
  char *ptr = arg;
  if (ptr[123] == 'e') {
    printf("Error: ptr[123] has an invalid value!\n");
    // Check stack backtrace to see who is to blame.
  }
}

int main() {
  char *ptr = malloc(256);
  qdbp_trap(ptr[123], callback, ptr);
  ptr[123] = 'e';
  // As if "callback(ptr);" was here.
  return(0);
}
```
Synopsys
---------------
```c
#include "qdbp.h"
int qdbp_set_trap(void const *addr, int len, qdbp_cb_t cb, void *arg);
int qdbp_del_trap(int id);
```
Description
---------------
The `qdbp_set_trap` function sets a trap that covers `len` bytes
of memory starting at `addr`.
Whenever any memory location within the range is written to,
the callback `cb` will be called with argument `arg`.
Returns the identifier of the trap.

There is a convenience macro
`qdbp_trap(object, callback, argument)` which is equivalent to
`qdbp_set_trap(&(object), sizeof(object), callback, argument)`.

The `qdbp_del_trap` function deletes the trap identified by `id`,
which must have been returned by a previous call to `qdbp_set_trap`.

Limitations
---------------
The program using `qdbp` needs to be run under `qdbp_enable`:
```sh
qdbp_enable PROGRAM [ARGUMENT]...
```
otherwise the callback will not be executed.
This limitation comes from the use of `ptrace`.

The memory range covered by a trap needs to be valid for `mprotect`.
That means that it should come from a call to `mmap()`.
This is most probably the case for memory allocated by any of the
`malloc`-family of functions.
Before memory covered by a trap is freed or re-allocated,
it should be released with `qdbp_del_trap`.
After re-allocating, traps can be assigned again.

The underlying mechanism makes use of the signals
`SIGSEGV`, `SIGTRAP` and `SIGUSR1`, so, they must not be used
by the application.

The number of simultaneous active traps may be limited by implementation.

It has only been tested with the x86 architecture.

Return value
---------------
The `qdbp_set_trap` function returns the trap identifier or
a negative value in case of error.

The `qdbp_del_trap` function returns 0 on success or
a negative value in case of error.

Errors
---------------
The `qdbp_set_trap` returns:
* `-EINVAL` in case of an invalid argument:
  * `addr` is `NULL` or less than the page size.
  * `len` is less than 1 or greater than the page size.
  * `cb` is `NULL`.
* `-EBUSY` in case the memory range specified overlaps with another trap.

The `qdbp_set_trap` returns:
* `-EINVAL` in case of an invalid argument:
  * `id` does not represent an active trap.

License
---------------
This software is licensed under the 0BSD license (BSD Zero Clause License).

https://spdx.org/licenses/0BSD.html
