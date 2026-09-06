#include "config.h"
#include "lencache.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#endif

static void copy_str(char *dst, size_t cap, const char *src)
{
	size_t n;
	if (!dst || cap == 0)
		return;
	if (!src) {
		dst[0] = '\0';
		return;
	}
	n = strlen(src);
	if (n >= cap)
		n = cap - 1;
	memcpy(dst, src, n);
	dst[n] = '\0';
}

const char *gc_engine_name(gc_engine e)
{
	switch (e) {
	case GC_ENG_NEZ: return "NEZplug++";
	case GC_ENG_GME: return "Game_Music_Emu";
	case GC_ENG_NSFPLAY: return "NSFPlay";
	case GC_ENG_FATSO: return "NotSo Fatso";
	case GC_ENG_GSF: return "VIOGSF";
	case GC_ENG_USF: return "lazyusf2";
	case GC_ENG_HA: return "Highly Advanced";
	default: return "Auto";
	}
}

static gc_engine parse_eng(const char *s)
{
	if (!s) return GC_ENG_AUTO;
	if (!strcmp(s, "nez") || !strcmp(s, "NEZ") || !strcmp(s, "nezplug"))
		return GC_ENG_NEZ;
	if (!strcmp(s, "gme") || !strcmp(s, "GME"))
		return GC_ENG_GME;
	if (!strcmp(s, "nsfplay") || !strcmp(s, "NSFPlay"))
		return GC_ENG_NSFPLAY;
	if (!strcmp(s, "fatso") || !strcmp(s, "notso") || !strcmp(s, "NotSo Fatso"))
		return GC_ENG_FATSO;
	if (!strcmp(s, "ha") || !strcmp(s, "highlyadvanced") ||
	    !strcmp(s, "Highly Advanced") || !strcmp(s, "highly advanced"))
		return GC_ENG_HA;
	if (!strcmp(s, "viogsf") || !strcmp(s, "VIOGSF") || !strcmp(s, "gsf") ||
	    !strcmp(s, "GSF"))
		return GC_ENG_GSF;
	return GC_ENG_AUTO;
}

static const char *eng_key(gc_engine e)
{
	switch (e) {
	case GC_ENG_NEZ: return "nez";
	case GC_ENG_GME: return "gme";
	case GC_ENG_NSFPLAY: return "nsfplay";
	case GC_ENG_FATSO: return "fatso";
	case GC_ENG_HA: return "ha";
	case GC_ENG_GSF: return "viogsf";
	default: return "auto";
	}
}

void gc_config_defaults(gc_config *c)
{
	int i;
	if (!c)
		return;
	memset(c, 0, sizeof *c);
	c->rate = GC_DEFAULT_RATE;
	c->loop_count = GC_DEFAULT_LOOPS;
	c->fade_ms = GC_DEFAULT_FADE_MS;
	c->auto_normalize = 1;
	c->soft_limiter = 1;
	c->loudness_db = 2.0f; /* quieter default; soft limiter still catches peaks */
	c->stereo_width = 0.0f;
	c->nsfplay_irq = 1;
	c->nsfplay_n163_mux = 1;
	c->nsfplay_region = 0;
	c->engine_nsf = GC_ENG_AUTO; /* NSFPlay first in fallback list */
	c->engine_gbs = GC_ENG_AUTO;
	c->engine_kss = GC_ENG_AUTO;
	c->engine_ay = GC_ENG_AUTO;
	c->engine_hes = GC_ENG_AUTO;
	c->engine_cpc = GC_ENG_AUTO;
	c->engine_sgc = GC_ENG_AUTO;
	c->engine_gsf = GC_ENG_AUTO;
	c->fatso_pal = 0;
	c->fatso_highpass = 0;
	c->fatso_lowpass = 0;
	c->gsf_interpolation = 1;
	c->gsf_loops = 2;
	c->usf_hle_audio = 1;
	c->gbs_highpass = 0;
	c->spc_interp = 1;
	c->use_tag_length = 1;
	c->measure_untagged = 1;
	c->untagged_max_sec = 600;
	c->filter_hz = 210;
	c->fatso_silence_ms = 1200;
	c->fatso_dmc_pop = 1;
	c->fatso_n106_pop = 1;
	c->fatso_fds_pop = 1;
	c->fatso_no_silence_if_time = 1;
	c->fatso_invert_hz = 210;
	c->fatso_hpf_hz = 120;
	c->fatso_lpf_hz = 12000;
	c->fatso_pre_hz = 120;
	c->nsfplay_irq = 1;
	c->nsfplay_n163_mux = 1;
	for (i = 0; i < GC_MAX_CHANNELS; ++i) {
		c->chan_vol[i] = 255;
		c->chan_pan[i] = 0;
		c->chan_level[i] = 1.0f;
	}
	c->chan_vol[2] = 124; /* triangle */
	c->chan_level[2] = 124.0f / 255.0f;
	c->chan_pan[0] = -19; /* square 1 */
	c->chan_pan[1] = 19;  /* square 2 */
	c->trim_db[GC_FMT_KSS] = 6.0f;
	c->trim_db[GC_FMT_GBS] = 3.0f;
	c->trim_db[GC_FMT_GBR] = 3.0f;
	c->trim_db[GC_FMT_AY] = 3.0f;
	c->trim_db[GC_FMT_CPC] = 3.0f;
	c->trim_db[GC_FMT_HES] = 3.0f;
	c->trim_db[GC_FMT_SGC] = 3.0f;
	c->trim_db[GC_FMT_NSF] = 0.0f;
	c->trim_db[GC_FMT_NSFE] = 0.0f;
	c->trim_db[GC_FMT_NEZ] = 0.0f;
	c->engine_db[GC_ENG_FATSO] = -3.0f;
}

