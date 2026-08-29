#ifndef GAMECHIP_ARCHIVE_H
#define GAMECHIP_ARCHIVE_H

#include "gamechip.h"
#include "vfs.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct gc_blob {
	unsigned char *data;
	size_t len;
	char name[GC_MAX_PATH];
} gc_blob;

/* Inflate gzip (VGZ / .gz). Caller frees *out. */
int gc_inflate_gzip(const unsigned char *in, size_t in_len, unsigned char **out, size_t *out_len);

/* Extract first claimed music file from a zip, plus optional sibling .m3u. */
int gc_zip_extract_music(const unsigned char *zip, size_t zip_len,
                         gc_blob *music, gc_blob *m3u);

/* Same for 7z (LZMA SDK). foobar-style: we open the archive and read sibling .m3u. */
int gc_sevenz_extract_music(const unsigned char *in, size_t in_len,
                            gc_blob *music, gc_blob *m3u);
int gc_sevenz_extract_all(const unsigned char *in, size_t in_len, gc_vfs *vfs);

/* Zip or 7z. */
int gc_archive_extract_music(const unsigned char *data, size_t len,
                             gc_blob *music, gc_blob *m3u);

/* Extract every file into a VFS (foobar method: we open the archive). */
int gc_archive_extract_all(const unsigned char *data, size_t len, gc_vfs *vfs);

/* From a VFS, pick first claimed music + sibling .m3u (same stem preferred). */
int gc_vfs_pick_music(const gc_vfs *vfs, gc_blob *music, gc_blob *m3u);

#ifdef __cplusplus
}
#endif

#endif
