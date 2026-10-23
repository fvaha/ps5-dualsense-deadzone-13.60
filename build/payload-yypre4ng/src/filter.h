#ifndef DZ_FILTER_H
#define DZ_FILTER_H
#include "cone.h"
#include "home-blob.h"
static inline void dz_code(unsigned char *out,uintptr_t first,uintptr_t second,const struct dz_config *config) {
    memcpy(out,blob,sizeof(blob));memcpy(out+2,&first,8);memcpy(out+34,&second,8);
    cone_patch(out,home_fields,config);
}
static inline int dz_code_config(const unsigned char *code,uintptr_t first,uintptr_t second,struct dz_config *config) {
    if(cone_config_read(code,DZ_CONFIG_OFFSET,config))return -1;
    unsigned char expected[sizeof(blob)];dz_code(expected,first,second,config);
    return memcmp(code,expected,sizeof(expected))?-1:0;
}
#endif