void gc_config_set_ini_path(gc_config *c, const char *dll_dir)
{
	if (!c)
		return;
	if (dll_dir && dll_dir[0]) {
		size_t n = strlen(dll_dir);
		copy_str(c->ini_path, sizeof c->ini_path, dll_dir);
		if (n && dll_dir[n - 1] != '/' && dll_dir[n - 1] != '\\')
			strncat(c->ini_path, "/", sizeof c->ini_path - strlen(c->ini_path) - 1);
		strncat(c->ini_path, "xmp-gamemusic.ini",
		        sizeof c->ini_path - strlen(c->ini_path) - 1);
	} else {
		copy_str(c->ini_path, sizeof c->ini_path, "xmp-gamemusic.ini");
	}
	gc_len_cache_path_from_ini(c->len_cache_path, sizeof c->len_cache_path, c->ini_path);
}

static int get_int(const char *sec, const char *key, int def, const char *file)
{
#ifdef _WIN32
	return GetPrivateProfileIntA(sec, key, def, file);
#else
	char line[256];
	FILE *f = fopen(file, "r");
	int have_sec = 0;
	(void)sec;
	if (!f)
		return def;
	while (fgets(line, sizeof line, f)) {
		char *eq;
		if (line[0] == '[') {
			have_sec = (strncmp(line, "[gamemusic]", 11) == 0);
			continue;
		}
		if (!have_sec)
			continue;
		eq = strchr(line, '=');
		if (!eq)
			continue;
		*eq = 0;
		if (strcmp(line, key) == 0) {
			int v = atoi(eq + 1);
			fclose(f);
			return v;
		}
	}
	fclose(f);
	return def;
#endif
}

static void get_str(const char *sec, const char *key, const char *def,
                    char *buf, size_t cap, const char *file)
{
#ifdef _WIN32
	GetPrivateProfileStringA(sec, key, def, buf, (DWORD)cap, file);
#else
	char line[256];
	FILE *f = fopen(file, "r");
	int have_sec = 0;
	copy_str(buf, cap, def);
	if (!f)
		return;
	while (fgets(line, sizeof line, f)) {
		char *eq, *nl;
		if (line[0] == '[') {
			have_sec = (strncmp(line, "[gamemusic]", 11) == 0);
			continue;
		}
		if (!have_sec)
			continue;
		eq = strchr(line, '=');
		if (!eq)
			continue;
		*eq = 0;
		if (strcmp(line, key) == 0) {
			nl = strchr(eq + 1, '\n');
			if (nl) *nl = 0;
			copy_str(buf, cap, eq + 1);
			break;
		}
	}
	fclose(f);
	(void)sec;
#endif
}

