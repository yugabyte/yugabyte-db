
/*
 * machinarium.
 *
 * cooperative multitasking engine.
 */

#include <machinarium.h>
#include <machinarium_private.h>

void mm_coroutine_cache_init(mm_coroutine_cache_t *cache, int stack_size,
			     int stack_size_guard, int limit)
{
	mm_list_init(&cache->list);
	cache->count_free = 0;
	cache->count_total = 0;
	cache->stack_size = stack_size;
	cache->stack_size_guard = stack_size_guard;
	cache->limit = limit;
}

void mm_coroutine_cache_free(mm_coroutine_cache_t *cache)
{
	mm_list_t *i, *n;
	mm_list_foreach_safe(&cache->list, i, n)
	{
		mm_coroutine_t *coroutine;
		coroutine = mm_container_of(i, mm_coroutine_t, link);
		mm_coroutine_free(coroutine);
	}
}

void mm_coroutine_cache_stat(mm_coroutine_cache_t *cache, uint64_t *count,
			     uint64_t *count_free)
{
	*count = cache->count_total - cache->count_free;
	*count_free = cache->count_free;
}

mm_coroutine_t *mm_coroutine_cache_pop(mm_coroutine_cache_t *cache)
{
	mm_coroutine_t *coroutine;
	if (cache->count_free > 0) {
		mm_list_t *first = mm_list_pop(&cache->list);
		cache->count_free--;
		coroutine = mm_container_of(first, mm_coroutine_t, link);
		return coroutine;
	}
	cache->count_total++;

	coroutine = mm_coroutine_allocate(cache->stack_size,
					  cache->stack_size_guard);
	if (coroutine == NULL)
		cache->count_total--;
	return coroutine;
}

void mm_coroutine_cache_push(mm_coroutine_cache_t *cache,
			     mm_coroutine_t *coroutine)
{
	assert(coroutine->state == MM_CFREE);
	if (cache->count_free >= cache->limit) {
		cache->count_total--;
		mm_coroutine_free(coroutine);
		return;
	}
	mm_list_init(&coroutine->link);
	mm_list_append(&cache->list, &coroutine->link);
	cache->count_free++;
}
