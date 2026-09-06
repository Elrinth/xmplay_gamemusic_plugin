#ifndef GAMECHIP_H
#define GAMECHIP_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PLUGIN_NAME    "Game Music"
#define PLUGIN_VERSION "1.0.11"
#define PLUGIN_XMPVER  1001100 /* 1*1000000+0*10000+11*100; FILEVERSION 1.0.11.0 */

#define GC_MAX_PATH        1024
#define GC_MAX_TITLE       256
#define GC_MAX_TRACKS      256
#define GC_MAX_CHANNELS    32
#define GC_MAX_MODULE      ((size_t)64u * 1024u * 1024u)
#define GC_DEFAULT_RATE    48000
#define GC_DEFAULT_FADE_MS 3000
#define GC_DEFAULT_LOOPS   1
#define GC_DEFAULT_PLAY_MS 600000 /* untagged default TIME = 10 minutes */
#define GC_CAP_MS          (10 * 60 * 1000) /* hard safety cap, never a dummy TIME */
/* Confident measured lengths only (discard fragile phrase-repeat "loops"). */
#define GC_MEAS_MIN_LOOP_MS   15000 /* one-loop / intro+loop end must be >= ~15s */
#define GC_MEAS_SFX_MAX_MS    15000 /* silence-end one-shots / SFX only below this */
#define GC_MEAS_SFX_MIN_MS     250 /* measured SFX silence-end floor */
#define GC_M3U_MIN_MS          250 /* reject only absurd M3U / parse junk */
#define GC_MEAS_MIN_SANE_MS   GC_MEAS_SFX_MIN_MS /* alias: measured + advertise floor */
#define GC_MEAS_SILENCE_MS      800 /* trailing hush before silence-end commit */
/* 1.0.8: NEZ M3U H:MM:SS (0:02:09 = 129s, not ~2s). */
/* 1.0.9: trust M3U [250ms,15s] SFX + live silence-end SetLength for unlisted SFX. */
/* 1.0.10: live SetLength for confident deferred measure.
 * 1.0.11: loop commit floor 55s -> 15s (Zelda II track3 ~38s). */
/* Library placeholders — never expose these as TIME. */
#define GC_DUMMY_GME_MS    150000 /* GME unknown play_length: 2:30 */
#define GC_DUMMY_3MIN_MS   180000
#define GC_DUMMY_NEZ_MS    300000 /* NEZ DefaultPlayTime: 5:00 */
#define gc_is_dummy_length_ms(ms) \
	((ms) == 150000 || (ms) == 180000 || (ms) == 300000)
/* NSF extra-chip bits (header $7B / NSFPlay soundchip). */
#define GC_NSF_VRC6  0x01
#define GC_NSF_VRC7  0x02
#define GC_NSF_FDS   0x04
#define GC_NSF_MMC5  0x08
#define GC_NSF_N163  0x10
#define GC_NSF_5B    0x20

typedef enum {
	GC_FMT_UNKNOWN = 0,
	GC_FMT_AY,
	GC_FMT_GBS,
	GC_FMT_GBR,
	GC_FMT_GYM,
	GC_FMT_HES,
	GC_FMT_KSS,
	GC_FMT_NSF,
	GC_FMT_NSFE,
	GC_FMT_NEZ,
	GC_FMT_NSZ,
	GC_FMT_NSD,
	GC_FMT_RSN,
	GC_FMT_SGC,
	GC_FMT_SPC,
	GC_FMT_VGM,
	GC_FMT_VGZ,
	GC_FMT_CPC,
	GC_FMT_GSF,
	GC_FMT_MINIGSF,
	GC_FMT_USF,
	GC_FMT_MINIUSF,
	GC_FMT_ZIP,
	GC_FMT_SEVENZ,
	GC_FMT_GZIP
} gc_format;

typedef enum {
	GC_ENG_AUTO = 0,
	GC_ENG_NEZ,
	GC_ENG_GME,
	GC_ENG_NSFPLAY,
	GC_ENG_FATSO,
	GC_ENG_GSF,
	GC_ENG_USF,
	GC_ENG_HA
} gc_engine;

typedef struct gc_track_info {
	char title[GC_MAX_TITLE];
	int duration_ms; /* 0 = unknown. Untagged: measured one-loop, else fallback. */
	int loop_ms;     /* M3U loop start or length; 0 = none */
	int fade_ms;     /* M3U fade; 0 = use global fade */
} gc_track_info;

typedef struct gc_info {
	gc_format format;
	int track_count;
	int start_track; /* 0-based */
	char game[GC_MAX_TITLE];
	char artist[GC_MAX_TITLE];
	char copyright[GC_MAX_TITLE];
	char comment[GC_MAX_TITLE];
	char system[64];
	char extra[1024];      /* NSF addresses/chips, GBS detail, etc. */
	char lib_name[GC_MAX_TITLE]; /* PSF _lib / gsflib / usflib */
	char lib_path[GC_MAX_PATH];  /* resolved sibling .gsflib / .usflib */
	char ripper[GC_MAX_TITLE];
	char length_src[32];   /* M3U, NSFe, PSF tag, measured, untagged-max */
	int nsf_load, nsf_init, nsf_play;
	int nsf_exp;           /* NSF extra-chip bits */
	int nsf_ver;
	int nsf2_bits;
	unsigned char nsf_bank[8];
	int nsf_has_bank;
	gc_track_info tracks[GC_MAX_TRACKS];
} gc_info;

#ifdef __cplusplus
}
#endif

#endif
