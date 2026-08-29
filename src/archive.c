#include "archive.h"
#include "probe.h"
#include "vfs.h"

#define MINIZ_NO_STDIO
#define MINIZ_NO_ARCHIVE_WRITING_APIS
#define MINIZ_NO_TIME
#include "miniz.h"
#include "miniz.c"
#include "miniz_tdef.c"
#include "miniz_tinfl.c"
#include "miniz_zip.c"

#include <stdlib.h>
#include <string.h>

int gc_inflate_gzip(const unsigned char *in, size_t in_len, unsigned char **out, size_t *out_len)
{
	size_t off;
	unsigned flags;
	unsigned char *dst;
	size_t dst_len = 0;

	if (out) *out = NULL;
	if (out_len) *out_len = 0;
	if (!in || in_len < 18 || in[0] != 0x1F || in[1] != 0x8B || in[2] != 8)
		return 0;
	flags = in[3];
	off = 10;
	if (flags & 4) { /* extra */
		if (off + 2 > in_len) return 0;
		off += 2u + (unsigned)in[off] + ((unsigned)in[off + 1] << 8);
	}
	if (flags & 8) { /* name */
		while (off < in_len && in[off])
			off++;
		off++;
	}
	if (flags & 16) {
		while (off < in_len && in[off])
			off++;
		off++;
	}
	if (flags & 2)
		off += 2;
	if (off + 8 >= in_len)
		return 0;

	dst = (unsigned char *)tinfl_decompress_mem_to_heap(in + off, in_len - off - 8,
	                                                    &dst_len, 0);
	if (!dst || dst_len == 0) {
		free(dst);
		return 0;
	}
	*out = dst;
	*out_len = dst_len;
	return 1;
}

static int name_is_m3u(const char *n)
{
	size_t L;
	if (!n) return 0;
	L = strlen(n);
	if (L < 4) return 0;
	return (n[L - 4] == '.' &&
	        (n[L - 3] == 'm' || n[L - 3] == 'M') &&
	        (n[L - 2] == '3') &&
	        (n[L - 1] == 'u' || n[L - 1] == 'U'));
}

int gc_zip_extract_music(const unsigned char *zip, size_t zip_len,
                         gc_blob *music, gc_blob *m3u)
{
	mz_zip_archive ar;
	mz_uint i, n;
	int found_music = 0;

	if (music) memset(music, 0, sizeof *music);
	if (m3u) memset(m3u, 0, sizeof *m3u);
	if (!zip || zip_len < 4)
		return 0;

	memset(&ar, 0, sizeof ar);
	if (!mz_zip_reader_init_mem(&ar, zip, zip_len, 0))
		return 0;
	n = mz_zip_reader_get_num_files(&ar);
	for (i = 0; i < n; ++i) {
		mz_zip_archive_file_stat st;
		char name[GC_MAX_PATH];
		if (!mz_zip_reader_file_stat(&ar, i, &st) || st.m_is_directory)
			continue;
		name[0] = '\0';
		mz_zip_reader_get_filename(&ar, i, name, sizeof name);
		if (name_is_m3u(name) && m3u && !m3u->data) {
			size_t sz = 0;
			void *p = mz_zip_reader_extract_to_heap(&ar, i, &sz, 0);
			if (p && sz) {
				m3u->data = (unsigned char *)p;
				m3u->len = sz;
				memcpy(m3u->name, name, sizeof m3u->name);
			} else {
				free(p);
			}
			continue;
		}
		if (gc_is_psf_lib(name))
			continue;
		if (!found_music) {
			size_t sz = 0;
			void *p = mz_zip_reader_extract_to_heap(&ar, i, &sz, 0);
			gc_format fmt;
			if (!p || sz < 4) {
				free(p);
				continue;
			}
			fmt = gc_probe((const unsigned char *)p, sz, name);
			if (gc_format_claimed(fmt) && fmt != GC_FMT_ZIP &&
			    fmt != GC_FMT_SEVENZ && fmt != GC_FMT_GZIP) {
				music->data = (unsigned char *)p;
				music->len = sz;
				memcpy(music->name, name, sizeof music->name);
				found_music = 1;
			} else {
				free(p);
			}
		}
	}
	mz_zip_reader_end(&ar);
	return found_music;
}

