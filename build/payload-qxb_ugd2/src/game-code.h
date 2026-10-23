#ifndef GAME_CODE_H
#define GAME_CODE_H
#include <limits.h>
#include "cone.h"
#include "game-blob.h"
/* The five reader functions are resolved by name at runtime (functions.h)
 * and the code cave is discovered in the library image: nothing here depends
 * on one firmware's layout. */
static inline int game_build(unsigned char *out,uintptr_t cave,const uintptr_t functions[5],const struct dz_config *config) {
    if(!dz_valid(config))return -1;
    memcpy(out,game_blob,sizeof(game_blob));
    for(int i=0;i<5;i++) {
        int64_t delta=(int64_t)functions[i]-(int64_t)(cave+game_entries[i]+7);
        if(delta<INT32_MIN || delta>INT32_MAX)return -1;
        int32_t displacement=(int32_t)delta;memcpy(out+game_entries[i]+3,&displacement,4);
    }
    cone_patch(out,game_fields,config);return 0;
}
static inline int game_code_config(const unsigned char *code,uintptr_t cave,const uintptr_t functions[5],struct dz_config *config) {
    if(cone_config_read(code,GAME_CONFIG_OFFSET,config))return -1;
    unsigned char expected[sizeof(game_blob)];
    return game_build(expected,cave,functions,config) || memcmp(code,expected,sizeof(expected))?-1:0;
}
#endif
