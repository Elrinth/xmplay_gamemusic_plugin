#include "engine.h"
#include "psf_io.h"

#include "GBA.h"
#include "Sound.h"

#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <vector>

struct gsf_loader_state {
	uint8_t *data;
	size_t data_size;
	uint32_t entry;
	int entry_set;
};

struct gsf_tags {
	char title[GC_MAX_TITLE];
	char artist[GC_MAX_TITLE];
	char game[GC_MAX_TITLE];
	char copyright[GC_MAX_TITLE];
	char comment[GC_MAX_TITLE];
	char lib[GC_MAX_TITLE];
	int length_ms;
	int fade_ms;
};

class GsfOut : public GBASoundOut {
public:
	std::vector<int16_t> buf;
	void write(const void *samples, unsigned long bytes) override
	{
		const int16_t *s = (const int16_t *)samples;
		size_t n = bytes / sizeof(int16_t);
		buf.insert(buf.end(), s, s + n);
	}
};

struct gsf_state {
	GBASystem *gba;
	GsfOut *out;
	int rate;
	int length_ms;
	gsf_tags tags;
	char lib_path[GC_MAX_PATH];
};

static uint32_t le32(const uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
	       ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
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

static int gsf_loader(void *context, const uint8_t *exe, size_t exe_size,
                      const uint8_t *reserved, size_t reserved_size)
{
	gsf_loader_state *st = (gsf_loader_state *)context;
	uint32_t xofs, xsize;
	size_t need;
	(void)reserved;
	(void)reserved_size;
	if (!st || !exe || exe_size < 12)
		return -1;
	if (!st->entry_set) {
		st->entry = le32(exe);
		st->entry_set = 1;
	}
	xofs = le32(exe + 4) & 0x1ffffffu;
	xsize = le32(exe + 8);
	if (xsize > exe_size - 12)
		return -1;
	need = (size_t)xofs + (size_t)xsize;
	if (!st->data) {
		size_t r = 1;
		while (r < need)
			r <<= 1;
		if (r < 0x8000)
			r = 0x8000;
		st->data = (uint8_t *)calloc(1, r);
		if (!st->data)
			return -1;
		st->data_size = r;
	} else if (st->data_size < need) {
		size_t r = st->data_size;
		uint8_t *n;
		while (r < need)
			r <<= 1;
		n = (uint8_t *)realloc(st->data, r);
		if (!n)
			return -1;
		memset(n + st->data_size, 0, r - st->data_size);
		st->data = n;
		st->data_size = r;
	}
	memcpy(st->data + xofs, exe + 12, xsize);
	return 0;
}

static int gsf_info_cb(void *context, const char *name, const char *value)
{
	gsf_tags *t = (gsf_tags *)context;
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
	return 0;
}

extern "C" {

static int gsf_can(gc_format fmt)
{
	return fmt == GC_FMT_GSF || fmt == GC_FMT_MINIGSF;
}

static gc_eng_state *gsf_open(const unsigned char *data, size_t len, int rate,
                              const gc_config *cfg)
{
	gsf_state *s;
	gsf_loader_state load;
	psf_file_callbacks cb;
	gc_vfs local;
	const gc_vfs *vfs;
	const char *uri = "tune.minigsf";
	int ver;
	if (!data || len < 16 || data[0] != 'P' || data[1] != 'S' || data[2] != 'F' ||
	    data[3] != 0x22)
		return NULL;
	if (rate < 8000)
		rate = 44100;
	memset(&load, 0, sizeof load);
	memset(&local, 0, sizeof local);
	vfs = (cfg && cfg->vfs) ? cfg->vfs : NULL;
	if (cfg && cfg->vfs_uri[0])
		uri = cfg->vfs_uri;
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
	}

	gc_psf_callbacks(&cb, vfs);
	s = (gsf_state *)calloc(1, sizeof *s);
	if (!s) {
		gc_vfs_free(&local);
		free(load.data);
		return NULL;
	}
	ver = psf_load(uri, &cb, 0x22, gsf_loader, &load, gsf_info_cb, &s->tags, 0, NULL, NULL);
	if (ver <= 0 || !load.data || load.data_size < 4) {
		free(load.data);
		free(s);
		gc_vfs_free(&local);
		return NULL;
	}
	s->gba = new GBASystem();
	s->out = new GsfOut();
	s->gba->soundInterpolation = cfg ? (cfg->gsf_interpolation ? true : false) : true;
	s->gba->cpuIsMultiBoot = false;
	if (!CPULoadRom(s->gba, load.data, (u32)load.data_size)) {
		delete s->gba;
		delete s->out;
		free(load.data);
		free(s);
		gc_vfs_free(&local);
		return NULL;
	}
	free(load.data);
	soundSetSampleRate(s->gba, rate);
	if (!soundInit(s->gba, s->out)) {
		CPUCleanUp(s->gba);
		delete s->gba;
		delete s->out;
		free(s);
		gc_vfs_free(&local);
		return NULL;
	}
	CPUInit(s->gba);
	CPUReset(s->gba);
	soundReset(s->gba);
	soundResume(s->gba);
	s->rate = rate;
	{
		int loops = (cfg && cfg->gsf_loops > 0) ? cfg->gsf_loops : 2;
		s->length_ms = gc_gsf_play_ms(s->tags.length_ms, s->tags.fade_ms, loops);
	}
	gc_psf_lib_path(s->lib_path, sizeof s->lib_path,
	                cfg ? cfg->vfs_dir : "", s->tags.lib);
	gc_vfs_free(&local);
	return (gc_eng_state *)s;
}

static void gsf_close(gc_eng_state *st)
{
	gsf_state *s = (gsf_state *)st;
	if (!s)
		return;
	if (s->gba) {
		soundShutdown(s->gba);
		CPUCleanUp(s->gba);
		delete s->gba;
	}
	delete s->out;
	free(s);
}

static int gsf_info(gc_eng_state *st, gc_info *out)
{
	gsf_state *s = (gsf_state *)st;
	if (!s || !out)
		return 0;
	memset(out, 0, sizeof *out);
	out->track_count = 1;
	out->start_track = 0;
	copy_field(out->game, sizeof out->game, s->tags.game);
	copy_field(out->artist, sizeof out->artist, s->tags.artist);
	copy_field(out->copyright, sizeof out->copyright, s->tags.copyright);
	copy_field(out->comment, sizeof out->comment, s->tags.comment);
	copy_field(out->system, sizeof out->system, "GBA");
	copy_field(out->lib_name, sizeof out->lib_name, s->tags.lib);
	copy_field(out->lib_path, sizeof out->lib_path, s->lib_path);
	if (s->tags.length_ms > 0)
		copy_field(out->length_src, sizeof out->length_src, "PSF tag");
	copy_field(out->tracks[0].title, sizeof out->tracks[0].title,
	           s->tags.title[0] ? s->tags.title : s->tags.game);
	out->tracks[0].duration_ms = s->length_ms;
	out->tracks[0].fade_ms = s->tags.fade_ms > 0 ? s->tags.fade_ms : 0;
	return 1;
}

static int gsf_set_track(gc_eng_state *st, int track0)
{
	gsf_state *s = (gsf_state *)st;
	if (!s || !s->gba || track0 != 0)
		return -1;
	s->out->buf.clear();
	CPUReset(s->gba);
	soundReset(s->gba);
	soundResume(s->gba);
	return 0;
}

static int gsf_render(gc_eng_state *st, float *stereo, int frames)
{
	gsf_state *s = (gsf_state *)st;
	int need, i, guard = 0;
	if (!s || !s->gba || !stereo || frames <= 0)
		return 0;
	need = frames * 2;
	while ((int)s->out->buf.size() < need && guard++ < 64)
		CPULoop(s->gba, 250000);
	if ((int)s->out->buf.size() < 2)
		return 0;
	if ((int)s->out->buf.size() < need)
		need = (int)s->out->buf.size() & ~1;
	frames = need / 2;
	for (i = 0; i < need; ++i)
		stereo[i] = (float)s->out->buf[(size_t)i] / 32768.0f;
	s->out->buf.erase(s->out->buf.begin(), s->out->buf.begin() + need);
	return frames;
}

static int gsf_seek(gc_eng_state *st, int ms)
{
	gsf_state *s = (gsf_state *)st;
	int frames, left;
	float dump[1024];
	if (!s || !s->gba)
		return -1;
	CPUReset(s->gba);
	soundReset(s->gba);
	soundResume(s->gba);
	s->out->buf.clear();
	if (ms <= 0)
		return 0;
	frames = (int)((int64_t)ms * s->rate / 1000);
	left = frames;
	while (left > 0) {
		int n = left > 512 ? 512 : left;
		int got = gsf_render(st, dump, n);
		if (got <= 0)
			break;
		left -= got;
	}
	return ms;
}

static int gsf_len(gc_eng_state *st, int track0)
{
	gsf_state *s = (gsf_state *)st;
	(void)track0;
	return s ? s->length_ms : 0;
}

static int gsf_mute(gc_eng_state *st, const unsigned char mute[GC_MAX_CHANNELS],
                    const float level[GC_MAX_CHANNELS])
{
	gsf_state *s = (gsf_state *)st;
	int mask = 0x3ff, i;
	(void)level;
	if (!s || !s->gba || !mute)
		return 0;
	for (i = 0; i < 10 && i < GC_MAX_CHANNELS; ++i) {
		if (mute[i])
			mask &= ~(1 << i);
	}
	soundSetEnable(s->gba, mask);
	return 1;
}

static int gsf_voices(gc_eng_state *st)
{
	(void)st;
	return 6;
}

static const char *gsf_vname(gc_eng_state *st, int i)
{
	static const char *n[] = { "SQ1", "SQ2", "WAVE", "NOISE", "FIFO A", "FIFO B" };
	(void)st;
	if (i < 0 || i >= 6)
		return "";
	return n[i];
}

static const gc_eng_ops ops = {
	"VIOGSF",
	GC_ENG_GSF,
	gsf_can,
	gsf_open,
	gsf_close,
	gsf_info,
	gsf_set_track,
	gsf_render,
	gsf_seek,
	gsf_len,
	gsf_mute,
	NULL,
	gsf_voices,
	gsf_vname,
	NULL,
	NULL,
	NULL,
	NULL
};

const gc_eng_ops *gc_eng_gsf(void)
{
	return &ops;
}

} /* extern "C" */
