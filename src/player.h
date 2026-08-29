#ifndef GAMECHIP_PLAYER_H
#define GAMECHIP_PLAYER_H

#include "gamechip.h"
#include "config.h"
#include "engines/engine.h"
#include "volume.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct gc_player gc_player;

gc_player *gc_player_open(const unsigned char *data, size_t len,
                          const char *filename, const char *m3u_text, size_t m3u_len,
                          const gc_config *cfg);
void gc_player_close(gc_player *p);
int gc_player_info(gc_player *p, gc_info *out);
int gc_player_set_track(gc_player *p, int track0);
int gc_player_current_track(const gc_player *p);
int gc_player_track_count(const gc_player *p);
int gc_player_length_ms(gc_player *p);
int gc_player_process(gc_player *p, float *stereo, int frames);
int gc_player_seek_ms(gc_player *p, int ms);
gc_format gc_player_format(const gc_player *p);
gc_engine gc_player_engine(const gc_player *p);
const char *gc_player_engine_name(const gc_player *p);
int gc_player_rate(const gc_player *p);
int gc_player_voices(gc_player *p);
const char *gc_player_voice_name(gc_player *p, int i);
void gc_player_apply_mute(gc_player *p, const gc_config *cfg);

#ifdef __cplusplus
}
#endif

#endif