#ifdef _WIN32
#include <windows.h>
static void set_int(const char *sec, const char *key, int v, const char *file)
{
	char buf[32];
	snprintf(buf, sizeof buf, "%d", v);
	WritePrivateProfileStringA(sec, key, buf, file);
}
static void set_str(const char *sec, const char *key, const char *v, const char *file)
{
	WritePrivateProfileStringA(sec, key, v, file);
}
#else
static void rewrite_ini(const gc_config *c)
{
	FILE *f;
	if (!c->ini_path[0])
		return;
	f = fopen(c->ini_path, "w");
	if (!f)
		return;
	fprintf(f, "[gamemusic]\n");
	fprintf(f, "rate=%d\n", c->rate);
	fprintf(f, "loop_count=%d\n", c->loop_count);
	fprintf(f, "fade_ms=%d\n", c->fade_ms);
	fprintf(f, "auto_normalize=%d\n", c->auto_normalize);
	fprintf(f, "soft_limiter=%d\n", c->soft_limiter);
	fprintf(f, "loudness_db=%.2f\n", (double)c->loudness_db);
	fprintf(f, "stereo_width=%.2f\n", (double)c->stereo_width);
	fprintf(f, "engine_nsf=%s\n", eng_key(c->engine_nsf));
	fprintf(f, "engine_gbs=%s\n", eng_key(c->engine_gbs));
	fprintf(f, "engine_kss=%s\n", eng_key(c->engine_kss));
	fprintf(f, "engine_ay=%s\n", eng_key(c->engine_ay));
	fprintf(f, "engine_hes=%s\n", eng_key(c->engine_hes));
	fprintf(f, "engine_cpc=%s\n", eng_key(c->engine_cpc));
	fprintf(f, "engine_sgc=%s\n", eng_key(c->engine_sgc));
	fprintf(f, "engine_gsf=%s\n", eng_key(c->engine_gsf));
	fprintf(f, "trim_kss_db=%.2f\n", (double)c->trim_db[GC_FMT_KSS]);
	fprintf(f, "trim_gbs_db=%.2f\n", (double)c->trim_db[GC_FMT_GBS]);
	fprintf(f, "trim_ay_db=%.2f\n", (double)c->trim_db[GC_FMT_AY]);
	fprintf(f, "trim_hes_db=%.2f\n", (double)c->trim_db[GC_FMT_HES]);
	fprintf(f, "trim_nsf_db=%.2f\n", (double)c->trim_db[GC_FMT_NSF]);
	fprintf(f, "trim_vgm_db=%.2f\n", (double)c->trim_db[GC_FMT_VGM]);
	fprintf(f, "engine_fatso_db=%.2f\n", (double)c->engine_db[GC_ENG_FATSO]);
	fprintf(f, "fatso_pal=%d\n", c->fatso_pal);
	fprintf(f, "fatso_silence_ms=%d\n", c->fatso_silence_ms);
	fprintf(f, "fatso_highpass=%d\n", c->fatso_highpass);
	fprintf(f, "fatso_lowpass=%d\n", c->fatso_lowpass);
	fprintf(f, "nsfplay_irq=%d\n", c->nsfplay_irq);
	fprintf(f, "nsfplay_n163_mux=%d\n", c->nsfplay_n163_mux);
	fprintf(f, "nsfplay_region=%d\n", c->nsfplay_region);
	fprintf(f, "gsf_interpolation=%d\n", c->gsf_interpolation);
	fprintf(f, "gsf_loops=%d\n", c->gsf_loops);
	fprintf(f, "usf_hle_audio=%d\n", c->usf_hle_audio);
	{
		unsigned mask = 0;
		int i;
		for (i = 0; i < 32 && i < GC_MAX_CHANNELS; ++i)
			if (c->mute[i])
				mask |= (1u << i);
		fprintf(f, "mute=%u\n", mask);
		fprintf(f, "chan_vol=");
		for (i = 0; i < GC_MAX_CHANNELS; ++i)
			fprintf(f, "%s%d", i ? "," : "", c->chan_vol[i]);
		fprintf(f, "\nchan_pan=");
		for (i = 0; i < GC_MAX_CHANNELS; ++i)
			fprintf(f, "%s%d", i ? "," : "", c->chan_pan[i]);
		fprintf(f, "\n");
	}
	fprintf(f, "output_mono=%d\n", c->output_mono);
	fprintf(f, "filter_mode=%d\n", c->filter_mode);
	fprintf(f, "filter_hz=%d\n", c->filter_hz);
	fprintf(f, "use_tag_length=%d\n", c->use_tag_length);
	fprintf(f, "measure_untagged=%d\n", c->measure_untagged);
	fprintf(f, "untagged_max_sec=%d\n", c->untagged_max_sec);
	fprintf(f, "fatso_dmc_pop=%d\n", c->fatso_dmc_pop);
	fprintf(f, "fatso_n106_pop=%d\n", c->fatso_n106_pop);
	fprintf(f, "fatso_fds_pop=%d\n", c->fatso_fds_pop);
	fprintf(f, "fatso_ignore_4011=%d\n", c->fatso_ignore_4011);
	fprintf(f, "fatso_reset_duty=%d\n", c->fatso_reset_duty);
	fprintf(f, "fatso_ignore_brk=%d\n", c->fatso_ignore_brk);
	fprintf(f, "fatso_ignore_illegal=%d\n", c->fatso_ignore_illegal);
	fprintf(f, "fatso_no_wait_play=%d\n", c->fatso_no_wait_play);
	fprintf(f, "fatso_reset_regs=%d\n", c->fatso_reset_regs);
	fprintf(f, "fatso_ignore_version=%d\n", c->fatso_ignore_version);
	fprintf(f, "fatso_force_4017=%d\n", c->fatso_force_4017);
	fprintf(f, "fatso_invert_hz=%d\n", c->fatso_invert_hz);
	fprintf(f, "fatso_no_silence_if_time=%d\n", c->fatso_no_silence_if_time);
	fprintf(f, "fatso_prepass=%d\n", c->fatso_prepass);
	fprintf(f, "gbs_use_int=%d\n", c->gbs_use_int);
	fprintf(f, "gbs_highpass=%d\n", c->gbs_highpass);
	fprintf(f, "spc_interp=%d\n", c->spc_interp);
	fclose(f);
}
#endif

