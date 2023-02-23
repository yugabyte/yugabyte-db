/*
 * Odyssey.
 *
 * Scalable PostgreSQL connection pooler.
 */

#include <kiwi.h>
#include <machinarium.h>
#include <odyssey.h>

od_hashmap_list_item_t *od_hashmap_list_item_create(void)
{
	od_hashmap_list_item_t *list;
	list = malloc(sizeof(od_hashmap_list_item_t));

	memset(list, 0, sizeof(od_hashmap_list_item_t));
	od_list_init(&list->link);
	return list;
}

void od_hashmap_list_item_add(od_hashmap_list_item_t *list,
			      od_hashmap_list_item_t *it)
{
	od_list_append(&list->link, &it->link);
}

od_retcode_t od_hashmap_list_item_free(od_hashmap_list_item_t *l)
{
	od_list_unlink(&l->link);
	free(l->key.data);
	free(l->value.data);
	free(l);

	return OK_RESPONSE;
}

static inline od_retcode_t od_hash_bucket_init(od_hashmap_bucket_t **b)
{
	*b = malloc(sizeof(od_hashmap_bucket_t));
	pthread_mutex_init(&(*b)->mu, NULL);
	(*b)->nodes = od_hashmap_list_item_create();

	return OK_RESPONSE;
}

static inline od_retcode_t od_hash_bucket_free(od_hashmap_bucket_t *b)
{
	pthread_mutex_destroy(&b->mu);
	od_hashmap_list_item_free(b->nodes);

	free(b);
	return OK_RESPONSE;
}

od_hashmap_t *od_hashmap_create(size_t sz)
{
	od_hashmap_t *hm;

	hm = malloc(sizeof(od_hashmap_t));
	if (hm == NULL) {
		return NULL;
	}

	hm->size = sz;
	hm->buckets = malloc(sz * sizeof(od_hashmap_bucket_t *));

	if (hm->buckets == NULL) {
		free(hm);
		return NULL;
	}

	for (size_t i = 0; i < sz; ++i) {
		od_hash_bucket_init(&hm->buckets[i]);
	}

	return hm;
}

od_retcode_t od_hashmap_free(od_hashmap_t *hm)
{
	for (size_t i = 0; i < hm->size; ++i) {
		od_list_t *j, *n;

		od_list_foreach_safe(&hm->buckets[i]->nodes->link, j, n)
		{
			od_hashmap_list_item_t *it;
			it = od_container_of(j, od_hashmap_list_item_t, link);
			od_hashmap_list_item_free(it);
		}

		od_hash_bucket_free(hm->buckets[i]);
	}

	free(hm->buckets);
	free(hm);

	return OK_RESPONSE;
}

static inline od_hashmap_elt_t *od_bucket_search(od_hashmap_bucket_t *b,
						 void *value, size_t value_len)
{
	od_list_t *i;
	od_list_foreach(&(b->nodes->link), i)
	{
		od_hashmap_list_item_t *item;
		item = od_container_of(i, od_hashmap_list_item_t, link);
		if (item->key.len == value_len &&
		    memcmp(item->key.data, value, value_len) == 0) {
			// find
			return &item->value;
		}
	}

	return NULL;
}

static inline int od_hashmap_elt_copy(od_hashmap_elt_t *dst,
				      od_hashmap_elt_t *src)
{
	dst->len = src->len;
	dst->data = malloc(src->len * sizeof(char));
	if (dst->data == NULL) {
		return -1;
	}

	memcpy(dst->data, src->data, src->len);
	return 0;
}

int od_hashmap_insert(od_hashmap_t *hm, od_hash_t keyhash,
		      od_hashmap_elt_t *key, od_hashmap_elt_t **value)
{
	size_t bucket_index = keyhash % hm->size;
	pthread_mutex_lock(&hm->buckets[bucket_index]->mu);

	od_hashmap_elt_t *ptr = od_bucket_search(hm->buckets[bucket_index],
						 key->data, key->len);

	int ret = 1;

	if (ptr == NULL) {
		od_hashmap_list_item_t *it;
		it = od_hashmap_list_item_create();

		od_hashmap_elt_copy(&it->key, key);
		od_hashmap_elt_copy(&it->value, *value);

		od_hashmap_list_item_add(hm->buckets[bucket_index]->nodes, it);
		ret = 0;
	} else {
		free(ptr->data);
		od_hashmap_elt_copy(ptr, *value);
		*value = ptr;
	}

	pthread_mutex_unlock(&hm->buckets[bucket_index]->mu);
	return ret;
}

od_hashmap_elt_t *od_hashmap_find(od_hashmap_t *hm, od_hash_t keyhash,
				  od_hashmap_elt_t *key)
{
	size_t bucket_index = keyhash % hm->size;
	pthread_mutex_lock(&hm->buckets[bucket_index]->mu);

	od_hashmap_elt_t *ptr = od_bucket_search(hm->buckets[bucket_index],
						 key->data, key->len);

	pthread_mutex_unlock(&hm->buckets[bucket_index]->mu);
	return ptr;
}
