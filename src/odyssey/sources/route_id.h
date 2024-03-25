#ifndef ODYSSEY_ROUTE_ID_H
#define ODYSSEY_ROUTE_ID_H

/*
 * Odyssey.
 *
 * Scalable PostgreSQL connection pooler.
 */

typedef struct od_route_id od_route_id_t;

struct od_route_id {
	char *user;
	int user_len;

	/* (YB changes)
		char *database;
		int database_len;
	*/

	bool physical_rep;
	bool logical_rep;

	int yb_stats_index;
	int yb_db_oid;
};

static inline void od_route_id_init(od_route_id_t *id)
{
	id->user = NULL;
	id->user_len = 0;
	id->physical_rep = false;
	id->logical_rep = false;
	id->yb_db_oid = -1;
}

static inline void od_route_id_free(od_route_id_t *id)
{
	if (id->user)
		free(id->user);
}

static inline int od_route_id_copy(od_route_id_t *dest, od_route_id_t *id)
{
	dest->yb_db_oid = id->yb_db_oid;

	dest->user = malloc(id->user_len);
	if (dest->user == NULL) {
		dest->yb_db_oid = -1;
		return -1;
	}
	memcpy(dest->user, id->user, id->user_len);
	dest->user_len = id->user_len;
	dest->physical_rep = id->physical_rep;
	dest->logical_rep = id->logical_rep;
	return 0;
}

static inline int od_route_id_compare(od_route_id_t *a, od_route_id_t *b)
{
	if (a->user_len == b->user_len) {
		if (a->yb_db_oid ==  b->yb_db_oid &&
		    memcmp(a->user, b->user, a->user_len) == 0 &&
		    a->logical_rep == b->logical_rep)
			if (a->physical_rep == b->physical_rep)
				return 1;
	}
	return 0;
}

#endif /* ODYSSEY_ROUTE_ID_H */