static int same_stem(const char *a, const char *b)
{
	const char *da, *db, *pa, *pb;
	if (!a || !b)
		return 0;
	da = a;
	db = b;
	for (pa = a; *pa; ++pa)
		if (*pa == '/' || *pa == '\\')
			da = pa + 1;
	for (pb = b; *pb; ++pb)
		if (*pb == '/' || *pb == '\\')
			db = pb + 1;
	while (*da && *db && *da != '.' && *db != '.') {
		unsigned char ca = (unsigned char)*da++;
		unsigned char cb = (unsigned char)*db++;
		if (ca >= 'A' && ca <= 'Z') ca = (unsigned char)(ca - 'A' + 'a');
		if (cb >= 'A' && cb <= 'Z') cb = (unsigned char)(cb - 'A' + 'a');
		if (ca != cb)
			return 0;
	}
	return (*da == '.' || *da == 0) && (*db == '.' || *db == 0);
}

int gc_vfs_pick_music(const gc_vfs *vfs, gc_blob *music, gc_blob *m3u)
{
	int i, found = 0, m3u_i = -1, sib = -1;
	if (music)
		memset(music, 0, sizeof *music);
	if (m3u)
		memset(m3u, 0, sizeof *m3u);
	if (!vfs)
		return 0;
	for (i = 0; i < vfs->count; ++i) {
		gc_format fmt = gc_probe(vfs->files[i].data, vfs->files[i].len,
		                         vfs->files[i].name);
		if (name_is_m3u(vfs->files[i].name)) {
			if (m3u_i < 0)
				m3u_i = i;
			continue;
		}
		if (gc_is_psf_lib(vfs->files[i].name))
			continue;
		if (!found && gc_format_claimed(fmt) && fmt != GC_FMT_ZIP &&
		    fmt != GC_FMT_SEVENZ && fmt != GC_FMT_GZIP) {
			music->data = (unsigned char *)malloc(vfs->files[i].len);
			if (!music->data)
				return 0;
			memcpy(music->data, vfs->files[i].data, vfs->files[i].len);
			music->len = vfs->files[i].len;
			memcpy(music->name, vfs->files[i].name, sizeof music->name);
			found = 1;
		}
	}
	if (!found)
		return 0;
	for (i = 0; i < vfs->count; ++i) {
		if (name_is_m3u(vfs->files[i].name) && same_stem(vfs->files[i].name, music->name)) {
			sib = i;
			break;
		}
	}
	if (sib < 0)
		sib = m3u_i;
	if (sib >= 0 && m3u) {
		m3u->data = (unsigned char *)malloc(vfs->files[sib].len);
		if (m3u->data) {
			memcpy(m3u->data, vfs->files[sib].data, vfs->files[sib].len);
			m3u->len = vfs->files[sib].len;
			memcpy(m3u->name, vfs->files[sib].name, sizeof m3u->name);
		}
	}
	return 1;
}

static int zip_extract_all(const unsigned char *zip, size_t zip_len, gc_vfs *vfs)
{
	mz_zip_archive ar;
	mz_uint i, n;
	if (!vfs || !zip || zip_len < 4)
		return 0;
	memset(&ar, 0, sizeof ar);
	if (!mz_zip_reader_init_mem(&ar, zip, zip_len, 0))
		return 0;
	n = mz_zip_reader_get_num_files(&ar);
	for (i = 0; i < n; ++i) {
		mz_zip_archive_file_stat st;
		char name[GC_MAX_PATH];
		size_t sz = 0;
		void *p;
		if (!mz_zip_reader_file_stat(&ar, i, &st) || st.m_is_directory)
			continue;
		name[0] = '\0';
		mz_zip_reader_get_filename(&ar, i, name, sizeof name);
		p = mz_zip_reader_extract_to_heap(&ar, i, &sz, 0);
		if (!p || !sz) {
			free(p);
			continue;
		}
		if (!gc_vfs_add(vfs, name, (unsigned char *)p, sz))
			free(p);
	}
	mz_zip_reader_end(&ar);
	return vfs->count > 0;
}

int gc_archive_extract_all(const unsigned char *data, size_t len, gc_vfs *vfs)
{
	if (!data || len < 6 || !vfs)
		return 0;
	if (data[0] == 'P' && data[1] == 'K')
		return zip_extract_all(data, len, vfs);
	if (data[0] == '7' && data[1] == 'z')
		return gc_sevenz_extract_all(data, len, vfs);
	return 0;
}

int gc_archive_extract_music(const unsigned char *data, size_t len,
                             gc_blob *music, gc_blob *m3u)
{
	gc_vfs vfs;
	int ok;
	gc_vfs_init(&vfs);
	if (!gc_archive_extract_all(data, len, &vfs)) {
		gc_vfs_free(&vfs);
		return 0;
	}
	ok = gc_vfs_pick_music(&vfs, music, m3u);
	gc_vfs_free(&vfs);
	return ok;
}
