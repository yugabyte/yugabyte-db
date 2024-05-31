#ifndef MM_LIST_H
#define MM_LIST_H

/*
 * machinarium.
 *
 * cooperative multitasking engine.
 */

typedef struct mm_list mm_list_t;

struct mm_list {
	mm_list_t *next, *prev;
};

static inline void mm_list_init(mm_list_t *list)
{
	list->next = list->prev = list;
}

static inline void mm_list_append(mm_list_t *list, mm_list_t *node)
{
	node->next = list;
	node->prev = list->prev;
	node->prev->next = node;
	node->next->prev = node;
}

static inline void mm_list_unlink(mm_list_t *node)
{
	node->prev->next = node->next;
	node->next->prev = node->prev;
}

static inline void mm_list_push(mm_list_t *list, mm_list_t *node)
{
	node->next = list->next;
	node->prev = list;
	node->prev->next = node;
	node->next->prev = node;
}

static inline mm_list_t *mm_list_pop(mm_list_t *list)
{
	register mm_list_t *pop = list->next;
	mm_list_unlink(pop);
	return pop;
}

#define mm_list_foreach(H, I) for (I = (H)->next; I != H; I = (I)->next)

#define mm_list_foreach_safe(H, I, N) \
	for (I = (H)->next; I != H && (N = I->next); I = N)

#endif /* MM_LIST_H */
