/* Minimal zlib.h for lazyusf2 on MinGW — function only, no name macros.
 * miniz's #define adler32 mz_adler32 would rename struct fields. */
#ifndef GC_USF_ZLIB_H
#define GC_USF_ZLIB_H
#ifdef __cplusplus
extern "C" {
#endif
unsigned long adler32(unsigned long adler, const unsigned char *buf, unsigned int len);
#ifdef __cplusplus
}
#endif
#endif
