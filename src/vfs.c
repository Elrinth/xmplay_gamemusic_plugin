#include "vfs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *basename_of(const char *path)
{
	const char *s = path, *p;
	if (!path)
		return "";
	for (p = path; *p; ++p) {
		if (*p == '/' || *p == '\\')
			s = p + 1;
	}
	return s;
}

static int ieq(const char *a, const char *b)
{
	if (!a || !b)
		return 0;
	while (*a && *b) {
		unsigned char ca = (unsigned char)*a++;
		unsigned char cb = (unsigned char)*b++;
		if (ca >= 'A' && ca <= 'Z') ca = (unsigned char)(ca - 'A' + 'a');
		if (cb >= 'A' && cb <= 'Z') cb = (unsigned char)(cb - 'A' + 'a');
		if (ca != cb)
			return 0;
	}
	return *a == 0 && *b == 0;
}

void gc_vfs_init(gc_vfs *v)
{
	if (!v)
		return;
	memset(v, 0, sizeof *v);
}

void gc_vfs_free(gc_vfs *v)
{
	int i;
	if (!v)
		return;
	for (i = 0; i < v->count; ++i)
		free(v->files[i].data);
	memset(v, 0, sizeof *v);
}

int gc_vfs_add(gc_vfs *v, const char *name, unsigned char *data, size_t len)
{
	gc_vfs_file *f;
	if (!v || v->count >= GC_MAX_VFS || !data || len == 0)
		return 0;
	f = &v->files[v->count];
	memset(f, 0, sizeof *f);
	if (name && name[0]) {
		size_t n = strlen(name);
		if (n >= sizeof f->name)
			n = sizeof f->name - 1;
		memcpy(f->name, name, n);
		f->name[n] = '\0';
	}
	f->data = data;
	f->len = len;
	v->count++;
	return 1;
}

const gc_vfs_file *gc_vfs_find(const gc_vfs *v, const char *name)
{
	const char *base;
	int i;
	if (!v || !name || !name[0])
		return NULL;
	base = basename_of(name);
	for (i = 0; i < v->count; ++i) {
		if (ieq(basename_of(v->files[i].name), base))
			return &v->files[i];
	}
	return NULL;
}

void gc_vfs_set_dir_from_path(gc_vfs *v, const char *path)
{
	const char *base;
	size_t n;
	if (!v)
		return;
	v->dir[0] = '\0';
	if (!path || !path[0])
		return;
	base = basename_of(path);
	n = (size_t)(base - path);
	if (n >= sizeof v->dir)
		n = sizeof v->dir - 1;
	if (n)
		memcpy(v->dir, path, n);
	v->dir[n] = '\0';
}

int gc_vfs_load_dir_file(const gc_vfs *v, const char *name,
                         unsigned char **out, size_t *out_len)
{
	char path[GC_MAX_PATH * 2];
	FILE *f;
	long sz;
	unsigned char *buf;
	if (out) *out = NULL;
	if (out_len) *out_len = 0;
	if (!v || !v->dir[0] || !name || !name[0])
		return 0;
	snprintf(path, sizeof path, "%s%s", v->dir, basename_of(name));
	f = fopen(path, "rb");
	if (!f)
		return 0;
	if (fseek(f, 0, SEEK_END) != 0) {
		fclose(f);
		return 0;
	}
	sz = ftell(f);
	if (sz <= 0 || (size_t)sz > GC_MAX_MODULE) {
		fclose(f);
		return 0;
	}
	rewind(f);
	buf = (unsigned char *)malloc((size_t)sz);
	if (!buf) {
		fclose(f);
		return 0;
	}
	if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
		free(buf);
		fclose(f);
		return 0;
	}
	fclose(f);
	*out = buf;
	*out_len = (size_t)sz;
	return 1;
}
