
/*
 * Odyssey.
 *
 * Scalable PostgreSQL connection pooler.
 */

#include <kiwi.h>
#include <machinarium.h>
#include <odyssey.h>

inline od_counter_llist_t *od_counter_llist_create(void)
{
	od_counter_llist_t *llist = malloc(sizeof(od_counter_llist_t));
	if (llist == NULL)
		return NULL;

	llist->list = NULL;
	llist->count = 0;

	return llist;
}

od_bucket_t *od_bucket_create(void)
{
	od_bucket_t *b = malloc(sizeof(od_bucket_t));
	if (b == NULL)
		return NULL;

	b->l = od_counter_llist_create();
	const int res = pthread_mutex_init(&b->mutex, NULL);
	if (!res) {
		return b;
	}

	return NULL;
}

void od_bucket_free(od_bucket_t *b)
{
	od_counter_llist_free(b->l);
	pthread_mutex_destroy(&b->mutex);
	free(b);
}

inline void od_counter_llist_add(od_counter_llist_t *llist,
				 const od_counter_item_t *it)
{
	od_counter_litem_t *litem = malloc(sizeof(od_counter_litem_t));
	if (litem == NULL)
		return;
	litem->value = *it;
	litem->cnt = 1;

	litem->next = llist->list;
	llist->list = litem;

	++llist->count;
}

inline od_retcode_t od_counter_llist_free(od_counter_llist_t *l)
{
	od_counter_litem_t *next = NULL;

	for (od_counter_litem_t *it = l->list; it != NULL; it = next) {
		next = it->next;
		free(it);
	}
	free(l);
	return OK_RESPONSE;
}

static size_t od_counter_required_buf_size(int sz)
{
	return sizeof(od_counter_t) + (sz * sizeof(od_bucket_t));
}

od_counter_t *od_counter_create(size_t sz)
{
	od_counter_t *t = malloc(od_counter_required_buf_size(sz));
	if (t == NULL) {
		goto error;
	}
	t->size = sz;

	for (size_t i = 0; i < t->size; ++i) {
		t->buckets[i] = od_bucket_create();
		if (t->buckets[i] == NULL) {
			goto error;
		}
	}

	return t;
error:
	if (t) {
		for (size_t i = 0; i < t->size; ++i) {
			if (t->buckets[i] == NULL)
				continue;

			od_bucket_free(t->buckets[i]);
		}

		free(t);
	}
	return NULL;
}

od_counter_t *od_counter_create_default(void)
{
	return od_counter_create(OD_DEFAULT_HASH_TABLE_SIZE);
}

od_retcode_t od_counter_free(od_counter_t *t)
{
	for (size_t i = 0; i < t->size; ++i) {
		od_bucket_free(t->buckets[i]);
	}

	free(t);

	return OK_RESPONSE;
}

void od_counter_inc(od_counter_t *t, od_counter_item_t item)
{
	od_counter_item_t key = od_hash_item(t, item);
	/*
	 * prevent concurrent access to
	 * modify hash table section
	 */
	pthread_mutex_lock(&t->buckets[key]->mutex);
	{
		bool fnd = false;

		for (od_counter_litem_t *it = t->buckets[key]->l->list;
		     it != NULL; it = it->next) {
			if (it->value == item) {
				++it->cnt;
				fnd = true;
				break;
			}
		}
		if (!fnd)
			od_counter_llist_add(t->buckets[key]->l, &item);
	}
	pthread_mutex_unlock(&t->buckets[key]->mutex);
}

od_count_t od_counter_get_count(od_counter_t *t, od_counter_item_t value)
{
	od_counter_item_t key = od_hash_item(t, value);

	od_count_t ret_val = 0;

	pthread_mutex_lock(&t->buckets[key]->mutex);
	{
		for (od_counter_litem_t *it = t->buckets[key]->l->list;
		     it != NULL; it = it->next) {
			if (it->value == value) {
				ret_val = it->cnt;
				break;
			}
		}
	}
	pthread_mutex_unlock(&t->buckets[key]->mutex);

	return ret_val;
}

static inline od_retcode_t od_counter_reset_target_bucket(od_counter_t *t,
							  size_t bucket_key)
{
	pthread_mutex_lock(&t->buckets[bucket_key]->mutex);
	{
		for (od_counter_litem_t *it = t->buckets[bucket_key]->l->list;
		     it != NULL; it = it->next) {
			it->value = 0;
		}
	}
	pthread_mutex_unlock(&t->buckets[bucket_key]->mutex);

	return OK_RESPONSE;
}

od_retcode_t od_counter_reset(od_counter_t *t, od_counter_item_t value)
{
	od_counter_item_t key = od_hash_item(t, value);

	pthread_mutex_lock(&t->buckets[key]->mutex);
	{
		for (od_counter_litem_t *it = t->buckets[key]->l->list;
		     it != NULL; it = it->next) {
			if (it->value == value) {
				it->value = 0;
				break;
			}
		}
	}
	pthread_mutex_unlock(&t->buckets[key]->mutex);
	return OK_RESPONSE;
}

od_retcode_t od_counter_reset_all(od_counter_t *t)
{
	for (size_t i = 0; i < t->size; ++i) {
		od_counter_reset_target_bucket(t, i);
	}
	return OK_RESPONSE;
}
