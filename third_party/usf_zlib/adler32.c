#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#include "miniz.h"

unsigned long adler32(unsigned long adler, const unsigned char *buf, unsigned int len)
{
	return (unsigned long)mz_adler32((mz_ulong)adler, buf, (size_t)len);
}
