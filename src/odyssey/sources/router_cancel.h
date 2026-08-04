#ifndef ODYSSEY_ROUTER_CANCEL_H
#define ODYSSEY_ROUTER_CANCEL_H

/*
 * Odyssey.
 *
 * Scalable PostgreSQL connection pooler.
 */

#include <kiwi.h>

typedef struct {
	od_id_t id;
	od_rule_storage_t *storage;
	kiwi_key_t key;
} od_router_cancel_t;

static inline void od_router_cancel_init(od_router_cancel_t *cancel)
{
	cancel->storage = NULL;
	kiwi_key_init(&cancel->key);
}

static inline void od_router_cancel_free(od_router_cancel_t *cancel)
{
	if (cancel->storage)
		od_rules_storage_free(cancel->storage);
}

#endif /* ODYSSEY_ROUTER_CANCEL_H */
