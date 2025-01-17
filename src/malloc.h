#ifndef MALLOC_H
#define MALLOC_H

#include <stddef.h>

void get_malloc_mapinfo(void *addr,
                        int *fd,
                        int64_t *map_length,
                        ptrdiff_t *offset);

#endif /* MALLOC_H */