int gc_config_load(gc_config *c)
{
	char buf[64];
	const char *ini;
	if (!c || !c->ini_path[0])
		return 0;
	ini = c->ini_path;
	c->rate = get_int("gamemusic", "rate", c->rate, ini);
	if (c->rate < 8000 || c->rate > 192000)
		c->rate = GC_DEFAULT_RATE;
	c->loop_count = get_int("gamemusic", "loop_count", c->loop_count, ini);
	if (c->loop_count < 0)
		c->loop_count = 0;
	if (c->loop_count > 16)
		c->loop_count = 16;
	c->fade_ms = get_int("gamemusic", "fade_ms", c->fade_ms, ini);
	if (c->fade_ms < 0)
		c->fade_ms = 0;
	if (c->fade_ms > 30000)
		c->fade_ms = 30000;
	c->auto_normalize = get_int("gamemusic", "auto_normalize", c->auto_normalize, ini) ? 1 : 0;
	c->soft_limiter = get_int("gamemusic", "soft_limiter", c->soft_limiter, ini) ? 1 : 0;
	get_str("gamemusic", "loudness_db", "2", buf, sizeof buf, ini);
	c->loudness_db = (float)atof(buf);
	get_str("gamemusic", "stereo_width", "0", buf, sizeof buf, ini);
	c->stereo_width = (float)atof(buf);
	get_str("gamemusic", "engine_nsf", "auto", buf, sizeof buf, ini);
	c->engine_nsf = parse_eng(buf);
	get_str("gamemusic", "engine_gbs", "auto", buf, sizeof buf, ini);
	c->engine_gbs = parse_eng(buf);
	get_str("gamemusic", "engine_kss", "auto", buf, sizeof buf, ini);
	c->engine_kss = parse_eng(buf);
	get_str("gamemusic", "engine_ay", "auto", buf, sizeof buf, ini);
	c->engine_ay = parse_eng(buf);
	get_str("gamemusic", "engine_hes", "auto", buf, sizeof buf, ini);
	c->engine_hes = parse_eng(buf);
	get_str("gamemusic", "engine_cpc", "auto", buf, sizeof buf, ini);
	c->engine_cpc = parse_eng(buf);
	get_str("gamemusic", "engine_sgc", "auto", buf, sizeof buf, ini);
	c->engine_sgc = parse_eng(buf);
	get_str("gamemusic", "engine_gsf", "auto", buf, sizeof buf, ini);
	c->engine_gsf = parse_eng(buf);
	get_str("gamemusic", "trim_kss_db", "6", buf, sizeof buf, ini);
	c->trim_db[GC_FMT_KSS] = (float)atof(buf);
	get_str("gamemusic", "trim_gbs_db", "3", buf, sizeof buf, ini);
	c->trim_db[GC_FMT_GBS] = (float)atof(buf);
	c->trim_db[GC_FMT_GBR] = c->trim_db[GC_FMT_GBS];
	get_str("gamemusic", "trim_ay_db", "3", buf, sizeof buf, ini);
	c->trim_db[GC_FMT_AY] = (float)atof(buf);
	c->trim_db[GC_FMT_CPC] = c->trim_db[GC_FMT_AY];
	c->trim_db[GC_FMT_SGC] = c->trim_db[GC_FMT_AY];
	get_str("gamemusic", "trim_hes_db", "3", buf, sizeof buf, ini);
	c->trim_db[GC_FMT_HES] = (float)atof(buf);
	get_str("gamemusic", "trim_nsf_db", "2", buf, sizeof buf, ini);
	c->trim_db[GC_FMT_NSF] = (float)atof(buf);
	c->trim_db[GC_FMT_NSFE] = c->trim_db[GC_FMT_NSF];
	c->trim_db[GC_FMT_NEZ] = c->trim_db[GC_FMT_NSF];
	get_str("gamemusic", "trim_vgm_db", "0", buf, sizeof buf, ini);
	c->trim_db[GC_FMT_VGM] = (float)atof(buf);
	c->trim_db[GC_FMT_VGZ] = c->trim_db[GC_FMT_VGM];
	c->trim_db[GC_FMT_GYM] = c->trim_db[GC_FMT_VGM];
	get_str("gamemusic", "engine_fatso_db", "-3", buf, sizeof buf, ini);
	c->engine_db[GC_ENG_FATSO] = (float)atof(buf);
	c->fatso_pal = get_int("gamemusic", "fatso_pal", c->fatso_pal, ini) ? 1 : 0;
	c->fatso_silence_ms = get_int("gamemusic", "fatso_silence_ms", c->fatso_silence_ms, ini);
	c->fatso_highpass = get_int("gamemusic", "fatso_highpass", c->fatso_highpass, ini) ? 1 : 0;
	c->fatso_lowpass = get_int("gamemusic", "fatso_lowpass", c->fatso_lowpass, ini) ? 1 : 0;
	c->nsfplay_irq = get_int("gamemusic", "nsfplay_irq", c->nsfplay_irq, ini) ? 1 : 0;
	c->nsfplay_n163_mux = get_int("gamemusic", "nsfplay_n163_mux", c->nsfplay_n163_mux, ini) ? 1 : 0;
	c->nsfplay_region = get_int("gamemusic", "nsfplay_region", c->nsfplay_region, ini);
	c->gsf_interpolation = get_int("gamemusic", "gsf_interpolation", c->gsf_interpolation, ini) ? 1 : 0;
	c->gsf_loops = get_int("gamemusic", "gsf_loops", c->gsf_loops, ini);
	if (c->gsf_loops < 1)
		c->gsf_loops = 2;
	if (c->gsf_loops > 16)
		c->gsf_loops = 16;
	c->usf_hle_audio = get_int("gamemusic", "usf_hle_audio", c->usf_hle_audio, ini) ? 1 : 0;
	{
		unsigned mask = (unsigned)get_int("gamemusic", "mute", 0, ini);
		int i;
		char vbuf[512], *tok;
		for (i = 0; i < 32 && i < GC_MAX_CHANNELS; ++i)
			c->mute[i] = (mask & (1u << i)) ? 1 : 0;
		get_str("gamemusic", "chan_vol", "", vbuf, sizeof vbuf, ini);
		tok = vbuf;
		for (i = 0; i < GC_MAX_CHANNELS && tok && tok[0]; ++i) {
			c->chan_vol[i] = atoi(tok);
			if (c->chan_vol[i] < 0) c->chan_vol[i] = 0;
			if (c->chan_vol[i] > 255) c->chan_vol[i] = 255;
			c->chan_level[i] = (float)c->chan_vol[i] / 255.0f;
			tok = strchr(tok, ',');
			if (tok) tok++;
		}
		get_str("gamemusic", "chan_pan", "", vbuf, sizeof vbuf, ini);
		tok = vbuf;
		for (i = 0; i < GC_MAX_CHANNELS && tok && tok[0]; ++i) {
			c->chan_pan[i] = atoi(tok);
			if (c->chan_pan[i] < -128) c->chan_pan[i] = -128;
			if (c->chan_pan[i] > 128) c->chan_pan[i] = 128;
			tok = strchr(tok, ',');
			if (tok) tok++;
		}
	}
	c->output_mono = get_int("gamemusic", "output_mono", c->output_mono, ini) ? 1 : 0;
	c->filter_mode = get_int("gamemusic", "filter_mode", c->filter_mode, ini);
	c->filter_hz = get_int("gamemusic", "filter_hz", c->filter_hz, ini);
	c->use_tag_length = get_int("gamemusic", "use_tag_length", c->use_tag_length, ini) ? 1 : 0;
	c->measure_untagged = get_int("gamemusic", "measure_untagged", c->measure_untagged, ini) ? 1 : 0;
	c->untagged_max_sec = get_int("gamemusic", "untagged_max_sec", c->untagged_max_sec, ini);
	if (c->untagged_max_sec < 10)
		c->untagged_max_sec = 10;
	if (c->untagged_max_sec > 600)
		c->untagged_max_sec = 600;
	c->fatso_dmc_pop = get_int("gamemusic", "fatso_dmc_pop", c->fatso_dmc_pop, ini) ? 1 : 0;
	c->fatso_n106_pop = get_int("gamemusic", "fatso_n106_pop", c->fatso_n106_pop, ini) ? 1 : 0;
	c->fatso_fds_pop = get_int("gamemusic", "fatso_fds_pop", c->fatso_fds_pop, ini) ? 1 : 0;
	c->fatso_ignore_4011 = get_int("gamemusic", "fatso_ignore_4011", c->fatso_ignore_4011, ini) ? 1 : 0;
	c->fatso_reset_duty = get_int("gamemusic", "fatso_reset_duty", c->fatso_reset_duty, ini) ? 1 : 0;
	c->fatso_ignore_brk = get_int("gamemusic", "fatso_ignore_brk", c->fatso_ignore_brk, ini) ? 1 : 0;
	c->fatso_ignore_illegal = get_int("gamemusic", "fatso_ignore_illegal", c->fatso_ignore_illegal, ini) ? 1 : 0;
	c->fatso_no_wait_play = get_int("gamemusic", "fatso_no_wait_play", c->fatso_no_wait_play, ini) ? 1 : 0;
	c->fatso_reset_regs = get_int("gamemusic", "fatso_reset_regs", c->fatso_reset_regs, ini) ? 1 : 0;
	c->fatso_ignore_version = get_int("gamemusic", "fatso_ignore_version", c->fatso_ignore_version, ini) ? 1 : 0;
	c->fatso_force_4017 = get_int("gamemusic", "fatso_force_4017", c->fatso_force_4017, ini);
	c->fatso_invert_hz = get_int("gamemusic", "fatso_invert_hz", c->fatso_invert_hz, ini);
	c->fatso_no_silence_if_time = get_int("gamemusic", "fatso_no_silence_if_time", c->fatso_no_silence_if_time, ini) ? 1 : 0;
	c->fatso_prepass = get_int("gamemusic", "fatso_prepass", c->fatso_prepass, ini) ? 1 : 0;
	c->gbs_use_int = get_int("gamemusic", "gbs_use_int", c->gbs_use_int, ini) ? 1 : 0;
	c->gbs_highpass = get_int("gamemusic", "gbs_highpass", c->gbs_highpass, ini) ? 1 : 0;
	c->spc_interp = get_int("gamemusic", "spc_interp", c->spc_interp, ini);
	return 1;
}

