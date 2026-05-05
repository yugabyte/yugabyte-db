#ifndef OD_MURMURHASH_H
#define OD_MURMURHASH_H

// 8 hex
#define OD_HASH_LEN 9

typedef uint32_t od_hash_t;

od_hash_t od_murmur_hash(const void *data, size_t size);

#endif /* OD_MURMURHASH_H */
