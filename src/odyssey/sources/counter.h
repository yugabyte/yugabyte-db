#ifndef ODYSSEY_COUNTER_H
#define ODYSSEY_COUNTER_H

/*
 * Odyssey.
 *
 * Scalable PostgreSQL connection pooler.
 */

/* llist stands for linked list */
typedef struct od_hash_litem od_counter_litem_t;
typedef struct od_hash_llist od_counter_llist_t;

typedef size_t od_hash_table_key_t;
/* support only int values for now */
/* TODO: change value type to (void *) */
typedef size_t od_counter_item_t;
typedef size_t od_count_t;

struct od_hash_litem {
	od_counter_item_t value;
	od_count_t cnt;

	od_counter_litem_t *next;
};

struct od_hash_llist {
	size_t count;
	od_counter_litem_t *list;
};

extern od_counter_llist_t *od_counter_llist_create(void);

extern void od_counter_llist_add(od_counter_llist_t *llist,
				 const od_counter_item_t *it);

extern od_retcode_t od_counter_llist_free(od_counter_llist_t *l);

#define OD_DEFAULT_HASH_TABLE_SIZE 15
typedef struct od_bucket {
	od_counter_llist_t *l;
	pthread_mutex_t mutex;
} od_bucket_t;

typedef struct od_counter od_counter_t;
struct od_counter {
	size_t size;
	// ISO C99 flexible array member
	od_bucket_t *buckets[FLEXIBLE_ARRAY_MEMBER];
};

extern od_counter_t *od_counter_create(size_t sz);

extern od_counter_t *od_counter_create_default(void);

extern od_retcode_t od_counter_free(od_counter_t *t);

extern od_count_t od_counter_get_count(od_counter_t *t,
				       od_counter_item_t value);

extern od_retcode_t od_counter_reset(od_counter_t *t, od_counter_item_t value);

extern od_retcode_t od_counter_reset_all(od_counter_t *t);

extern od_retcode_t od_counter_erase(od_counter_t *t, od_hash_table_key_t key,
				     od_counter_llist_t *item);

extern void od_counter_inc(od_counter_t *t, od_counter_item_t item);

static inline od_counter_item_t od_hash_item(od_counter_t *t,
					     od_counter_item_t item)
{
	/*
	 * How to get hash of number in range 0 to MAXINT ?
	 * there are many ways, we choose simplest one
	 */
	return item % t->size;
}

/* these are function to perform dynamic counter resize in case of overfit
 * TODO: implement them
 * */

extern od_retcode_t od_counter_resize_up(od_counter_t *t);

extern od_retcode_t od_counter_resize_down(od_counter_t *t);

#endif // ODYSSEY_COUNTER_H