int gc_config_save(const gc_config *c)
{
	if (!c || !c->ini_path[0])
		return 0;
#ifdef _WIN32
	set_int("gamemusic", "rate", c->rate, c->ini_path);
	set_int("gamemusic", "loop_count", c->loop_count, c->ini_path);
	set_int("gamemusic", "fade_ms", c->fade_ms, c->ini_path);
	set_int("gamemusic", "auto_normalize", c->auto_normalize, c->ini_path);
	{
		char buf[32];
		snprintf(buf, sizeof buf, "%.2f", (double)c->loudness_db);
		set_str("gamemusic", "loudness_db", buf, c->ini_path);
		snprintf(buf, sizeof buf, "%.2f", (double)c->stereo_width);
		set_str("gamemusic", "stereo_width", buf, c->ini_path);
		snprintf(buf, sizeof buf, "%.2f", (double)c->trim_db[GC_FMT_KSS]);
		set_str("gamemusic", "trim_kss_db", buf, c->ini_path);
		snprintf(buf, sizeof buf, "%.2f", (double)c->trim_db[GC_FMT_GBS]);
		set_str("gamemusic", "trim_gbs_db", buf, c->ini_path);
		snprintf(buf, sizeof buf, "%.2f", (double)c->trim_db[GC_FMT_AY]);
		set_str("gamemusic", "trim_ay_db", buf, c->ini_path);
		snprintf(buf, sizeof buf, "%.2f", (double)c->trim_db[GC_FMT_HES]);
		set_str("gamemusic", "trim_hes_db", buf, c->ini_path);
		snprintf(buf, sizeof buf, "%.2f", (double)c->trim_db[GC_FMT_NSF]);
		set_str("gamemusic", "trim_nsf_db", buf, c->ini_path);
		snprintf(buf, sizeof buf, "%.2f", (double)c->trim_db[GC_FMT_VGM]);
		set_str("gamemusic", "trim_vgm_db", buf, c->ini_path);
	}
	set_int("gamemusic", "soft_limiter", c->soft_limiter, c->ini_path);
	set_str("gamemusic", "engine_nsf", eng_key(c->engine_nsf), c->ini_path);
	set_str("gamemusic", "engine_gbs", eng_key(c->engine_gbs), c->ini_path);
	set_str("gamemusic", "engine_kss", eng_key(c->engine_kss), c->ini_path);
	set_str("gamemusic", "engine_ay", eng_key(c->engine_ay), c->ini_path);
	set_str("gamemusic", "engine_hes", eng_key(c->engine_hes), c->ini_path);
	set_str("gamemusic", "engine_cpc", eng_key(c->engine_cpc), c->ini_path);
	set_str("gamemusic", "engine_sgc", eng_key(c->engine_sgc), c->ini_path);
	set_str("gamemusic", "engine_gsf", eng_key(c->engine_gsf), c->ini_path);
	set_int("gamemusic", "fatso_pal", c->fatso_pal, c->ini_path);
	set_int("gamemusic", "fatso_silence_ms", c->fatso_silence_ms, c->ini_path);
	set_int("gamemusic", "fatso_highpass", c->fatso_highpass, c->ini_path);
	set_int("gamemusic", "fatso_lowpass", c->fatso_lowpass, c->ini_path);
	set_int("gamemusic", "nsfplay_irq", c->nsfplay_irq, c->ini_path);
	set_int("gamemusic", "nsfplay_n163_mux", c->nsfplay_n163_mux, c->ini_path);
	set_int("gamemusic", "nsfplay_region", c->nsfplay_region, c->ini_path);
	set_int("gamemusic", "gsf_interpolation", c->gsf_interpolation, c->ini_path);
	set_int("gamemusic", "gsf_loops", c->gsf_loops, c->ini_path);
	set_int("gamemusic", "usf_hle_audio", c->usf_hle_audio, c->ini_path);
	{
		unsigned mask = 0;
		int i;
		for (i = 0; i < 32 && i < GC_MAX_CHANNELS; ++i)
			if (c->mute[i])
				mask |= (1u << i);
		set_int("gamemusic", "mute", (int)mask, c->ini_path);
		{
			char line[512];
			int n = 0, i;
			line[0] = 0;
			for (i = 0; i < GC_MAX_CHANNELS; ++i)
				n += snprintf(line + n, sizeof line - (size_t)n, "%s%d", i ? "," : "",
				              c->chan_vol[i]);
			set_str("gamemusic", "chan_vol", line, c->ini_path);
			n = 0;
			line[0] = 0;
			for (i = 0; i < GC_MAX_CHANNELS; ++i)
				n += snprintf(line + n, sizeof line - (size_t)n, "%s%d", i ? "," : "",
				              c->chan_pan[i]);
			set_str("gamemusic", "chan_pan", line, c->ini_path);
		}
	}
	set_int("gamemusic", "output_mono", c->output_mono, c->ini_path);
	set_int("gamemusic", "filter_mode", c->filter_mode, c->ini_path);
	set_int("gamemusic", "filter_hz", c->filter_hz, c->ini_path);
	set_int("gamemusic", "use_tag_length", c->use_tag_length, c->ini_path);
	set_int("gamemusic", "measure_untagged", c->measure_untagged, c->ini_path);
	set_int("gamemusic", "untagged_max_sec", c->untagged_max_sec, c->ini_path);
	set_int("gamemusic", "fatso_dmc_pop", c->fatso_dmc_pop, c->ini_path);
	set_int("gamemusic", "fatso_n106_pop", c->fatso_n106_pop, c->ini_path);
	set_int("gamemusic", "fatso_fds_pop", c->fatso_fds_pop, c->ini_path);
	set_int("gamemusic", "fatso_ignore_4011", c->fatso_ignore_4011, c->ini_path);
	set_int("gamemusic", "fatso_reset_duty", c->fatso_reset_duty, c->ini_path);
	set_int("gamemusic", "fatso_ignore_brk", c->fatso_ignore_brk, c->ini_path);
	set_int("gamemusic", "fatso_ignore_illegal", c->fatso_ignore_illegal, c->ini_path);
	set_int("gamemusic", "fatso_no_wait_play", c->fatso_no_wait_play, c->ini_path);
	set_int("gamemusic", "fatso_reset_regs", c->fatso_reset_regs, c->ini_path);
	set_int("gamemusic", "fatso_ignore_version", c->fatso_ignore_version, c->ini_path);
	set_int("gamemusic", "fatso_force_4017", c->fatso_force_4017, c->ini_path);
	set_int("gamemusic", "fatso_invert_hz", c->fatso_invert_hz, c->ini_path);
	set_int("gamemusic", "fatso_no_silence_if_time", c->fatso_no_silence_if_time, c->ini_path);
	set_int("gamemusic", "fatso_prepass", c->fatso_prepass, c->ini_path);
	set_int("gamemusic", "gbs_use_int", c->gbs_use_int, c->ini_path);
	set_int("gamemusic", "gbs_highpass", c->gbs_highpass, c->ini_path);
	set_int("gamemusic", "spc_interp", c->spc_interp, c->ini_path);
	return 1;
#else
	rewrite_ini(c);
	return 1;
#endif
}

