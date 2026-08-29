/* In-process 7z (LZMA SDK, public domain). Sibling .m3u is read here. */
#include "archive.h"
#include "probe.h"

#include "7z.h"
#include "7zAlloc.h"
#include "7zCrc.h"

#include <stdlib.h>
#include <string.h>

#define SEVENZ_LOOK ((size_t)1 << 18)

typedef struct {
	ISeekInStream vt;
	const Byte *data;
	size_t size;
	size_t pos;
} MemSeek;

static SRes mem_read(ISeekInStreamPtr pp, void *buf, size_t *size)
{
	MemSeek *p = Z7_CONTAINER_FROM_VTBL(pp, MemSeek, vt);
	size_t rem, n;
	if (!p || !size)
		return SZ_ERROR_PARAM;
	rem = (p->pos < p->size) ? (p->size - p->pos) : 0;
	n = *size;
	if (n > rem)
		n = rem;
	if (n && buf)
		memcpy(buf, p->data + p->pos, n);
	p->pos += n;
	*size = n;
	return SZ_OK;
}

static SRes mem_seek(ISeekInStreamPtr pp, Int64 *pos, ESzSeek origin)
{
	MemSeek *p = Z7_CONTAINER_FROM_VTBL(pp, MemSeek, vt);
	Int64 n;
	if (!p || !pos)
		return SZ_ERROR_PARAM;
	if (origin == SZ_SEEK_SET)
		n = *pos;
	else if (origin == SZ_SEEK_CUR)
		n = (Int64)p->pos + *pos;
	else if (origin == SZ_SEEK_END)
		n = (Int64)p->size + *pos;
	else
		return SZ_ERROR_PARAM;
	if (n < 0)
		return SZ_ERROR_PARAM;
	p->pos = (size_t)n;
	*pos = n;
	return SZ_OK;
}

static int utf16_to_ascii(const UInt16 *in, size_t n, char *out, size_t cap)
{
	size_t i, w = 0;
	if (!out || cap == 0)
		return 0;
	for (i = 0; i < n && w + 1 < cap; ++i) {
		unsigned c = in[i];
		if (c == 0)
			break;
		out[w++] = (c < 128) ? (char)c : '_';
	}
	out[w] = '\0';
	return 1;
}

static int name_is_m3u(const char *n)
{
	size_t L;
	if (!n)
		return 0;
	L = strlen(n);
	if (L < 4)
		return 0;
	return n[L - 4] == '.' &&
	       (n[L - 3] == 'm' || n[L - 3] == 'M') &&
	       n[L - 2] == '3' &&
	       (n[L - 1] == 'u' || n[L - 1] == 'U');
}

int gc_sevenz_extract_music(const unsigned char *in, size_t in_len,
                            gc_blob *music, gc_blob *m3u)
{
	static int crc_ok;
	MemSeek mem;
	CLookToRead2 look;
	CSzArEx db;
	ISzAlloc alloc = { SzAlloc, SzFree };
	Byte *lookbuf = NULL;
	UInt32 blockIndex = 0xFFFFFFFF;
	Byte *outBuf = NULL;
	size_t outBufSize = 0;
	UInt32 i;
	int found = 0;

	if (music)
		memset(music, 0, sizeof *music);
	if (m3u)
		memset(m3u, 0, sizeof *m3u);
	if (!in || in_len < 32)
		return 0;
	if (!(in[0] == '7' && in[1] == 'z' && in[2] == 0xBC &&
	      in[3] == 0xAF && in[4] == 0x27 && in[5] == 0x1C))
		return 0;

	if (!crc_ok) {
		CrcGenerateTable();
		crc_ok = 1;
	}

	lookbuf = (Byte *)malloc(SEVENZ_LOOK);
	if (!lookbuf)
		return 0;

	mem.vt.Read = mem_read;
	mem.vt.Seek = mem_seek;
	mem.data = (const Byte *)in;
	mem.size = in_len;
	mem.pos = 0;

	LookToRead2_CreateVTable(&look, 0);
	look.realStream = &mem.vt;
	look.buf = lookbuf;
	look.bufSize = SEVENZ_LOOK;
	LookToRead2_INIT(&look);

	SzArEx_Init(&db);
	if (SzArEx_Open(&db, &look.vt, &alloc, &alloc) != SZ_OK) {
		free(lookbuf);
		SzArEx_Free(&db, &alloc);
		return 0;
	}

	for (i = 0; i < db.NumFiles; ++i) {
		UInt16 name16[GC_MAX_PATH];
		char name[GC_MAX_PATH];
		size_t offset = 0, outSize = 0;
		size_t nlen;
		if (SzArEx_IsDir(&db, i))
			continue;
		nlen = SzArEx_GetFileNameUtf16(&db, i, NULL);
		if (nlen == 0 || nlen >= GC_MAX_PATH)
			continue;
		SzArEx_GetFileNameUtf16(&db, i, name16);
		utf16_to_ascii(name16, nlen, name, sizeof name);
		if (SzArEx_Extract(&db, &look.vt, i, &blockIndex, &outBuf, &outBufSize,
		                   &offset, &outSize, &alloc, &alloc) != SZ_OK)
			continue;
		if (name_is_m3u(name) && m3u && !m3u->data && outSize) {
			m3u->data = (unsigned char *)malloc(outSize);
			if (m3u->data) {
				memcpy(m3u->data, outBuf + offset, outSize);
				m3u->len = outSize;
				memcpy(m3u->name, name, sizeof m3u->name);
			}
			continue;
		}
		if (gc_is_psf_lib(name))
			continue;
		if (!found && outSize >= 4) {
			gc_format fmt = gc_probe(outBuf + offset, outSize, name);
			if (gc_format_claimed(fmt) && fmt != GC_FMT_ZIP &&
			    fmt != GC_FMT_SEVENZ && fmt != GC_FMT_GZIP) {
				music->data = (unsigned char *)malloc(outSize);
				if (music->data) {
					memcpy(music->data, outBuf + offset, outSize);
					music->len = outSize;
					memcpy(music->name, name, sizeof music->name);
					found = 1;
				}
			}
		}
	}

	ISzAlloc_Free(&alloc, outBuf);
	SzArEx_Free(&db, &alloc);
	free(lookbuf);
	return found;
}

