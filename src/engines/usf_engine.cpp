#include "engine.h"
#include "psf_io.h"
#include "usf/usf.h"

#include <stdlib.h>
#include <string.h>
#include <strings.h>

struct usf_tags {
	char title[GC_MAX_TITLE];
	char artist[GC_MAX_TITLE];
	char game[GC_MAX_TITLE];
	char copyright[GC_MAX_TITLE];
	char comment[GC_MAX_TITLE];
	char lib[GC_MAX_TITLE];
	int length_ms;
	int fade_ms;
	int enable_compare;
	int enable_fifo;
};

struct usf_state_wrap {
	void *usf;
	int rate;
	int length_ms;
	usf_tags tags;
	int16_t *hold;
	int hold_frames;
	int hold_pos;
	char lib_path[GC_MAX_PATH];
};

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

static int usf_loader(void *context, const uint8_t *exe, size_t exe_size,
                      const uint8_t *reserved, size_t reserved_size)
{
	void *state = context;
	if (exe && exe_size > 0)
		return -1;
	if (!reserved || reserved_size == 0)
		return 0;
	return usf_upload_section(state, reserved, reserved_size);
}

static int usf_info_cb(void *context, const char *name, const char *value)
{
	usf_tags *t = (usf_tags *)context;
	if (!t || !name || !value)
		return 0;
	if (!strcasecmp(name, "title"))
		copy_field(t->title, sizeof t->title, value);
	else if (!strcasecmp(name, "artist"))
		copy_field(t->artist, sizeof t->artist, value);
	else if (!strcasecmp(name, "game") || !strcasecmp(name, "album"))
		copy_field(t->game, sizeof t->game, value);
	else if (!strcasecmp(name, "copyright") || !strcasecmp(name, "year"))
		copy_field(t->copyright, sizeof t->copyright, value);
	else if (!strcasecmp(name, "comment"))
		copy_field(t->comment, sizeof t->comment, value);
	else if (!strcasecmp(name, "_lib"))
		copy_field(t->lib, sizeof t->lib, value);
	else if (!strcasecmp(name, "length"))
		t->length_ms = gc_psf_parse_time_ms(value);
	else if (!strcasecmp(name, "fade"))
		t->fade_ms = gc_psf_parse_time_ms(value);
	else if (!strcasecmp(name, "_enablecompare") && value[0] && value[0] != '0')
		t->enable_compare = 1;
	else if (!strcasecmp(name, "_enablefifofull") && value[0] && value[0] != '0')
		t->enable_fifo = 1;
	return 0;
}

