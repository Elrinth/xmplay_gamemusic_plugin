#ifndef GAMECHIP_M3U_H
#define GAMECHIP_M3U_H

#include "gamechip.h"

#ifdef __cplusplus
extern "C" {
#endif

/* NEZplug-style M3U: filename::NSF,track,title,time,loop,fade
   Track index is 1-based. Zip-internal paths match by basename or suffix.
   time is H:MM:SS[.frac] or M:SS[.frac] or seconds (nsfe2m3u / NEZ). */
int gc_m3u_parse(const char *text, size_t len, const char *music_name, gc_info *out);
int gc_m3u_parse_file(const char *path, const char *music_name, gc_info *out);
int gc_parse_mmss(const char *s);
void gc_info_apply_loops(gc_info *inf, int loop_count);

#ifdef __cplusplus
}
#endif

#endif
