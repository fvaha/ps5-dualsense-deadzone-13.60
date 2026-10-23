#ifndef DZ_PATCH_H
#define DZ_PATCH_H
#include <stdint.h>
#include <stddef.h>
#include "cone.h"
struct dz_state {
    int pid, enabled, connected, fresh;
    unsigned char lx, ly, rx, ry;
    uint64_t timestamp, started;
    struct dz_config config;
    int idle; /* 1 = no stick movement (all axes neutral) for over 5 s */
};
/* 13.60: the handle caches of libScePad live in its rw- segment, which starts
 * at pad + 0x24000 and is 64 KB (0x20018 and its 24 x 0x5c8 records are the
 * 10.20 layout and read as zeros here). */
#define DZ_REC_OFF 0x24000
#define DZ_REC_LEN 0x10000
int dz_read(struct dz_state *out);
const unsigned char *dz_active_sample(const unsigned char *records, uint64_t *stamp);
int dz_apply(const struct dz_config *config, int enabled);
const char *dz_error(void);
int dz_read_remote(int pid, uintptr_t addr, void *buf, size_t size);
int dz_write_remote(int pid, uintptr_t addr, const void *buf, size_t size);
#endif
