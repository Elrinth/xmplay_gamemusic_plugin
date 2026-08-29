#ifndef GAMECHIP_PROBE_H
#define GAMECHIP_PROBE_H

#include "gamechip.h"

#ifdef __cplusplus
extern "C" {
#endif

gc_format gc_probe(const unsigned char *data, size_t len, const char *filename);
int gc_format_claimed(gc_format fmt); /* 1 if we take it in CheckFile */
const char *gc_format_name(gc_format fmt);
const char *gc_format_ext(gc_format fmt);
int gc_is_sap(const unsigned char *data, size_t len, const char *filename);
/* Sibling GSF/USF libraries — keep in the VFS, never CheckFile / playlist. */
int gc_is_psf_lib(const char *filename);

#ifdef __cplusplus
}
#endif

#endif
