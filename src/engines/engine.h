#ifndef GAMECHIP_ENGINE_H
#define GAMECHIP_ENGINE_H

#include "../gamechip.h"
#include "../config.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct gc_eng_state gc_eng_state;

typedef struct gc_eng_ops {
	const char *name;
	gc_engine kind;
	int (*can_open)(gc_format fmt);
	gc_eng_state *(*open)(const unsigned char *data, size_t len, int rate,
	                      const gc_config *cfg);
	void (*close)(gc_eng_state *s);
	int (*info)(gc_eng_state *s, gc_info *out);
	int (*set_track)(gc_eng_state *s, int track0);
	int (*render)(gc_eng_state *s, float *stereo, int frames);
	int (*seek_ms)(gc_eng_state *s, int ms);
	int (*length_ms)(gc_eng_state *s, int track0);
	int (*set_mute)(gc_eng_state *s, const unsigned char mute[GC_MAX_CHANNELS],
	                const float level[GC_MAX_CHANNELS]);
	/* Optional: full mixer (mute/vol/pan) from config. Falls back to set_mute. */
	int (*set_mixer)(gc_eng_state *s, const gc_config *cfg);
	int (*voice_count)(gc_eng_state *s);
	const char *(*voice_name)(gc_eng_state *s, int i);
	/* Optional: one-loop / song-end scan. 0 = not detected (caller may PCM-scan). */
	int (*measure_ms)(gc_eng_state *s, int track0, int cap_ms, int fade_ms);
	/* Optional: non-blocking measure while playback continues on another instance. */
	int (*defer_measure_start)(gc_eng_state *s, int track0, int cap_ms, int fade_ms);
	int (*defer_measure_poll)(gc_eng_state *s); /* 0=busy, >0=ms done, -1=no detect */
	void (*defer_measure_cancel)(gc_eng_state *s);
} gc_eng_ops;

const gc_eng_ops *gc_eng_nez(void);
const gc_eng_ops *gc_eng_gme(void);
const gc_eng_ops *gc_eng_nsfplay(void);
const gc_eng_ops *gc_eng_fatso(void);
const gc_eng_ops *gc_eng_gsf(void);
const gc_eng_ops *gc_eng_usf(void);
const gc_eng_ops *gc_eng_ha(void);

#ifdef __cplusplus
}
#endif

#endif
