#include "engine.h"
#include "psf_io.h"
#include "ha_host.h"

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

struct ha_state {
	int rate;
	int native_rate;
	int length_ms;
	int fade_ms;
	gsf_tags tags;
	double src_pos;
	std::vector<int16_t> src;
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

static int pull_native(ha_state *s, int frames)
{
	int16_t tmp[2048];
	int got, need = frames * 2;
	while ((int)s->src.size() < need) {
		int want = (need - (int)s->src.size()) / 2;
		if (want > 1024)
			want = 1024;
		got = ha_core_render_i16(tmp, want);
		if (got <= 0)
			break;
		s->src.insert(s->src.end(), tmp, tmp + got * 2);
	}
	return (int)s->src.size() / 2;
}

extern "C" {

static int ha_can(gc_format fmt)
{
	return fmt == GC_FMT_GSF || fmt == GC_FMT_MINIGSF;
}

static gc_eng_state *ha_open(const unsigned char *data, size_t len, int rate,
                             const gc_config *cfg)
{
	ha_state *s;
	gsf_loader_state load;
	psf_file_callbacks cb;
	gc_vfs local;
	const gc_vfs *vfs;
	const char *uri = "tune.minigsf";
	int ver, loops;
	if (ha_core_busy())
		return NULL;
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
	s = new ha_state();
	s->rate = rate;
	s->native_rate = 44100;
	s->src_pos = 0;
	ver = psf_load(uri, &cb, 0x22, gsf_loader, &load, gsf_info_cb, &s->tags, 0,
	               NULL, NULL);
	if (ver <= 0 || !load.data || load.data_size < 4) {
		free(load.data);
		delete s;
		gc_vfs_free(&local);
		return NULL;
	}
	if (!ha_core_open(load.data, (int)load.data_size,
	                  cfg ? cfg->gsf_interpolation : 1)) {
		free(load.data);
		delete s;
		gc_vfs_free(&local);
		return NULL;
	}
	free(load.data);
	loops = (cfg && cfg->gsf_loops > 0) ? cfg->gsf_loops : 2;
	s->fade_ms = s->tags.fade_ms > 0 ? s->tags.fade_ms : 0;
	s->length_ms = gc_gsf_play_ms(s->tags.length_ms, s->fade_ms, loops);
	gc_psf_lib_path(s->lib_path, sizeof s->lib_path,
	                cfg ? cfg->vfs_dir : "", s->tags.lib);
	gc_vfs_free(&local);
	return (gc_eng_state *)s;
}

static void ha_close(gc_eng_state *st)
{
	ha_state *s = (ha_state *)st;
	if (!s)
		return;
	ha_core_close();
	delete s;
}

static int ha_info(gc_eng_state *st, gc_info *out)
{
	ha_state *s = (ha_state *)st;
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
	out->tracks[0].fade_ms = s->fade_ms;
	return 1;
}

static int ha_set_track(gc_eng_state *st, int track0)
{
	ha_state *s = (ha_state *)st;
	if (!s || track0 != 0)
		return -1;
	s->src.clear();
	s->src_pos = 0;
	ha_core_reset();
	return 0;
}

static int ha_render(gc_eng_state *st, float *stereo, int frames)
{
	ha_state *s = (ha_state *)st;
	int i;
	if (!s || !stereo || frames <= 0)
		return 0;
	if (s->rate == s->native_rate) {
		int16_t tmp[4096];
		int got, out = 0, left = frames;
		while (left > 0) {
			int n = left > 2048 ? 2048 : left;
			got = ha_core_render_i16(tmp, n);
			if (got <= 0)
				break;
			for (i = 0; i < got * 2; ++i)
				stereo[out * 2 + i] = (float)tmp[i] / 32768.0f;
			out += got;
			left -= got;
		}
		return out;
	}
	{
		double step = (double)s->native_rate / (double)s->rate;
		int need = (int)(s->src_pos + (double)frames * step) + 2;
		pull_native(s, need);
		if ((int)s->src.size() < 4)
			return 0;
		for (i = 0; i < frames; ++i) {
			int j = (int)s->src_pos;
			double t = s->src_pos - (double)j;
			int maxj = (int)s->src.size() / 2 - 2;
			if (j > maxj) {
				frames = i;
				break;
			}
			{
				float l0 = (float)s->src[(size_t)j * 2] / 32768.0f;
				float r0 = (float)s->src[(size_t)j * 2 + 1] / 32768.0f;
				float l1 = (float)s->src[(size_t)j * 2 + 2] / 32768.0f;
				float r1 = (float)s->src[(size_t)j * 2 + 3] / 32768.0f;
				stereo[i * 2] = (float)(l0 + (l1 - l0) * t);
				stereo[i * 2 + 1] = (float)(r0 + (r1 - r0) * t);
			}
			s->src_pos += step;
		}
		{
			int drop = (int)s->src_pos;
			if (drop > 0 && drop * 2 <= (int)s->src.size()) {
				s->src.erase(s->src.begin(), s->src.begin() + drop * 2);
				s->src_pos -= (double)drop;
			}
		}
		return frames;
	}
}

static int ha_seek(gc_eng_state *st, int ms)
{
	ha_state *s = (ha_state *)st;
	int frames, left;
	float dump[1024];
	if (!s)
		return -1;
	ha_core_reset();
	s->src.clear();
	s->src_pos = 0;
	if (ms <= 0)
		return 0;
	frames = (int)((int64_t)ms * s->rate / 1000);
	left = frames;
	while (left > 0) {
		int n = left > 512 ? 512 : left;
		int got = ha_render(st, dump, n);
		if (got <= 0)
			break;
		left -= got;
	}
	return ms;
}

static int ha_len(gc_eng_state *st, int track0)
{
	ha_state *s = (ha_state *)st;
	(void)track0;
	return s ? s->length_ms : 0;
}

static int ha_mute(gc_eng_state *st, const unsigned char mute[GC_MAX_CHANNELS],
                   const float level[GC_MAX_CHANNELS])
{
	int mask = 0x30f, i;
	(void)st;
	(void)level;
	if (!mute)
		return 0;
	for (i = 0; i < 6 && i < GC_MAX_CHANNELS; ++i) {
		if (mute[i]) {
			if (i < 4)
				mask &= ~(1 << i);
			else
				mask &= ~(0x100 << (i - 4));
		}
	}
	ha_core_set_enable(mask);
	return 1;
}

static int ha_voices(gc_eng_state *st)
{
	(void)st;
	return 6;
}

static const char *ha_vname(gc_eng_state *st, int i)
{
	static const char *n[] = { "SQ1", "SQ2", "WAVE", "NOISE", "FIFO A", "FIFO B" };
	(void)st;
	if (i < 0 || i >= 6)
		return "";
	return n[i];
}

static const gc_eng_ops ops = {
	"Highly Advanced",
	GC_ENG_HA,
	ha_can,
	ha_open,
	ha_close,
	ha_info,
	ha_set_track,
	ha_render,
	ha_seek,
	ha_len,
	ha_mute,
	NULL,
	ha_voices,
	ha_vname,
	NULL,
	NULL,
	NULL,
	NULL
};

const gc_eng_ops *gc_eng_ha(void)
{
	return &ops;
}

} /* extern "C" */
