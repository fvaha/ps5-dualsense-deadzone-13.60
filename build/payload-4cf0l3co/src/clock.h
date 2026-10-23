#ifndef DZ_CLOCK_H
#define DZ_CLOCK_H
#include <stdint.h>
#include <time.h>
static inline int64_t dz_milliseconds(void) {
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now)) return -1;
    return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}
#endif
