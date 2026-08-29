#ifndef GAMECHIP_PSF_IO_H
#define GAMECHIP_PSF_IO_H

#include "vfs.h"
#include "psflib.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Fill psflib callbacks that read from an in-memory VFS, then the sidecar dir. */
void gc_psf_callbacks(psf_file_callbacks *cb, const gc_vfs *vfs);

/* PSF tag time: "m:ss", "m:ss.ms", or seconds. 0 on failure (never a dummy). */
int gc_psf_parse_time_ms(const char *s);

/* GSF length tags already include 2 loops. play = length * loops / 2
   + fade (fade 0 = natural end, no extra fade). Dummy library lengths → 0. */
int gc_gsf_play_ms(int tag_length_ms, int tag_fade_ms, int loops);

/* Join folder + _lib name into a display path. dir may be empty (archive). */
void gc_psf_lib_path(char *out, size_t cap, const char *dir, const char *lib);

#ifdef __cplusplus
}
#endif

#endif
