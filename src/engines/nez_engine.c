#include "engine.h"

#include "nezplug.h"
#include "format/songinfo.h"

#include <stdlib.h>
#include <string.h>

extern unsigned char chmask[0x80];
extern struct {
	char *title;
	char *artist;
	char *copyright;
	char detail[1024];
} songinfodata;

int gc_nez_gbs_force_int;

typedef struct {
	NEZ_PLAY *nez;
	int rate;
	int track;
	int frames_played;
	int length_ms;
} nez_state;

static int nez_can(gc_format fmt)
{
	switch (fmt) {
	case GC_FMT_AY:
	case GC_FMT_GBS:
	case GC_FMT_GBR:
	case GC_FMT_HES:
	case GC_FMT_KSS:
	case GC_FMT_NSF:
	case GC_FMT_NSFE:
	case GC_FMT_NSD:
	case GC_FMT_SGC:
	case GC_FMT_CPC:
	case GC_FMT_NEZ:
	case GC_FMT_NSZ:
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

static gc_eng_state *nez_open(const unsigned char *data, size_t len, int rate,
                              const gc_config *cfg)
{
	nez_state *s;
	NEZ_PLAY *z;
	unsigned char *copy;
	gc_nez_gbs_force_int = cfg && cfg->gbs_use_int;
	if (!data || len < 8)
		return NULL;
	copy = (unsigned char *)malloc(len);
	if (!copy)
		return NULL;
	memcpy(copy, data, len);
	z = NEZNew();
	if (!z) {
		free(copy);
		return NULL;
	}
	if (NEZLoad(z, copy, (unsigned)len) != 0) {
		NEZDelete(z);
		free(copy);
		return NULL;
	}
	free(copy);
	if (rate < 8000)
		rate = 48000;
	NEZSetFrequency(z, (unsigned)rate);
	NEZSetChannel(z, 2);
	NEZReset(z);
	s = (nez_state *)calloc(1, sizeof *s);
	if (!s) {
		NEZDelete(z);
		return NULL;
	}
	s->nez = z;
	s->rate = rate;
	s->track = (int)NEZGetSongStart(z);
	if (s->track > 0)
		s->track -= 1;
	if (s->track < 0)
		s->track = 0;
	NEZSetSongNo(z, (unsigned)(s->track + 1));
	NEZReset(z);
	s->length_ms = 0;
	return (gc_eng_state *)s;
}

static void nez_close(gc_eng_state *st)
{
	nez_state *s = (nez_state *)st;
	if (!s)
		return;
	if (s->nez)
		NEZDelete(s->nez);
	free(s);
}

static int nez_info(gc_eng_state *st, gc_info *out)
{
	nez_state *s = (nez_state *)st;
	int n, i;
	if (!s || !s->nez || !out)
		return 0;
	memset(out, 0, sizeof *out);
	n = (int)NEZGetSongMax(s->nez);
	if (n < 1)
		n = 1;
	if (n > GC_MAX_TRACKS)
		n = GC_MAX_TRACKS;
	out->track_count = n;
	out->start_track = (int)NEZGetSongStart(s->nez);
	if (out->start_track > 0)
		out->start_track -= 1;
	copy_field(out->game, sizeof out->game, songinfodata.title);
	copy_field(out->artist, sizeof out->artist, songinfodata.artist);
	copy_field(out->copyright, sizeof out->copyright, songinfodata.copyright);
	copy_field(out->extra, sizeof out->extra, songinfodata.detail);
	for (i = 0; i < n; ++i)
		out->tracks[i].duration_ms = 0;
	return 1;
}

static int nez_set_track(gc_eng_state *st, int track0)
{
	nez_state *s = (nez_state *)st;
	int max;
	if (!s || !s->nez)
		return -1;
	max = (int)NEZGetSongMax(s->nez);
	if (max < 1)
		max = 1;
	if (track0 < 0 || track0 >= max)
		return -1;
	s->track = track0;
	s->frames_played = 0;
	NEZSetSongNo(s->nez, (unsigned)(track0 + 1));
	NEZReset(s->nez);
	return 0;
}

static int nez_render(gc_eng_state *st, float *stereo, int frames)
{
	nez_state *s = (nez_state *)st;
	short *tmp;
	int i;
	if (!s || !s->nez || !stereo || frames <= 0)
		return 0;
	/* Always stereo: NEZSetChannel(2) drives NESAudioRender. SONGINFO_GetChannel
	   can stay 1 (KSS/AY); using that for the buffer size overflows the heap. */
	tmp = (short *)malloc((size_t)frames * 2u * sizeof(short));
	if (!tmp)
		return 0;
	NEZRender(s->nez, tmp, (unsigned)frames);
	for (i = 0; i < frames; ++i) {
		stereo[i * 2] = (float)tmp[i * 2] / 32768.0f;
		stereo[i * 2 + 1] = (float)tmp[i * 2 + 1] / 32768.0f;
	}
	free(tmp);
	s->frames_played += frames;
	return frames;
}

static int nez_seek(gc_eng_state *st, int ms)
{
	nez_state *s = (nez_state *)st;
	int frames, left, got;
	float dump[1024];
	if (!s || !s->nez)
		return -1;
	NEZReset(s->nez);
	s->frames_played = 0;
	if (ms <= 0)
		return 0;
	frames = (int)((int64_t)ms * s->rate / 1000);
	left = frames;
	while (left > 0) {
		int n = left > 512 ? 512 : left;
		got = nez_render(st, dump, n);
		if (got <= 0)
			break;
		left -= got;
	}
	return ms;
}

static int nez_len(gc_eng_state *st, int track0)
{
	(void)st;
	(void)track0;
	return 0;
}

static int nez_mute(gc_eng_state *st, const unsigned char mute[GC_MAX_CHANNELS],
                    const float level[GC_MAX_CHANNELS])
{
	int i;
	(void)st;
	(void)level;
	if (!mute)
		return 0;
	for (i = 0; i < 0x80 && i < GC_MAX_CHANNELS; ++i)
		chmask[i] = mute[i] ? 0 : 1;
	return 1;
}

static int nez_voices(gc_eng_state *st)
{
	(void)st;
	return 8;
}

static const char *nez_vname(gc_eng_state *st, int i)
{
	static const char *n[] = { "PSG", "SCC", "YM2413", "Y8950", "CH5", "CH6", "CH7", "CH8" };
	(void)st;
	if (i < 0 || i >= 8)
		return "";
	return n[i];
}

static const gc_eng_ops ops = {
	"NEZplug++",
	GC_ENG_NEZ,
	nez_can,
	nez_open,
	nez_close,
	nez_info,
	nez_set_track,
	nez_render,
	nez_seek,
	nez_len,
	nez_mute,
	NULL,
	nez_voices,
	nez_vname,
	NULL,
	NULL,
	NULL,
	NULL
};

const gc_eng_ops *gc_eng_nez(void)
{
	return &ops;
}
