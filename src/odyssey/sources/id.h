#ifndef ODYSSEY_ID_H
#define ODYSSEY_ID_H

/*
 * Odyssey.
 *
 * Scalable PostgreSQL connection pooler.
 */

typedef struct od_id od_id_t;
typedef struct od_id_mgr od_id_mgr_t;

#define OD_ID_SEEDMAX 6
#define OD_ID_LEN OD_ID_SEEDMAX * 2

struct od_id {
	char *id_prefix;
	char id[OD_ID_LEN];
	uint64_t id_a;
	uint64_t id_b;
};

static inline int od_id_cmp(od_id_t *a, od_id_t *b)
{
	return memcmp(a->id, b->id, sizeof(a->id)) == 0;
}

static inline void od_id_generate(od_id_t *id, char *prefix)
{
	long int a = machine_lrand48();
	long int b = machine_lrand48();

	char seed[OD_ID_SEEDMAX];
	memcpy(seed + 0, &a, 4);
	memcpy(seed + 4, &b, 2);

	id->id_prefix = prefix;
	id->id_a = a;
	id->id_b = b;

	static const char *hex = "0123456789abcdef";
	int q, w;
	for (q = 0, w = 0; q < OD_ID_SEEDMAX; q++) {
		id->id[w++] = hex[(seed[q] >> 4) & 0x0F];
		id->id[w++] = hex[(seed[q]) & 0x0F];
	}
#if OD_DEVEL_LVL != -1
	assert(w == (OD_ID_SEEDMAX * 2));
#endif
}

void od_id_generator_seed(void);

#endif /* ODYSSEY_ID_H */
