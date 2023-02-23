#ifndef MM_COROUTINE_CACHE_H
#define MM_COROUTINE_CACHE_H

/*
 * machinarium.
 *
 * cooperative multitasking engine.
 */

typedef struct mm_coroutine_cache mm_coroutine_cache_t;

struct mm_coroutine_cache {
	int stack_size;
	int stack_size_guard;
	mm_list_t list;
	int count_free;
	int count_total;
	int limit;
};

void mm_coroutine_cache_init(mm_coroutine_cache_t *, int, int, int);
void mm_coroutine_cache_free(mm_coroutine_cache_t *);
void mm_coroutine_cache_stat(mm_coroutine_cache_t *, uint64_t *, uint64_t *);

mm_coroutine_t *mm_coroutine_cache_pop(mm_coroutine_cache_t *);

void mm_coroutine_cache_push(mm_coroutine_cache_t *, mm_coroutine_t *);

#endif /* MM_COROUTINE_CACHE_H */
