#ifndef GAMECHIP_MEASURE_H
#define GAMECHIP_MEASURE_H

#include "engines/engine.h"

#ifdef __cplusplus
extern "C" {
#endif

/* PCM / engine-end scan for one untagged chip track.
   Length = intro + one loop + fade, or last audible peak + tail.
   Ignores leading hush. Returns 0 if the scan cap is hit with no loop
   and no song-end (caller uses the untagged fallback). */
int gc_measure_pcm_ms(const gc_eng_ops *ops, gc_eng_state *eng,
                      int track0, int rate, int cap_ms, int fade_ms);

#ifdef __cplusplus
}
#endif

#endif
