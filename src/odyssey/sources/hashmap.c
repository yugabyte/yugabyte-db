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
	if (list == NULL) {
		return NULL;
	}

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
	if (l->key.data)
		free(l->key.data);
	if (l->value.data)
		free(l->value.data);
	free(l);

	return OK_RESPONSE;
}

static inline od_retcode_t od_hash_bucket_init(od_hashmap_bucket_t **b)
{
	*b = malloc(sizeof(od_hashmap_bucket_t));
	if (*b == NULL)
		return NOT_OK_RESPONSE;

	pthread_mutex_init(&(*b)->mu, NULL);
	(*b)->nodes = od_hashmap_list_item_create();
	if ((*b)->nodes == NULL)
		return NOT_OK_RESPONSE;

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
		if (od_hash_bucket_init(&hm->buckets[i]) == NOT_OK_RESPONSE) {
			for (size_t j = 0; j < i; ++j) {
				od_hash_bucket_free(hm->buckets[j]);
			}
			free(hm->buckets);
			free(hm);
			return NULL;
		}
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

static inline od_hashmap_list_item_t *yb_od_bucket_search_by_keyhash(od_hashmap_bucket_t *b,
						      od_hash_t keyhash)
{
	od_list_t *i;
	od_list_foreach(&(b->nodes->link), i)
	{
		od_hashmap_list_item_t *item;
		item = od_container_of(i, od_hashmap_list_item_t, link);
		od_hash_t hash = od_murmur_hash(item->key.data, item->key.len);
		if (hash == keyhash) {
			// find
			return item;
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
		if (it != NULL) {
			od_hashmap_elt_copy(&it->key, key);
			od_hashmap_elt_copy(&it->value, *value);

			od_hashmap_list_item_add(
				hm->buckets[bucket_index]->nodes, it);
			ret = 0;
		} else {
			/* oom or other error */
			pthread_mutex_unlock(&hm->buckets[bucket_index]->mu);
			return -1;
		}
	} else {
		/* 
		 * Element already exists.
		 * Copy *value content to ptr data, free previous value.
		 */
		free(ptr->data);
		od_hashmap_elt_copy(ptr, *value);
		*value = ptr;
	}

	pthread_mutex_unlock(&hm->buckets[bucket_index]->mu);
	return ret;
}

bool yb_od_hashmap_find_key_and_remove(od_hashmap_t *hm, od_hash_t keyhash)
{
	size_t bucket_index = keyhash % hm->size;
	pthread_mutex_lock(&hm->buckets[bucket_index]->mu);

	od_hashmap_list_item_t *item = yb_od_bucket_search_by_keyhash(hm->buckets[bucket_index], keyhash);
	if (item != NULL) {
		od_hashmap_list_item_free(item);
		pthread_mutex_unlock(&hm->buckets[bucket_index]->mu);
		return true;
	}
	pthread_mutex_unlock(&hm->buckets[bucket_index]->mu);
	return false;
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

