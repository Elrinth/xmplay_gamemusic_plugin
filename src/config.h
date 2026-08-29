#ifndef GAMECHIP_CONFIG_H
#define GAMECHIP_CONFIG_H

#include "gamechip.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct gc_config {
	int rate;
	int loop_count;
	int fade_ms;
	int auto_normalize;
	float loudness_db;     /* target peak-ish, default +6 (loud) */
	float stereo_width;    /* 0 = off */
	float trim_db[32];     /* index by gc_format */
	float engine_db[8];    /* index by gc_engine */
	gc_engine engine_nsf;
	gc_engine engine_gbs;
	gc_engine engine_kss;
	gc_engine engine_ay;
	gc_engine engine_hes;
	gc_engine engine_cpc;
	gc_engine engine_sgc;
	unsigned char mute[GC_MAX_CHANNELS];
	float chan_level[GC_MAX_CHANNELS];
	int soft_limiter;      /* default on */
	int output_mono;       /* 0 = stereo */
	int filter_mode;       /* 0 none, 1 hipass, 2 lopass, 3 prepass */
	int filter_hz;
	int use_tag_length;    /* prefer M3U / NSFe / PSF length */
	int measure_untagged;  /* default on: one-loop / song-end scan */
	int untagged_max_sec;  /* fallback + measure scan cap; default 180 */
	int fatso_pal;
	int fatso_silence_ms;
	int fatso_highpass;
	int fatso_lowpass;
	int fatso_prepass;
	int fatso_hpf_hz;
	int fatso_lpf_hz;
	int fatso_pre_hz;
	int fatso_dmc_pop;
	int fatso_n106_pop;
	int fatso_fds_pop;
	int fatso_ignore_4011;
	int fatso_reset_duty;
	int fatso_ignore_brk;
	int fatso_ignore_illegal;
	int fatso_no_wait_play;
	int fatso_reset_regs;
	int fatso_ignore_version;
	int fatso_force_4017;  /* 0 none, 1 $00, 2 $80 */
	int fatso_invert_hz;
	int fatso_no_silence_if_time;
	int nsfplay_irq;
	int nsfplay_n163_mux;
	int nsfplay_region;
	int gsf_interpolation;
	int gsf_loops;
	gc_engine engine_gsf;
	int usf_hle_audio;     /* lazyusf2; default on. HLE RSP gfx is always on. */
	int gbs_use_int;
	int gbs_highpass;      /* optional GB capacitor high-pass */
	int spc_interp;        /* 0 off, 1 linear, 2 cubic */
	int chan_vol[GC_MAX_CHANNELS]; /* 0–255 */
	int chan_pan[GC_MAX_CHANNELS]; /* -128…128 */
	char ini_path[GC_MAX_PATH];
	char len_cache_path[GC_MAX_PATH];
	/* Runtime only — not written to INI. Player fills these for PSF _lib lookup. */
	const struct gc_vfs *vfs;
	char vfs_dir[GC_MAX_PATH];
	char vfs_uri[GC_MAX_PATH];
} gc_config;

void gc_config_defaults(gc_config *c);
void gc_config_set_ini_path(gc_config *c, const char *dll_dir);
int gc_config_load(gc_config *c);
int gc_config_save(const gc_config *c);
gc_engine gc_config_engine_for(const gc_config *c, gc_format fmt);
const char *gc_engine_name(gc_engine e);
int gc_config_untagged_cap_ms(const gc_config *c);
int gc_config_untagged_fallback_ms(const gc_config *c);

#ifdef __cplusplus
}
#endif

#endif
