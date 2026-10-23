#ifndef DZ_INPUT_H
#define DZ_INPUT_H
#include "patch.h"
struct dz_observation {
    uint64_t stamp;
    int64_t changed, motion;
    int observed, advanced;
};
static inline int dz_sample(struct dz_state *out, struct dz_observation *seen,
                            const unsigned char *data, uint64_t stamp, int64_t now) {
    out->lx=out->ly=out->rx=out->ry=128;
    out->timestamp=0;out->connected=out->fresh=out->idle=0;
    if (now<0) { memset(seen,0,sizeof(*seen));return -1; }
    if (!data || data[76]!=1 || !stamp) { memset(seen,0,sizeof(*seen));return 0; }
    if (!seen->observed || stamp<seen->stamp || now<seen->changed || now<seen->motion) {
        *seen=(struct dz_observation){.stamp=stamp,.changed=now,.motion=now,.observed=1};
    } else if (stamp>seen->stamp) {
        seen->stamp=stamp;seen->changed=now;seen->advanced=1;
    }
    if (data[4]!=128 || data[5]!=128 || data[6]!=128 || data[7]!=128) seen->motion=now;
    out->lx=data[4];out->ly=data[5];out->rx=data[6];out->ry=data[7];
    out->timestamp=stamp;out->connected=1;
    out->fresh=seen->advanced && now-seen->changed<2000;
    out->idle=now-seen->motion>5000;
    return 0;
}
#endif
