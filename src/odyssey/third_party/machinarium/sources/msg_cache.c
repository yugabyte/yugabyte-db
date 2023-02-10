
/*
 * machinarium.
 *
 * cooperative multitasking engine.
 */

#include <machinarium.h>
#include <machinarium_private.h>

void mm_msgcache_init(mm_msgcache_t *cache)
{
	mm_list_init(&cache->list);
	cache->count = 0;
	cache->count_allocated = 0;
	cache->count_gc = 0;
	cache->size = 0;
	cache->gc_watermark = 0;
}

void mm_msgcache_free(mm_msgcache_t *cache)
{
	mm_list_t *i, *n;
	mm_list_foreach_safe(&cache->list, i, n)
	{
		mm_msg_t *msg = mm_container_of(i, mm_msg_t, link);
		mm_buf_free(&msg->data);
		free(msg);
	}
}

void mm_msgcache_stat(mm_msgcache_t *cache, uint64_t *count_allocated,
		      uint64_t *count_gc, uint64_t *count, uint64_t *size)
{
	*count_allocated = cache->count_allocated;
	*count_gc = cache->count_gc;
	*count = cache->count;
	*size = cache->size;
}

mm_msg_t *mm_msgcache_pop(mm_msgcache_t *cache)
{
	mm_msg_t *msg = NULL;
	if (cache->count > 0) {
		mm_list_t *first = mm_list_pop(&cache->list);
		cache->count--;
		msg = mm_container_of(first, mm_msg_t, link);
		cache->size -= mm_buf_size(&msg->data);
		goto init;
	}
	cache->count_allocated++;

	msg = malloc(sizeof(mm_msg_t));
	if (msg == NULL)
		return NULL;
	mm_buf_init(&msg->data);
	/* fallthrough */
init:
	msg->machine_id = mm_self->id;
	msg->refs = 0;
	msg->type = 0;
	mm_buf_reset(&msg->data);
	mm_list_init(&msg->link);
	return msg;
}

void mm_msgcache_push(mm_msgcache_t *cache, mm_msg_t *msg)
{
	if (msg->machine_id != mm_self->id ||
	    mm_buf_size(&msg->data) > cache->gc_watermark) {
		cache->count_gc++;
		mm_buf_free(&msg->data);
		free(msg);
		return;
	}

	mm_list_append(&cache->list, &msg->link);
	cache->count++;
	cache->size += mm_buf_size(&msg->data);
}
