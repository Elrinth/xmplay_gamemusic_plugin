#ifndef GAMECHIP_VFS_H
#define GAMECHIP_VFS_H

#include "gamechip.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GC_MAX_VFS 256

typedef struct gc_vfs_file {
	char name[GC_MAX_PATH];
	unsigned char *data;
	size_t len;
} gc_vfs_file;

typedef struct gc_vfs {
	gc_vfs_file files[GC_MAX_VFS];
	int count;
	char dir[GC_MAX_PATH];
} gc_vfs;

void gc_vfs_init(gc_vfs *v);
void gc_vfs_free(gc_vfs *v);
/* Takes ownership of data (may be NULL to skip). */
int gc_vfs_add(gc_vfs *v, const char *name, unsigned char *data, size_t len);
const gc_vfs_file *gc_vfs_find(const gc_vfs *v, const char *name);
void gc_vfs_set_dir_from_path(gc_vfs *v, const char *path);
int gc_vfs_load_dir_file(const gc_vfs *v, const char *name,
                         unsigned char **out, size_t *out_len);

#ifdef __cplusplus
}
#endif

#endif