extern "C" {

static int gc_usf_can(gc_format fmt)
{
	return fmt == GC_FMT_USF || fmt == GC_FMT_MINIUSF;
}

static gc_eng_state *gc_usf_open(const unsigned char *data, size_t len, int rate,
                              const gc_config *cfg)
{
	usf_state_wrap *s;
	psf_file_callbacks cb;
	gc_vfs local;
	const gc_vfs *vfs;
	const char *uri = "tune.miniusf";
	int ver;
	if (!data || len < 16 || data[0] != 'P' || data[1] != 'S' || data[2] != 'F' ||
	    data[3] != 0x21)
		return NULL;
	if (rate < 8000)
		rate = 44100;
	memset(&local, 0, sizeof local);
	vfs = (cfg && cfg->vfs) ? cfg->vfs : NULL;
	if (!vfs) {
		unsigned char *copy = (unsigned char *)malloc(len);
		if (!copy)
			return NULL;
		memcpy(copy, data, len);
		gc_vfs_init(&local);
		if (cfg && cfg->vfs_dir[0])
			copy_field(local.dir, sizeof local.dir, cfg->vfs_dir);
		gc_vfs_add(&local, uri, copy, len);
		vfs = &local;
	} else if (cfg && cfg->vfs_uri[0])
		uri = cfg->vfs_uri;

	s = (usf_state_wrap *)calloc(1, sizeof *s);
	if (!s) {
		gc_vfs_free(&local);
		return NULL;
	}
	s->usf = malloc(usf_get_state_size());
	if (!s->usf) {
		free(s);
		gc_vfs_free(&local);
		return NULL;
	}
	usf_clear(s->usf);
	gc_psf_callbacks(&cb, vfs);
	ver = psf_load(uri, &cb, 0x21, usf_loader, s->usf, usf_info_cb, &s->tags, 1, NULL, NULL);
	if (ver <= 0) {
		usf_shutdown(s->usf);
		free(s->usf);
		free(s);
		gc_vfs_free(&local);
		return NULL;
	}
	usf_set_compare(s->usf, s->tags.enable_compare);
	usf_set_fifo_full(s->usf, s->tags.enable_fifo);
	/* HLE RSP gfx is always on in lazyusf2 (hle.hle_gfx=1) so Display Lists
	   fire DP interrupts (Mario Kart 64 re-rip, Yoshi's Story). HLE audio
	   is the public switch; default on like foo_input_usf 3.2. */
	usf_set_hle_audio(s->usf, (!cfg || cfg->usf_hle_audio) ? 1 : 0);
	s->rate = rate;
	s->length_ms = s->tags.length_ms;
	if (gc_is_dummy_length_ms(s->length_ms))
		s->length_ms = 0;
	if (s->length_ms > 0 && s->tags.fade_ms > 0)
		s->length_ms += s->tags.fade_ms;
	if (s->length_ms > GC_CAP_MS)
		s->length_ms = GC_CAP_MS;
	gc_psf_lib_path(s->lib_path, sizeof s->lib_path,
	                cfg ? cfg->vfs_dir : "", s->tags.lib);
	gc_vfs_free(&local);
	return (gc_eng_state *)s;
}

static void gc_usf_close(gc_eng_state *st)
{
	usf_state_wrap *s = (usf_state_wrap *)st;
	if (!s)
		return;
	if (s->usf) {
		usf_shutdown(s->usf);
		free(s->usf);
	}
	free(s->hold);
	free(s);
}

static int gc_usf_info(gc_eng_state *st, gc_info *out)
{
	usf_state_wrap *s = (usf_state_wrap *)st;
	if (!s || !out)
		return 0;
	memset(out, 0, sizeof *out);
	out->track_count = 1;
	out->start_track = 0;
	copy_field(out->game, sizeof out->game, s->tags.game);
	copy_field(out->artist, sizeof out->artist, s->tags.artist);
	copy_field(out->copyright, sizeof out->copyright, s->tags.copyright);
	copy_field(out->comment, sizeof out->comment, s->tags.comment);
	copy_field(out->system, sizeof out->system, "N64");
	copy_field(out->lib_name, sizeof out->lib_name, s->tags.lib);
	copy_field(out->lib_path, sizeof out->lib_path, s->lib_path);
	copy_field(out->extra, sizeof out->extra,
	           "lazyusf2 (kode54). HLE RSP gfx on (DP interrupt). "
	           "Not 64th Note / in_usf.dll. HCS / Adam Gashlin.");
	if (s->tags.length_ms > 0)
		copy_field(out->length_src, sizeof out->length_src, "PSF tag");
	copy_field(out->tracks[0].title, sizeof out->tracks[0].title,
	           s->tags.title[0] ? s->tags.title : s->tags.game);
	out->tracks[0].duration_ms = s->length_ms;
	out->tracks[0].fade_ms = s->tags.fade_ms > 0 ? s->tags.fade_ms : 0;
	return 1;
}

static int gc_usf_set_track(gc_eng_state *st, int track0)
{
	usf_state_wrap *s = (usf_state_wrap *)st;
	if (!s || !s->usf || track0 != 0)
		return -1;
	usf_restart(s->usf);
	s->hold_pos = 0;
	s->hold_frames = 0;
	return 0;
}

static int gc_usf_render(gc_eng_state *st, float *stereo, int frames)
{
	usf_state_wrap *s = (usf_state_wrap *)st;
	int16_t *tmp;
	int i, got = 0;
	int32_t sr = 0;
	const char *err;
	if (!s || !s->usf || !stereo || frames <= 0)
		return 0;
	tmp = (int16_t *)malloc((size_t)frames * 2u * sizeof(int16_t));
	if (!tmp)
		return 0;
	err = usf_render_resampled(s->usf, tmp, (size_t)frames, s->rate);
	if (err) {
		/* try native render + ignore rate */
		err = usf_render(s->usf, tmp, (size_t)frames, &sr);
		if (err) {
			free(tmp);
			return 0;
		}
	}
	for (i = 0; i < frames * 2; ++i)
		stereo[i] = (float)tmp[i] / 32768.0f;
	free(tmp);
	(void)got;
	return frames;
}

static int gc_usf_seek(gc_eng_state *st, int ms)
{
	usf_state_wrap *s = (usf_state_wrap *)st;
	int frames, left;
	if (!s || !s->usf)
		return -1;
	usf_restart(s->usf);
	if (ms <= 0)
		return 0;
	frames = (int)((int64_t)ms * s->rate / 1000);
	left = frames;
	while (left > 0) {
		int n = left > 1024 ? 1024 : left;
		const char *err = usf_render_resampled(s->usf, NULL, (size_t)n, s->rate);
		if (err)
			break;
		left -= n;
	}
	return ms;
}

static int gc_usf_len(gc_eng_state *st, int track0)
{
	usf_state_wrap *s = (usf_state_wrap *)st;
	(void)track0;
	return s ? s->length_ms : 0;
}

static int gc_usf_mute(gc_eng_state *st, const unsigned char *, const float *)
{
	(void)st;
	return 0;
}

static int gc_usf_voices(gc_eng_state *st)
{
	(void)st;
	return 0;
}

static const char *gc_usf_vname(gc_eng_state *st, int)
{
	(void)st;
	return "";
}

static const gc_eng_ops ops = {
	"lazyusf2",
	GC_ENG_USF,
	gc_usf_can,
	gc_usf_open,
	gc_usf_close,
	gc_usf_info,
	gc_usf_set_track,
	gc_usf_render,
	gc_usf_seek,
	gc_usf_len,
	gc_usf_mute,
	NULL,
	gc_usf_voices,
	gc_usf_vname,
	NULL,
	NULL,
	NULL,
	NULL
};

const gc_eng_ops *gc_eng_usf(void)
{
	return &ops;
}

} /* extern "C" */
