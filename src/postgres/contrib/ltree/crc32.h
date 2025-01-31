#ifndef _CRC32_H
#define _CRC32_H

/* contrib/ltree/crc32.h */

/* Returns crc32 of data block */
extern unsigned int ltree_crc32_sz(const char *buf, int size);

/* Returns crc32 of null-terminated string */
#define crc32(buf) ltree_crc32_sz((buf),strlen(buf))

#endif
