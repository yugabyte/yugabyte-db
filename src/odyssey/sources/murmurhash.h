#ifndef OD_MURMURHASH_H
#define OD_MURMURHASH_H

#ifndef YB_SUPPORT_FOUND

// 8 hex
#define OD_HASH_LEN 9

typedef uint32_t od_hash_t;

od_hash_t od_murmur_hash(const void *data, size_t size);

#endif // YB_SUPPORT_FOUND

/* YB: 64-bit hash to reduce prepared statement name collisions. */

#define YB_OD_HASH_64_LEN 17
typedef uint64_t yb_od_hash_64_t;

yb_od_hash_64_t yb_od_murmur_hash_64(const void *data, size_t size);

#endif /* OD_MURMURHASH_H */
