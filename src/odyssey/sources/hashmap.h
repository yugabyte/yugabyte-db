#ifndef OD_HASHMAP_H
#define OD_HASHMAP_H

typedef struct od_hashmap_list_item od_hashmap_list_item_t;

// void * data should have following fmt:
// key
// value

// header, first keylen bytes is key, other is value
typedef struct {
	void *data;
	size_t len;
} od_hashmap_elt_t;

struct od_hashmap_list_item {
	od_hashmap_elt_t key;
	od_hashmap_elt_t value;
	od_list_t link;
};

extern od_hashmap_list_item_t *od_hashmap_list_item_create(void);

extern void od_hashmap_list_item_add(od_hashmap_list_item_t *list,
				     od_hashmap_list_item_t *it);

extern od_retcode_t od_hashmap_list_item_free(od_hashmap_list_item_t *l);

typedef struct od_hashmap_bucket {
	od_hashmap_list_item_t *nodes;
	pthread_mutex_t mu;
} od_hashmap_bucket_t;

typedef struct od_hashmap od_hashmap_t;

struct od_hashmap {
	size_t size;
	// ISO C99 flexible array member
	od_hashmap_bucket_t **buckets;
};

extern od_hashmap_t *od_hashmap_create(size_t sz);
extern od_retcode_t od_hashmap_free(od_hashmap_t *hm);
od_hashmap_elt_t *od_hashmap_find(od_hashmap_t *hm, od_hash_t keyhash,
				  od_hashmap_elt_t *key);
int od_hashmap_insert(od_hashmap_t *hm, od_hash_t keyhash,
		      od_hashmap_elt_t *key, od_hashmap_elt_t **value);

#endif /* OD_HASHMAP_H */
