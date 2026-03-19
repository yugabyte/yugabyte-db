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

/*
 * This function finds all keys that match the given keyhash and removes them from the hashmap.
 * It is done to handle possible hashmap collisions (i.e different keys with the same hash) during
 * pipelining. Therefore remove all the matching entries from the hashmap and report them to
 * the caller to log them.
 */

bool yb_od_hashmap_find_key_and_remove(od_hashmap_t *hm, od_hash_t keyhash,
					char ***matched_keys, int *matched_count)
{
	*matched_keys = NULL;
	*matched_count = 0;

	od_hashmap_list_item_t *matched_item = NULL;

	size_t bucket_index = keyhash % hm->size;
	pthread_mutex_lock(&hm->buckets[bucket_index]->mu);

	/*
	 * First pass: count matching entries so we can allocate the array
	 * in a single shot.
	 */
	int count = 0;
	od_list_t *i;
	od_list_foreach(&(hm->buckets[bucket_index]->nodes->link), i)
	{
		od_hashmap_list_item_t *item;
		item = od_container_of(i, od_hashmap_list_item_t, link);
		od_hash_t hash = od_murmur_hash(item->key.data, item->key.len);
		if (hash == keyhash)
		{
			matched_item = item;
			count++;
		}
	}

	if (count == 0) {
		pthread_mutex_unlock(&hm->buckets[bucket_index]->mu);
		return false;
	}

	if (count == 1) {
		od_hashmap_list_item_free(matched_item);
		pthread_mutex_unlock(&hm->buckets[bucket_index]->mu);
		return true;
	}

	char **keys = malloc(count * sizeof(char *));
	if (keys == NULL) {
		pthread_mutex_unlock(&hm->buckets[bucket_index]->mu);
		return false;
	}

	/*
	 * Second pass: copy key strings into the array and remove the
	 * matching items from the bucket.
	 */
	int idx = 0;
	od_list_t *n;
	od_list_foreach_safe(&(hm->buckets[bucket_index]->nodes->link), i, n)
	{
		od_hashmap_list_item_t *item;
		item = od_container_of(i, od_hashmap_list_item_t, link);
		od_hash_t hash = od_murmur_hash(item->key.data, item->key.len);
		if (hash == keyhash) {
			keys[idx] = malloc(item->key.len + 1);
			if (keys[idx] == NULL) {
				for (int j = 0; j < idx; j++)
					free(keys[j]);
				free(keys);
				pthread_mutex_unlock(&hm->buckets[bucket_index]->mu);
				return false;
			}
			memcpy(keys[idx], item->key.data, item->key.len);
			keys[idx][item->key.len] = '\0';
			idx++;
			od_hashmap_list_item_free(item);
		}
	}

	pthread_mutex_unlock(&hm->buckets[bucket_index]->mu);

	*matched_keys = keys;
	*matched_count = count;
	return true;
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

