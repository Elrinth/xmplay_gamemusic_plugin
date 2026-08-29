#include "engine.h"

#include "gme.h"

#include <stdlib.h>
#include <string.h>

#ifndef GME_DISABLE_STEREO_DEPTH
#define GME_DISABLE_STEREO_DEPTH 1
#endif

struct gme_state {
	Music_Emu *emu;
	int rate;
	int track;
	int track_count;
	int length_ms;
	int loops;
	int fade_ms;
};

extern "C" {

static int gme_can(gc_format fmt)
{
	switch (fmt) {
	case GC_FMT_AY:
	case GC_FMT_GBS:
	case GC_FMT_GYM:
	case GC_FMT_HES:
	case GC_FMT_KSS:
	case GC_FMT_NSF:
	case GC_FMT_NSFE:
	case GC_FMT_SGC:
	case GC_FMT_SPC:
	case GC_FMT_VGM:
	case GC_FMT_VGZ:
	case GC_FMT_CPC:
	case GC_FMT_RSN:
		return 1;
	default:
		return 0;
	}
}

static void copy_field(char *dst, size_t cap, const char *src)
{
	size_t n;
	if (!dst || cap == 0)
		return;
	dst[0] = '\0';
	if (!src)
		return;
	n = strlen(src);
	if (n >= cap)
		n = cap - 1;
	memcpy(dst, src, n);
	dst[n] = '\0';
}

static int real_length_ms(gme_info_t *ti, int loop_count, int fade_ms)
{
	if (!ti)
		return 0;
	if (ti->length > 0 && ti->length < GC_CAP_MS &&
	    !gc_is_dummy_length_ms(ti->length))
		return ti->length + (fade_ms > 0 ? fade_ms : 0);
	if (ti->intro_length >= 0 && ti->loop_length > 0) {
		int loops = loop_count < 1 ? 1 : loop_count;
		int ms = ti->intro_length + ti->loop_length * loops;
		if (fade_ms > 0)
			ms += fade_ms;
		if (ms > GC_CAP_MS)
			ms = GC_CAP_MS;
		return ms;
	}
	/* Do not use GME's 150000 play_length dummy. */
	return 0;
}

static gc_eng_state *gme_open_impl(const unsigned char *data, size_t len, int rate,
                                   const gc_config *cfg)
{
	gme_state *s;
	Music_Emu *emu = NULL;
	gme_err_t err;
	int loops, fade;
	if (!data || len < 4)
		return NULL;
	if (rate < 8000)
		rate = 48000;
	/* Reject SAP even if the bytes look like it. */
	if (len >= 4 && data[0] == 'S' && data[1] == 'A' && data[2] == 'P' && data[3] == 0x0D)
		return NULL;
	err = gme_open_data(data, (long)len, &emu, rate);
	if (err || !emu)
		return NULL;
	gme_enable_accuracy(emu, (cfg && cfg->spc_interp == 0) ? 0 : 1);
	/* Never apply GME stereo echo / reverb. */
	gme_set_stereo_depth(emu, 0.0);
	loops = cfg ? cfg->loop_count : 1;
	fade = cfg ? cfg->fade_ms : GC_DEFAULT_FADE_MS;
	s = (gme_state *)calloc(1, sizeof *s);
	if (!s) {
		gme_delete(emu);
		return NULL;
	}
	s->emu = emu;
	s->rate = rate;
	s->loops = loops;
	s->fade_ms = fade;
	s->track_count = gme_track_count(emu);
	if (s->track_count < 1)
		s->track_count = 1;
	/* Inclusive last track — the old xmp-gme bug dropped N. */
	s->track = 0;
	{
		gme_info_t *ti = NULL;
		if (!gme_track_info(emu, &ti, 0) && ti) {
			s->length_ms = real_length_ms(ti, loops, fade);
			gme_free_info(ti);
		}
	}
	if (s->length_ms > 0)
		gme_set_fade_msecs(emu, s->length_ms > fade ? s->length_ms - fade : 0, fade);
	err = gme_start_track(emu, 0);
	if (err) {
		gme_delete(emu);
		free(s);
		return NULL;
	}
	return (gc_eng_state *)s;
}

static void gme_close_impl(gc_eng_state *st)
{
	gme_state *s = (gme_state *)st;
	if (!s)
		return;
	if (s->emu)
		gme_delete(s->emu);
	free(s);
}

static int gme_info_impl(gc_eng_state *st, gc_info *out)
{
	gme_state *s = (gme_state *)st;
	int i, n;
	if (!s || !s->emu || !out)
		return 0;
	memset(out, 0, sizeof *out);
	n = gme_track_count(s->emu);
	if (n < 1)
		n = 1;
	if (n > GC_MAX_TRACKS)
		n = GC_MAX_TRACKS;
	out->track_count = n;
	out->start_track = 0;
	for (i = 0; i < n; ++i) {
		gme_info_t *ti = NULL;
		if (gme_track_info(s->emu, &ti, i) || !ti)
			continue;
		if (i == 0) {
			copy_field(out->game, sizeof out->game, ti->game);
			copy_field(out->artist, sizeof out->artist, ti->author);
			copy_field(out->copyright, sizeof out->copyright, ti->copyright);
			copy_field(out->comment, sizeof out->comment, ti->comment);
			copy_field(out->system, sizeof out->system, ti->system);
		}
		copy_field(out->tracks[i].title, sizeof out->tracks[i].title, ti->song);
		out->tracks[i].duration_ms = real_length_ms(ti, s->loops, s->fade_ms);
		if (i == 0 && out->tracks[i].duration_ms > 0)
			copy_field(out->length_src, sizeof out->length_src, "embedded/tag");
		gme_free_info(ti);
	}
	return 1;
}

static int gme_set_track_impl(gc_eng_state *st, int track0)
{
	gme_state *s = (gme_state *)st;
	int n;
	if (!s || !s->emu)
		return -1;
	n = gme_track_count(s->emu);
	if (n < 1)
		n = 1;
	if (track0 < 0 || track0 >= n)
		return -1;
	s->track = track0;
	if (gme_start_track(s->emu, track0))
		return -1;
	{
		gme_info_t *ti = NULL;
		if (!gme_track_info(s->emu, &ti, track0) && ti) {
			s->length_ms = real_length_ms(ti, s->loops, s->fade_ms);
			gme_free_info(ti);
		}
	}
	if (s->length_ms > 0)
		gme_set_fade_msecs(s->emu,
		                   s->length_ms > s->fade_ms ? s->length_ms - s->fade_ms : 0,
		                   s->fade_ms);
	return 0;
}

static int gme_render_impl(gc_eng_state *st, float *stereo, int frames)
{
	gme_state *s = (gme_state *)st;
	short *tmp;
	int i;
	if (!s || !s->emu || !stereo || frames <= 0)
		return 0;
	if (gme_track_ended(s->emu))
		return 0;
	tmp = (short *)malloc((size_t)frames * 2u * sizeof(short));
	if (!tmp)
		return 0;
	if (gme_play(s->emu, frames * 2, tmp)) {
		free(tmp);
		return 0;
	}
	for (i = 0; i < frames * 2; ++i)
		stereo[i] = (float)tmp[i] / 32768.0f;
	free(tmp);
	return frames;
}

static int gme_seek_impl(gc_eng_state *st, int ms)
{
	gme_state *s = (gme_state *)st;
	if (!s || !s->emu)
		return -1;
	if (gme_seek(s->emu, ms < 0 ? 0 : ms))
		return -1;
	return ms < 0 ? 0 : ms;
}

static int gme_len_impl(gc_eng_state *st, int track0)
{
	gme_state *s = (gme_state *)st;
	gme_info_t *ti = NULL;
	int ms;
	if (!s || !s->emu)
		return 0;
	if (gme_track_info(s->emu, &ti, track0) || !ti)
		return s->length_ms;
	ms = real_length_ms(ti, s->loops, s->fade_ms);
	gme_free_info(ti);
	return ms;
}

static int gme_mute_impl(gc_eng_state *st, const unsigned char mute[GC_MAX_CHANNELS],
                         const float level[GC_MAX_CHANNELS])
{
	gme_state *s = (gme_state *)st;
	int i, n, mask = 0;
	(void)level;
	if (!s || !s->emu || !mute)
		return 0;
	n = gme_voice_count(s->emu);
	for (i = 0; i < n && i < GC_MAX_CHANNELS; ++i) {
		if (mute[i])
			mask |= (1 << i);
	}
	gme_mute_voices(s->emu, mask);
	return 1;
}

static int gme_voices_impl(gc_eng_state *st)
{
	gme_state *s = (gme_state *)st;
	if (!s || !s->emu)
		return 0;
	return gme_voice_count(s->emu);
}

static const char *gme_vname_impl(gc_eng_state *st, int i)
{
	gme_state *s = (gme_state *)st;
	if (!s || !s->emu)
		return "";
	return gme_voice_name(s->emu, i);
}

static const gc_eng_ops ops = {
	"Game_Music_Emu",
	GC_ENG_GME,
	gme_can,
	gme_open_impl,
	gme_close_impl,
	gme_info_impl,
	gme_set_track_impl,
	gme_render_impl,
	gme_seek_impl,
	gme_len_impl,
	gme_mute_impl,
	NULL,
	gme_voices_impl,
	gme_vname_impl,
	NULL
};

const gc_eng_ops *gc_eng_gme(void)
{
	return &ops;
}

} /* extern "C" */
