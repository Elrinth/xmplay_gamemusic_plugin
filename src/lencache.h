#ifndef GAMECHIP_LENCACHE_H
#define GAMECHIP_LENCACHE_H

#include "gamechip.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct gc_len_rec {
	gc_format format;
	int track_count;
	int duration_ms[GC_MAX_TRACKS];
	char game[GC_MAX_TITLE];
	char artist[GC_MAX_TITLE];
	char copyright[GC_MAX_TITLE];
	char src[32];
} gc_len_rec;

void gc_len_cache_path_from_ini(char *out, size_t cap, const char *ini_path);
int64_t gc_file_mtime(const char *path);
uint64_t gc_file_size(const char *path);
int gc_len_cache_get(const char *cache_file, const char *path,
                     uint64_t size, int64_t mtime, gc_len_rec *out);
int gc_len_cache_put(const char *cache_file, const char *path,
                     uint64_t size, int64_t mtime, const gc_len_rec *in);
void gc_len_rec_from_info(gc_len_rec *out, const gc_info *inf);

#ifdef __cplusplus
}
#endif

#endif
