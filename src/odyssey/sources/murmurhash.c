
/*
 * Odyssey.
 *
 * Scalable PostgreSQL connection pooler.
 */

#include <kiwi.h>
#include <machinarium.h>
#include <odyssey.h>

#ifndef YB_SUPPORT_FOUND

// from https://github.com/aappleby/smhasher/blob/master/src/MurmurHash1.cpp
//

/*
 * Copyright (C) Austin Appleby
 */
const od_hash_t seed = 0x5bd1e995;

od_hash_t od_murmur_hash(const void *raw, size_t len)
{
	const unsigned int m = 0xc6a4a793;

	const int r = 16;

	unsigned int h = seed ^ (len * m);
	unsigned int k;

	const unsigned char *data = (const unsigned char *)raw;
	char buf[4]; // raw may be misaligned

	while (len >= 4) {
		memcpy(buf, data, 4);
		k = *(unsigned int *)buf;

		h += k;
		h *= m;
		h ^= h >> 16;

		data += 4;
		len -= 4;
	}

	memset(buf, 0, 4);
	memcpy(buf, data, len);

	//----------

	/* handle safe promotion of char to unsigned int to handle ASAN failures */
	switch (len) {
	case 3:
		h += (unsigned char)buf[2] << 16;
		break;
	case 2:
		h += (unsigned char)buf[1] << 8;
		break;
	case 1:
		h += buf[0];
		h *= m;
		h ^= h >> r;
		break;
	};

	//----------

	h *= m;
	h ^= h >> 10;
	h *= m;
	h ^= h >> 17;

	return h;
}

#endif // YB_SUPPORT_FOUND

/*
 * YB: MurmurHash2 64-bit (MurmurHash64A) by Austin Appleby, public domain.
 *
 * Upgraded from the original 32-bit MurmurHash1 to reduce hash collisions in
 * prepared statement names. C++ versions of this same algorithm exist in:
 *   - src/yb/util/hash_util.h        (HashUtil::MurmurHash2_64)
 *   - src/yb/rocksdb/util/murmurhash.cc (MurmurHash64A)
 *
 * Copyright (C) Austin Appleby
 */

const yb_od_hash_64_t seed_64 = 0x5bd1e995;

yb_od_hash_64_t yb_od_murmur_hash_64(const void *raw, size_t len)
{
	const uint64_t m = 0xc6a4a7935bd1e995ULL;
	const int r = 47;

	uint64_t h = seed_64 ^ (len * m);

	const unsigned char *data = (const unsigned char *)raw;
	const unsigned char *end = data + (len & ~(size_t)7);

	while (data != end) {
		uint64_t k;
		memcpy(&k, data, 8);

		k *= m;
		k ^= k >> r;
		k *= m;

		h ^= k;
		h *= m;

		data += 8;
	}

	switch (len & 7) {
	case 7:
		h ^= (uint64_t)(unsigned char)data[6] << 48;
		yb_od_attribute_fallthrough;
	case 6:
		h ^= (uint64_t)(unsigned char)data[5] << 40;
		yb_od_attribute_fallthrough;
	case 5:
		h ^= (uint64_t)(unsigned char)data[4] << 32;
		yb_od_attribute_fallthrough;
	case 4:
		h ^= (uint64_t)(unsigned char)data[3] << 24;
		yb_od_attribute_fallthrough;
	case 3:
		h ^= (uint64_t)(unsigned char)data[2] << 16;
		yb_od_attribute_fallthrough;
	case 2:
		h ^= (uint64_t)(unsigned char)data[1] << 8;
		yb_od_attribute_fallthrough;
	case 1:
		h ^= (uint64_t)(unsigned char)data[0];
		h *= m;
	};

	h ^= h >> r;
	h *= m;
	h ^= h >> r;

	return h;
}
