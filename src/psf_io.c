#include "psf_io.h"
#include "gamechip.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

typedef struct {
	const unsigned char *data;
	size_t len;
	size_t pos;
	int heap; /* 1 if we own data */
} gc_psf_fh;

static void *psf_fopen(void *context, const char *path)
{
	const gc_vfs *vfs = (const gc_vfs *)context;
	const gc_vfs_file *f;
	unsigned char *disk = NULL;
	size_t disk_len = 0;
	gc_psf_fh *h;
	if (!path)
		return NULL;
	f = gc_vfs_find(vfs, path);
	if (f) {
		h = (gc_psf_fh *)calloc(1, sizeof *h);
		if (!h)
			return NULL;
		h->data = f->data;
		h->len = f->len;
		h->heap = 0;
		return h;
	}
	if (gc_vfs_load_dir_file(vfs, path, &disk, &disk_len) && disk) {
		h = (gc_psf_fh *)calloc(1, sizeof *h);
		if (!h) {
			free(disk);
			return NULL;
		}
		h->data = disk;
		h->len = disk_len;
		h->heap = 1;
		return h;
	}
	return NULL;
}

static size_t psf_fread(void *buffer, size_t size, size_t count, void *handle)
{
	gc_psf_fh *h = (gc_psf_fh *)handle;
	size_t want, rem;
	if (!h || !buffer || size == 0)
		return 0;
	want = size * count;
	rem = (h->pos < h->len) ? (h->len - h->pos) : 0;
	if (want > rem)
		want = rem;
	if (want)
		memcpy(buffer, h->data + h->pos, want);
	h->pos += want;
	return size ? (want / size) : 0;
}

static int psf_fseek(void *handle, int64_t off, int whence)
{
	gc_psf_fh *h = (gc_psf_fh *)handle;
	int64_t n;
	if (!h)
		return -1;
	if (whence == SEEK_SET)
		n = off;
	else if (whence == SEEK_CUR)
		n = (int64_t)h->pos + off;
	else if (whence == SEEK_END)
		n = (int64_t)h->len + off;
	else
		return -1;
	if (n < 0)
		return -1;
	h->pos = (size_t)n;
	return 0;
}

static int psf_fclose(void *handle)
{
	gc_psf_fh *h = (gc_psf_fh *)handle;
	if (!h)
		return -1;
	if (h->heap)
		free((void *)h->data);
	free(h);
	return 0;
}

static long psf_ftell(void *handle)
{
	gc_psf_fh *h = (gc_psf_fh *)handle;
	if (!h)
		return -1;
	return (long)h->pos;
}

void gc_psf_callbacks(psf_file_callbacks *cb, const gc_vfs *vfs)
{
	if (!cb)
		return;
	memset(cb, 0, sizeof *cb);
	cb->path_separators = "\\/:";
	cb->context = (void *)vfs;
	cb->fopen = psf_fopen;
	cb->fread = psf_fread;
	cb->fseek = psf_fseek;
	cb->fclose = psf_fclose;
	cb->ftell = psf_ftell;
}

int gc_psf_parse_time_ms(const char *s)
{
	int min = 0, sec = 0, frac = 0, n;
	double t;
	char *end;
	if (!s || !s[0])
		return 0;
	if (strchr(s, ':')) {
		n = sscanf(s, "%d:%d.%d", &min, &sec, &frac);
		if (n >= 2) {
			int ms = min * 60000 + sec * 1000;
			if (n == 3) {
				if (frac < 10)
					ms += frac * 100;
				else if (frac < 100)
					ms += frac * 10;
				else
					ms += frac;
			}
			return ms > 0 ? ms : 0;
		}
		return 0;
	}
	t = strtod(s, &end);
	if (end == s || t <= 0)
		return 0;
	if (t < 60.0)
		return (int)(t * 1000.0);
	return (int)(t * 1000.0);
}

int gc_gsf_play_ms(int tag_length_ms, int tag_fade_ms, int loops)
{
	int play;
	if (gc_is_dummy_length_ms(tag_length_ms) || tag_length_ms == 124000)
		tag_length_ms = 0;
	if (tag_length_ms <= 0)
		return 0;
	if (loops < 1)
		loops = 2;
	if (loops > 16)
		loops = 16;
	play = (int)((int64_t)tag_length_ms * loops / 2);
	if (tag_fade_ms > 0)
		play += tag_fade_ms;
	if (play > GC_CAP_MS)
		play = GC_CAP_MS;
	return play;
}

void gc_psf_lib_path(char *out, size_t cap, const char *dir, const char *lib)
{
	const char *base;
	size_t n;
	if (!out || cap == 0)
		return;
	out[0] = '\0';
	if (!lib || !lib[0])
		return;
	base = lib;
	for (n = 0; lib[n]; ++n) {
		if (lib[n] == '/' || lib[n] == '\\')
			base = lib + n + 1;
	}
	if (dir && dir[0]) {
		snprintf(out, cap, "%s%s", dir, base);
		return;
	}
	n = strlen(lib);
	if (n >= cap)
		n = cap - 1;
	memcpy(out, lib, n);
	out[n] = '\0';
}
