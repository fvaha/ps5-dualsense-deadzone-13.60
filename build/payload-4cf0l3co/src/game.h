#ifndef GAME_H
#define GAME_H
#include "patch.h"
struct game_status {
    int pid, supported, enabled, imports;
    struct dz_config config;
    struct dz_state input;
    const char *error;
};
void game_refresh(void);
void game_tick(const struct dz_config *config, int enabled);
void game_read(struct game_status *out);
int game_sync(const struct dz_config *config, int enabled);
#endif