gc_engine gc_config_engine_for(const gc_config *c, gc_format fmt)
{
	if (!c)
		return GC_ENG_AUTO;
	switch (fmt) {
	case GC_FMT_NSF:
	case GC_FMT_NSFE:
	case GC_FMT_NEZ:
	case GC_FMT_NSZ:
		return c->engine_nsf;
	case GC_FMT_GBS:
	case GC_FMT_GBR:
		return c->engine_gbs;
	case GC_FMT_KSS:
		return c->engine_kss;
	case GC_FMT_AY:
		return c->engine_ay;
	case GC_FMT_HES:
		return c->engine_hes;
	case GC_FMT_CPC:
		return c->engine_cpc;
	case GC_FMT_SGC:
		return c->engine_sgc;
	case GC_FMT_SPC:
	case GC_FMT_VGM:
	case GC_FMT_VGZ:
	case GC_FMT_GYM:
	case GC_FMT_RSN:
		return GC_ENG_GME;
	case GC_FMT_GSF:
	case GC_FMT_MINIGSF:
		return c->engine_gsf;
	case GC_FMT_USF:
	case GC_FMT_MINIUSF:
		return GC_ENG_USF;
	default:
		return GC_ENG_AUTO;
	}
}

int gc_config_untagged_cap_ms(const gc_config *c)
{
	int sec = c && c->untagged_max_sec > 0 ? c->untagged_max_sec : 600;
	int ms;
	if (sec < 10)
		sec = 10;
	if (sec > 600)
		sec = 600;
	ms = sec * 1000;
	if (ms > GC_CAP_MS)
		ms = GC_CAP_MS;
	return ms;
}

int gc_config_untagged_fallback_ms(const gc_config *c)
{
	int fade = c && c->fade_ms > 0 ? c->fade_ms : 0;
	int ms = gc_config_untagged_cap_ms(c) + fade;
	if (ms > GC_CAP_MS)
		ms = GC_CAP_MS;
	if (ms < 1)
		ms = GC_DEFAULT_PLAY_MS + fade;
	if (gc_is_dummy_length_ms(ms))
		ms += 1;
	return ms;
}
