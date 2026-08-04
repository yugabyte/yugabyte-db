#ifndef KIWI_MD5_H
#define KIWI_MD5_H

/*
 * kiwi.
 *
 * postgreSQL protocol interaction library.
 */

typedef struct kiwi_md5 kiwi_md5_t;

struct kiwi_md5 {
	uint32_t state[4];
	uint64_t count;
	uint8_t buffer[64];
};

void kiwi_md5_init(kiwi_md5_t *);
void kiwi_md5_update(kiwi_md5_t *, void *input, size_t len);
void kiwi_md5_final(kiwi_md5_t *, uint8_t digest[16]);
void kiwi_md5_tostring(char *dest, uint8_t digest[16]);

#endif /* KIWI_MD5_H */
