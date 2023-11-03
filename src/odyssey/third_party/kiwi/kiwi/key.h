#ifndef KIWI_KEY_H
#define KIWI_KEY_H

/*
 * kiwi.
 *
 * postgreSQL protocol interaction library.
 */

typedef struct kiwi_key kiwi_key_t;

struct kiwi_key {
	uint32_t key;
	uint32_t key_pid;
};

static inline void kiwi_key_init(kiwi_key_t *key)
{
	key->key = 0;
	key->key_pid = 0;
}

static inline int kiwi_key_cmp(kiwi_key_t *a, kiwi_key_t *b)
{
	return a->key == b->key && a->key_pid == b->key_pid;
}

#endif /* KIWI_KEY_H */