int gc_sevenz_extract_all(const unsigned char *in, size_t in_len, gc_vfs *vfs)
{
	static int crc_ok;
	MemSeek mem;
	CLookToRead2 look;
	CSzArEx db;
	ISzAlloc alloc = { SzAlloc, SzFree };
	Byte *lookbuf = NULL;
	UInt32 blockIndex = 0xFFFFFFFF;
	Byte *outBuf = NULL;
	size_t outBufSize = 0;
	UInt32 i;

	if (!vfs || !in || in_len < 32)
		return 0;
	if (!(in[0] == '7' && in[1] == 'z' && in[2] == 0xBC &&
	      in[3] == 0xAF && in[4] == 0x27 && in[5] == 0x1C))
		return 0;
	if (!crc_ok) {
		CrcGenerateTable();
		crc_ok = 1;
	}
	lookbuf = (Byte *)malloc(SEVENZ_LOOK);
	if (!lookbuf)
		return 0;
	mem.vt.Read = mem_read;
	mem.vt.Seek = mem_seek;
	mem.data = (const Byte *)in;
	mem.size = in_len;
	mem.pos = 0;
	LookToRead2_CreateVTable(&look, 0);
	look.realStream = &mem.vt;
	look.buf = lookbuf;
	look.bufSize = SEVENZ_LOOK;
	LookToRead2_INIT(&look);
	SzArEx_Init(&db);
	if (SzArEx_Open(&db, &look.vt, &alloc, &alloc) != SZ_OK) {
		free(lookbuf);
		SzArEx_Free(&db, &alloc);
		return 0;
	}
	for (i = 0; i < db.NumFiles; ++i) {
		UInt16 name16[GC_MAX_PATH];
		char name[GC_MAX_PATH];
		size_t offset = 0, outSize = 0;
		size_t nlen;
		unsigned char *copy;
		if (SzArEx_IsDir(&db, i))
			continue;
		nlen = SzArEx_GetFileNameUtf16(&db, i, NULL);
		if (nlen == 0 || nlen >= GC_MAX_PATH)
			continue;
		SzArEx_GetFileNameUtf16(&db, i, name16);
		utf16_to_ascii(name16, nlen, name, sizeof name);
		if (SzArEx_Extract(&db, &look.vt, i, &blockIndex, &outBuf, &outBufSize,
		                   &offset, &outSize, &alloc, &alloc) != SZ_OK)
			continue;
		if (!outSize)
			continue;
		copy = (unsigned char *)malloc(outSize);
		if (!copy)
			continue;
		memcpy(copy, outBuf + offset, outSize);
		if (!gc_vfs_add(vfs, name, copy, outSize))
			free(copy);
	}
	ISzAlloc_Free(&alloc, outBuf);
	SzArEx_Free(&db, &alloc);
	free(lookbuf);
	return vfs->count > 0;
}
